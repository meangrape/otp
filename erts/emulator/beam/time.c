/*
 * %CopyrightBegin%
 *
 * Copyright Ericsson AB 1996-2013. All Rights Reserved.
 *
 * The contents of this file are subject to the Erlang Public License,
 * Version 1.1, (the "License"); you may not use this file except in
 * compliance with the License. You should have received a copy of the
 * Erlang Public License along with this software. If not, it can be
 * retrieved online at http://www.erlang.org/.
 *
 * Software distributed under the License is distributed on an "AS IS"
 * basis, WITHOUT WARRANTY OF ANY KIND, either express or implied. See
 * the License for the specific language governing rights and limitations
 * under the License.
 *
 * %CopyrightEnd%
 */

/*
 * TIMING WHEEL
 *
 * Timeouts kept in an wheel. A timeout is measured relative to the
 * current slot (tiw_pos) in the wheel, and inserted at slot
 * (tiw_pos + timeout) % ERTS_TIW_SIZE. Each timeout also has a count
 * equal to timeout/ERTS_TIW_SIZE, which is needed since the time axis
 * is wrapped arount the wheel.
 *
 * Several slots may be processed in one operation. If the number of
 * slots is greater that the wheel size, the wheel is only traversed
 * once,
 *
 * The following example shows a time axis where there is one timeout
 * at each "tick", and where 1, 2, 3 ... wheel slots are released in
 * one operation. The notation "<x" means "release all items with
 * counts less than x".
 *
 * Size of wheel: 4
 *
 *   --|----|----|----|----|----|----|----|----|----|----|----|----|----
 *    0.0  0.1  0.2  0.3  1.0  1.1  1.2  1.3  2.0  2.1  2.2  2.3  3.0
 *
 * 1   [    )
 *     <1  0.1  0.2  0.3  0.0  1.1  1.2  1.3  1.0  2.1  2.2  2.3  2.0
 *
 * 2   [         )
 *     <1   <1  0.2  0.3  0.0  0.1  1.2  1.3  1.0  1.1  2.2  2.3  2.0
 *
 * 3   [              )
 *     <1   <1   <1  0.3  0.0  0.1  0.2  1.3  1.0  1.1  1.2  2.3  2.0
 *
 * 4   [                   )
 *     <1   <1   <1   <1  0.0  0.1  0.2  0.3  1.0  1.1  1.2  1.3  2.0
 *
 * 5   [                        )
 *     <2   <1   <1   <1.      0.1  0.2  0.3  0.0  1.1  1.2  1.3  1.0
 *
 * 6   [                             )
 *     <2   <2   <1   <1.           0.2  0.3  0.0  0.1  1.2  1.3  1.0
 *
 * 7   [                                  )
 *     <2   <2   <2   <1.                0.3  0.0  0.1  0.2  1.3  1.0
 *
 * 8   [                                       )
 *     <2   <2   <2   <2.                     0.0  0.1  0.2  0.3  1.0
 *
 * 9   [                                            )
 *     <3   <2   <2   <2.                          0.1  0.2  0.3  0.0
 *
 */

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include "sys.h"
#include "erl_vm.h"
#include "global.h"
#include "erl_time.h"

#ifdef ERTS_ENABLE_LOCK_CHECK
#define ASSERT_NO_LOCKED_LOCKS		erts_lc_check_exact(NULL, 0)
#else
#define ASSERT_NO_LOCKED_LOCKS
#endif

typedef struct ErlTimerHead_ {
    ErlTimer  * head;
    ErlTimer  * tail;
} ErlTimerHead;

/* BEGIN tiw_lock protected variables
**
** The individual timer cells in tiw are also protected by the same mutex.
*/
#define TIW_LOC_STRATEGY_NONE   0
#define TIW_LOC_STRATEGY_SMPMTX 1

#define TIW_LOC_STRATEGY    TIW_LOC_STRATEGY_SMPMTX

#if     (TIW_LOC_STRATEGY == TIW_LOC_STRATEGY_NONE)
#define TIW_LOCK_DECLARE
#define TIW_LOCK_INIT(TiwPtr)
#define TIW_LOCK_ACQUIRE(TiwPtr)
#define TIW_LOCK_RELEASE(TiwPtr)
#define TIW_LOCK_DESTROY(TiwPtr)

#elif   (TIW_LOC_STRATEGY == TIW_LOC_STRATEGY_SMPMTX)
#define TIW_LOCK_DECLARE            erts_smp_mtx_t tiw_sync;
#define TIW_LOCK_INIT(TiwPtr)       erts_smp_mtx_init(& (TiwPtr)->tiw_sync, "timer_wheel")
#define TIW_LOCK_ACQUIRE(TiwPtr)    erts_smp_mtx_lock(& (TiwPtr)->tiw_sync)
#define TIW_LOCK_RELEASE(TiwPtr)    erts_smp_mtx_unlock(& (TiwPtr)->tiw_sync)
#define TIW_LOCK_DESTROY(TiwPtr)

#else
#error  Unimplemented TIW_LOC_STRATEGY
#endif

/*
    tiw_index_t is an unsigned integer type that
    - can hold at least ERTS_TIW_SIZE
    - can be read in a single operation by the CPU
    it is NOT to be used in general atomic operations, but is checked
    before acquiring a lock

    This was originally a Uint in all cases, although that type is larger than
    the native CPU word size on certain platform configurations (e.g. 64 bits
    most 32-bit platforms). ERTS_TIW_SIZE fits comfortably within 32 bits, so
    prefer the native 'unsigned' type, which should be an atomic read on pretty
    much anything unless it CANNOT hold the value, which would probably only
    happen on some crufty old 16-bit harware that we REALLY don't care about.
*/
#if (ERTS_TIW_SIZE <= UINT_MAX)
typedef unsigned    tiw_index_t;
#else
typedef Uint        tiw_index_t;
#endif

/* make it easier to twiddle member index volatility */
#if 1
#define TIW_INDEX_MEMBER_T  tiw_index_t
#else
#define TIW_INDEX_MEMBER_T  volatile tiw_index_t
#endif

typedef struct ErlTimerWheel_ {
    TIW_LOCK_DECLARE
    ErlTimerHead          * tiw;
    TIW_INDEX_MEMBER_T      tiw_pos;
    TIW_INDEX_MEMBER_T      tiw_nto;
    erts_smp_atomic32_t     tiw_min;
    ErlTimer              * tiw_min_ptr;
} ErlTimerWheel;

#define ERTS_TIW_INSTANCES  erts_no_schedulers
static ErlTimerWheel      * tiwtab;

/* END tiw_lock protected variables */

/* Actual interval time chosen by sys_init_time() */

#if SYS_CLOCK_RESOLUTION == 1
#  define TIW_ITIME 1
#  define TIW_ITIME_IS_CONSTANT
#else
static int tiw_itime; /* Constant after init */
#  define TIW_ITIME tiw_itime
#endif

erts_smp_atomic32_t do_time;    /* set at clock interrupt */

static ERTS_INLINE erts_short_time_t do_time_read(void)
{
    return erts_smp_atomic32_read_acqb(&do_time);
}

static ERTS_INLINE erts_short_time_t do_time_update(void)
{
    return do_time_read();
}

static ERTS_INLINE void do_time_init(void)
{
    erts_smp_atomic32_init_nob(&do_time, 0);
}

static ERTS_INLINE erts_aint32_t read_tiw_min(ErlTimerWheel* tiw)
{
    return erts_smp_atomic32_read_acqb(& tiw->tiw_min);
}

static ERTS_INLINE void set_tiw_min(ErlTimerWheel* tiw, erts_aint32_t val)
{
    erts_smp_atomic32_set_acqb(& tiw->tiw_min, val);
}

static ERTS_INLINE void reset_tiw_min(ErlTimerWheel* tiw)
{
    set_tiw_min(tiw, -1);
}

/*
    get the time (in units of TIW_ITIME) to the next timeout,
    or -1 if there are no timeouts

    PRE: tiw_lock taken by caller
*/
static erts_short_time_t next_time_internal(ErlTimerWheel* tiw)
{
    tiw_index_t         i, nto;
    erts_short_time_t   tm;
    Uint32              min;
    ErlTimer          * p;

    if (tiw->tiw_nto == 0)
        return -1;	/* no timeouts in wheel */

    if (tiw->tiw_min_ptr)
        return read_tiw_min(tiw);

    /* start going through wheel to find next timeout */
    nto = 0;
    tm  = 0;
    min = 0xffffffff;   /* max Uint32 */
    i   = tiw->tiw_pos;
    /* the loop terminates if we get back to the starting position */
    do {
        p = tiw->tiw[i].head;
        while (p != NULL) {
            nto++;
            if (p->count == 0) {
                /* found next timeout */
                /* p->count is zero */
                tiw->tiw_min_ptr = p;
                set_tiw_min(tiw, tm);
                return tm;
            } else {
                /* keep shortest time in 'min' */
                Uint32 newmin = (Uint32) (tm + (p->count * ERTS_TIW_SIZE));
                if (newmin < min) {
                    min = newmin;
                    tiw->tiw_min_ptr = p;
                    set_tiw_min(tiw, min);
                }
            }
            p = p->next;
        }
        /* when we have found all timeouts the shortest time will be in min */
        if (nto == tiw->tiw_nto)
            break;
        ++tm;
        if (++i >= ERTS_TIW_SIZE)
            i = 0;
    }
    while (i != tiw->tiw_pos);

    return (erts_short_time_t) min;
}

static void remove_timer(ErlTimerWheel* tiw, ErlTimer *p) {
    /* first */
    if (!p->prev) {
        tiw->tiw[p->slot].head = p->next;
        if(p->next)
            p->next->prev = NULL;
    } else {
        p->prev->next = p->next;
    }

    /* last */
    if (!p->next) {
        tiw->tiw[p->slot].tail = p->prev;
        if (p->prev)
            p->prev->next = NULL;
    } else {
        p->next->prev = p->prev;
    }

    p->next = NULL;
    p->prev = NULL;
    /* Make sure cancel callback isn't called */
    p->active = 0;
    tiw->tiw_nto--;
}

/* Private export to erl_time_sup.c */
erts_short_time_t erts_next_time(void)
{
    erts_short_time_t ret, dt;
    Uint32 min;
    int n;

    dt = do_time_update();
    for (n = 0; n < ERTS_TIW_INSTANCES; ++n) {
        ret = read_tiw_min(tiwtab + n);
        if (ret >= 0 && ret <= dt) {
            return 0;
        }
    }

    min = (Uint32) -1; /* max Uint32 */
    for (n = 0; n < ERTS_TIW_INSTANCES; ++n) {
        ErlTimerWheel* tiw = tiwtab + n;
        ret = read_tiw_min(tiw);
        if (ret < 0) {
            TIW_LOCK_ACQUIRE(tiw);
            ret = next_time_internal(tiw);
            TIW_LOCK_RELEASE(tiw);
        }
        if (ret >= 0) {
            dt = do_time_update();
            if (ret <= dt) {
                return 0;
            }
            if ((ret - (Uint32) dt) > (Uint32) ERTS_SHORT_TIME_T_MAX) {
                ret = ERTS_SHORT_TIME_T_MAX;
            }
            if (ret < min) {
                min = ret;
            }
        }
    }
    return min;
}

static ERTS_INLINE void
bump_timer_internal(ErlTimerWheel* tiw, erts_short_time_t dt)
{
    tiw_index_t keep_pos;
    Uint count;
    ErlTimer *p, **prev, *timeout_head, **timeout_tail;
    Uint dtime = (Uint) dt;

    TIW_LOCK_ACQUIRE(tiw);

    /* no need to bump the position if there aren't any timeouts */
    if (tiw->tiw_nto == 0) {
        TIW_LOCK_RELEASE(tiw);
        return;
    }

    /* if do_time > ERTS_TIW_SIZE we want to go around just once */
    count = (Uint)(dtime / ERTS_TIW_SIZE) + 1;
    /* dtime could potentially be a larger type than tix_pos */
    keep_pos = (tiw_index_t) ((dtime + tiw->tiw_pos) % ERTS_TIW_SIZE);
    if (dtime > ERTS_TIW_SIZE)
        dtime = ERTS_TIW_SIZE;

    timeout_head = NULL;
    timeout_tail = &timeout_head;
    while (dtime > 0) {
        /* this is to decrease the counters with the right amount */
        /* when dtime >= ERTS_TIW_SIZE */
        if (tiw->tiw_pos == keep_pos) --count;
        prev = &tiw->tiw[tiw->tiw_pos].head;
        while ((p = *prev) != NULL) {
            ASSERT( p != p->next);
            if (p->count < count) {     /* we have a timeout */
                /* remove min time */
                if (tiw->tiw_min_ptr == p) {
                    tiw->tiw_min_ptr = NULL;
                    reset_tiw_min(tiw);
                }

                /* Remove from list */
                remove_timer(tiw, p);
                *timeout_tail = p;	/* Insert in timeout queue */
                timeout_tail = &p->next;
            }
            else {
                /* no timeout, just decrease counter */
                p->count -= count;
                prev = &p->next;
            }
        }
        tiw->tiw_pos = (tiw->tiw_pos + 1) % ERTS_TIW_SIZE;
        dtime--;
    }
    tiw->tiw_pos = keep_pos;
    if (tiw->tiw_min_ptr) {
        set_tiw_min(tiw, read_tiw_min(tiw) - dt);
    }

    TIW_LOCK_RELEASE(tiw);

    /* Call timedout timers callbacks */
    while (timeout_head) {
        p = timeout_head;
        timeout_head = p->next;
        /* Here comes hairy use of the timer fields!
         * They are reset without having the lock.
         * It is assumed that no code but this will
         * accesses any field until the ->timeout
         * callback is called.
         */
        p->next = NULL;
        p->prev = NULL;
        p->slot = 0;
        (*p->timeout)(p->arg);
    }
}

void erts_bump_timer(erts_short_time_t dt) /* dt is value from do_time */
{
    unsigned i, c;
    for (i = 0, c = (unsigned) ERTS_TIW_INSTANCES; i < c; ++i)
    {
        ErlTimerWheel * tiw = (tiwtab + i);
        if ( tiw->tiw_nto )
            bump_timer_internal( tiw, dt );
    }
}

Uint
erts_timer_wheel_memory_size(void)
{
    return (Uint) ERTS_TIW_SIZE * sizeof(ErlTimer*) * ERTS_TIW_INSTANCES;
}

/* this routine links the time cells into a free list at the start
   and sets the time queue as empty */
void
erts_init_time(void)
{
    int i, itime, n;

    /* system dependent init; must be done before do_time_init()
       if timer thread is enabled */
    itime = erts_init_time_sup();
#ifdef TIW_ITIME_IS_CONSTANT
    if (itime != TIW_ITIME) {
        erl_exit(ERTS_ABORT_EXIT, "timer resolution mismatch %d != %d", itime, TIW_ITIME);
    }
#else
    tiw_itime = itime;
#endif

    tiwtab = (ErlTimerWheel*)
        erts_alloc(ERTS_ALC_T_TIMER_WHEEL, ERTS_TIW_INSTANCES * sizeof(*tiwtab));
    for (n = 0; n < ERTS_TIW_INSTANCES; ++n) {
        ErlTimerWheel* tiw = tiwtab+n;
        TIW_LOCK_INIT(tiw);
        tiw->tiw = (ErlTimerHead*) erts_alloc(ERTS_ALC_T_TIMER_WHEEL,
                                  ERTS_TIW_SIZE * sizeof(tiw->tiw[0]));
        for(i = 0; i < ERTS_TIW_SIZE; i++)
            tiw->tiw[i].head = tiw->tiw[i].tail = NULL;
        tiw->tiw_pos = tiw->tiw_nto = 0;
        tiw->tiw_min_ptr = NULL;
        reset_tiw_min(tiw);
    }
    do_time_init();
}

/*
** Insert a process into the time queue, with a timeout 't'
*/
static void
insert_timer(ErlTimerWheel* tiw, ErlTimer* p, Uint t)
{
    Uint tm;
    Uint64 ticks;

    /* The current slot (tiw_pos) in timing wheel is the next slot to be
     * be processed. Hence no extra time tick is needed.
     *
     * (x + y - 1)/y is precisely the "number of bins" formula.
     */
    ticks = (t + (TIW_ITIME - 1)) / TIW_ITIME;

    /*
     * Ticks must be a Uint64, or the addition may overflow here,
     * resulting in an incorrect value for p->count below.
     */
    ticks += do_time_update(); /* Add backlog of unprocessed time */

    /* calculate slot */
    tm = (ticks + tiw->tiw_pos) % ERTS_TIW_SIZE;
    p->slot = (Uint) tm;
    p->count = (Uint) (ticks / ERTS_TIW_SIZE);

    if (tiw->tiw[tm].head == NULL || p->count <= tiw->tiw[tm].head->count) {
        /* insert at head of list at slot */
        p->prev = NULL;
        p->next = tiw->tiw[tm].head;
        if (p->next) {
            p->next->prev = p;
        } else {
            tiw->tiw[tm].tail = p;
        }
        tiw->tiw[tm].head = p;
    } else {
        /* insert at tail of list at slot */
        p->next = NULL;
        p->prev = tiw->tiw[tm].tail;
        p->prev->next = p;
        tiw->tiw[tm].tail = p;
    }

    /* insert min time */
    if ((tiw->tiw_nto == 0) || ((tiw->tiw_min_ptr != NULL) && (ticks < read_tiw_min(tiw)))) {
        tiw->tiw_min_ptr = p;
        set_tiw_min(tiw, ticks);
    }
    if ((tiw->tiw_min_ptr == p) && (ticks > read_tiw_min(tiw))) {
        /* some other timer might be 'min' now */
        tiw->tiw_min_ptr = NULL;
        reset_tiw_min(tiw);
    }

    tiw->tiw_nto++;
}

void
erts_set_timer(ErlTimer* p, ErlTimeoutProc timeout, ErlCancelProc cancel,
              void* arg, Uint t)
{
    int n;

    erts_deliver_time();
    n = erts_get_scheduler_id();
    TIW_LOCK_ACQUIRE(tiwtab+n);
    if (p->active) { /* XXX assert ? */
        TIW_LOCK_RELEASE(tiwtab+n);
        return;
    }
    p->timeout = timeout;
    p->cancel = cancel;
    p->arg = arg;
    p->active = 1;
    p->instance = n;
    insert_timer(tiwtab+n, p, t);
    TIW_LOCK_RELEASE(tiwtab+n);
#if defined(ERTS_SMP)
    if (t <= (Uint) ERTS_SHORT_TIME_T_MAX)
        erts_sys_schedule_interrupt_timed(1, (erts_short_time_t) t);
#endif
}

void
erts_cancel_timer(ErlTimer* p)
{
    ErlTimerWheel* tiw = tiwtab + p->instance;

    TIW_LOCK_ACQUIRE(tiw);
    if (!p->active) { /* allow repeated cancel (drivers) */
        TIW_LOCK_RELEASE(tiw);
        return;
    }

    /* is it the 'min' timer, remove min */
    if (p == tiw->tiw_min_ptr) {
        tiw->tiw_min_ptr = NULL;
        reset_tiw_min(tiw);
    }

    remove_timer(tiw, p);
    p->slot = p->count = 0;

    TIW_LOCK_RELEASE(tiw);
    if (p->cancel != NULL) {
        (*p->cancel)(p->arg);
    }
}

/*
  Returns the amount of time left in ms until the timer 'p' is triggered.
  0 is returned if 'p' isn't active.
  0 is returned also if the timer is overdue (i.e., would have triggered
  immediately if it hadn't been cancelled).
*/
Uint
erts_time_left(ErlTimer *p)
{
    Uint left;
    erts_short_time_t dt;
    ErlTimerWheel* tiw;

    tiw = tiwtab + p->instance;
    TIW_LOCK_ACQUIRE(tiw);

    if (!p->active) {
        TIW_LOCK_RELEASE(tiw);
        return 0;
    }

    if (p->slot < tiw->tiw_pos)
        left = (p->count + 1) * ERTS_TIW_SIZE + p->slot - tiw->tiw_pos;
    else
        left = p->count * ERTS_TIW_SIZE + p->slot - tiw->tiw_pos;
    dt = do_time_read();
    if (left < dt)
        left = 0;
    else
        left -= dt;

    TIW_LOCK_RELEASE(tiw);

    return (Uint) left * TIW_ITIME;
}

#ifdef DEBUG

static void
p_slqq_internal (ErlTimerWheel* tiw)
{
    int i;
    ErlTimer* p;

    TIW_LOCK_ACQUIRE(tiw);

    /* print the whole wheel, starting at the current position */
    erts_printf("\ntiw_pos = %d tiw_nto %d\n", tiw->tiw_pos, tiw->tiw_nto);
    i = tiw->tiw_pos;
    if (tiw->tiw[i].head != NULL) {
        erts_printf("%d:\n", i);
        for(p = tiw->tiw[i].head; p != NULL; p = p->next) {
            erts_printf(" (count %d, slot %d)\n",
                        p->count, p->slot);
        }
    }
    for(i = (i+1)%ERTS_TIW_SIZE; i != tiw->tiw_pos; i = (i+1)%ERTS_TIW_SIZE) {
        if (tiw->tiw[i].head != NULL) {
            erts_printf("%d:\n", i);
            for(p = tiw->tiw[i].head; p != NULL; p = p->next) {
                erts_printf(" (count %d, slot %d)\n",
                            p->count, p->slot);
            }
        }
    }

    TIW_LOCK_RELEASE(tiw);
}

void erts_p_slpq(void)
{
    int n;

    for (n = 0; n < ERTS_TIW_INSTANCES; ++n) {
        p_slpq_internal(tiwtab + n);
    }
}

#endif /* DEBUG */
