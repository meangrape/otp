/*
 * %CopyrightBegin%
 *
 * Copyright Basho Technologies, Inc 2015. All Rights Reserved.
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
#include "time_internal.h"

#ifdef ERTS_ENABLE_LOCK_CHECK
#define ASSERT_NO_LOCKED_LOCKS  erts_lc_check_exact(NULL, 0)
#else
#define ASSERT_NO_LOCKED_LOCKS
#endif

/*
 * Set these non-zero for more complete timer field cleanup, at the expense
 * of writing three pointer values for each one in each timer at removal.
 */
#define SCRUB_TIMER_LINKS   1
#define SCRUB_TIMER_FUNCS   1

#ifdef SMALL_MEMORY
#define TIW_MAX_TIMERS  (ERTS_TIW_SIZE << 8)
#else
#define TIW_MAX_TIMERS  (ERTS_TIW_SIZE << 12)
#endif

/*
    tiw_count_t IS NOT to be confused with the 'count' field in ErlTimer!

    Semantics are as decribed for tiw_index_t in erl_time.h, but with a
    diferent scale.
*/
#if (ERTS_MAX_TIMERS < UINT_MAX)
typedef unsigned    tiw_count_t;
#define TIW_COUNT_MAX   UINT_MAX
#else
typedef Uint        tiw_count_t;
#define TIW_COUNT_MAX   ERTS_UINT_MAX
#endif

/*
    BEGIN wheel->sync protected variables

    The individual timer cells in each wheel are protected by the same mutex.
*/

typedef struct
{
    ErlTimer *  head;
    ErlTimer *  tail;
}
    TimerWheelEntry;

/*
    Some timer wheel variables are read unlocked in some scenarios, often
    to determine if we need to do further processing while holding the lock.

    These accesses don't really require the atomic operation functions, on the
    assumption that SMP builds are on CPUs with at least 32-bit native words
    where reads are atomic anyway, so making them volatile should be all we
    need to ensure we're not working with stale data.
*/
#ifdef ERTS_SMP
#define SMP_ACCESS  volatile
#else
#define SMP_ACCESS
#endif

/* typedef'd in erl_time.h as ErlTimerWheel */
struct erl_timer_wheel_
{
#ifdef ERTS_SMP
    erts_smp_mtx_t          sync[1];
#endif
#if ERTS_MULTI_TIW
    ErlTimerWheel *         next;   /* all wheels are linked in a ring */
    int                     id;
#endif
    /* these can be read without holding the lock, but lock to update */
    tiw_count_t SMP_ACCESS  to_cnt;     /* how many TOs traversal will find */
    int         SMP_ACCESS  min_set;    /* true if min_to/min_ptr are set */
    /* everything below here should only be accessed holding the lock */
    tiw_index_t             to_cur;
    u_short_time_t          min_to;
    ErlTimer *              min_ptr;
    TimerWheelEntry         timers[ERTS_TIW_SIZE];
};

static ERTS_INLINE void tiw_lock_init(ErlTimerWheel * wheel)
{
#ifdef ERTS_SMP
    erts_smp_mtx_init(wheel->sync, "timer_wheel");
#endif
}

static ERTS_INLINE void tiw_lock_acquire(ErlTimerWheel * wheel)
{
#ifdef ERTS_SMP
    erts_smp_mtx_lock(wheel->sync);
#endif
}

static ERTS_INLINE void tiw_lock_release(ErlTimerWheel * wheel)
{
#ifdef ERTS_SMP
    erts_smp_mtx_unlock(wheel->sync);
#endif
}

/*
    END wheel->sync protected variables
*/

/*
    once initialized in erts_init_time(), these are constant
    use distinct names for single and multiple to break code that should
    break rather than finding it the hard way ...
*/
#if ERTS_MULTI_TIW
static  ErlTimerWheel *     timer_wheels  = NULL;
static  unsigned            tiw_instances = 0;
#else
static  ErlTimerWheel *     timer_wheel = NULL;
#endif

/*
    On a 64-bit platform a timer wheel uses about a MB of memory.
    Dirty schedulers always return a scheduler id of 0, while non-dirty
    schedulers always have a non-zero id. If dirty schedulers are enabled,
    one extra TW is allocated for them, though it's unclear whether it'll
    ever be used. The TW selected by scheduler id SHOULD be calculable by
    subtracting the offset of 1 from the scheduler id (or not, if there are
    dirty schedulers), but for safety it's forced to wrap in case there are
    any oddball scenarios I haven't found.
*/
#if ERTS_MULTI_TIW
#ifdef  ERTS_DIRTY_SCHEDULERS
#define TARGET_TIW_COUNT    (erts_no_schedulers + 1)
#else   /* ! ERTS_DIRTY_SCHEDULERS */
#define TARGET_TIW_COUNT    erts_no_schedulers
#endif  /* ERTS_DIRTY_SCHEDULERS */
#else   /* ! ERTS_MULTI_TIW */
#define TARGET_TIW_COUNT    1
#endif  /* ERTS_MULTI_TIW */

/*
 * Timer wheel accessors
 */
#if ERTS_MULTI_TIW
#define timer_wheel_id(W)       (W)->id

static ERTS_INLINE ErlTimerWheel * esd_timer_wheel(ErtsSchedulerData * esd)
{
    ASSERT(esd != NULL);
    return (timer_wheels + (esd->no % tiw_instances));
}
static ERTS_INLINE ErlTimerWheel * sched_timer_wheel(void)
{
    ASSERT(tiw_instances == TARGET_TIW_COUNT);
    return (timer_wheels + (erts_get_scheduler_id() % tiw_instances));
}
static ERTS_INLINE ErlTimerWheel * timer_timer_wheel(ErlTimer * timer)
{
    ASSERT(timer != NULL);
    ErlTimerWheel * const wheel = timer->wheel;
    ASSERT(wheel != NULL);
    return wheel;
}
#else   /* ! ERTS_MULTI_TIW */
#define timer_wheel_id(W)       0
#define esd_timer_wheel(ESD)    timer_wheel
#define sched_timer_wheel()     timer_wheel
#define timer_timer_wheel(T)    timer_wheel
#endif  /* ERTS_MULTI_TIW */

/* Actual interval time chosen by sys_init_time() */
#if SYS_CLOCK_RESOLUTION == 1
#  define TIW_ITIME 1
#  define TIW_ITIME_IS_CONSTANT
#else
static int tiw_itime; /* Constant after init */
#  define TIW_ITIME tiw_itime
#endif

/*
 *  Current ticks handling
 */

erts_smp_atomic32_t do_time;    /* set at clock interrupt */

#define do_time_init()      erts_smp_atomic32_init_nob(& do_time, 0)
#define READ_DO_TIME        erts_smp_atomic32_read_acqb(& do_time)
#define s_do_time_read()    ((s_short_time_t) READ_DO_TIME)
#define s_do_time_update()  ((s_short_time_t) READ_DO_TIME)
#define u_do_time_read()    ((u_short_time_t) READ_DO_TIME)
#define u_do_time_update()  ((u_short_time_t) READ_DO_TIME)

/*
 * Internal helper functions
 */

static ERTS_INLINE void clear_wheel_min_timer(ErlTimerWheel * wheel)
{
    wheel->min_set  = 0;
    wheel->min_ptr  = NULL;
    wheel->min_to   = INVALID_U_SHORT_TIME;
}

static ERTS_INLINE void set_wheel_min_timer(
    ErlTimerWheel * wheel, ErlTimer * timer, u_short_time_t timeout)
{
    wheel->min_ptr  = timer;
    wheel->min_to   = timeout;
    wheel->min_set  = 1;
}

/*
 *  Used by erts_bump_timer() and erts_cancel_timer()
 *  Returns the number of remaining timers in the wheel.
 *
 *  PRE: wheel->sync is held by caller
 */
static tiw_count_t unlink_timer(ErlTimerWheel * wheel, ErlTimer * timer)
{
    ASSERT(wheel == timer_timer_wheel(timer));

    /* Make sure cancel callback isn't called */
    timer->active = 0;

    /* if it's the 'min' timer, remove it */
    if (wheel->min_set && wheel->min_ptr == timer)
        clear_wheel_min_timer(wheel);

    /* first */
    if (timer->prev == NULL)
    {
        ASSERT(timer->slot < ERTS_TIW_SIZE);
        ASSERT(wheel->timers[timer->slot].head == timer);

        wheel->timers[timer->slot].head = timer->next;
        if (timer->next != NULL)
            timer->next->prev = NULL;
    }
    else
        timer->prev->next = timer->next;

    /* last */
    if (timer->next == NULL)
    {
        ASSERT(timer->slot < ERTS_TIW_SIZE);
        ASSERT(wheel->timers[timer->slot].tail == timer);

        wheel->timers[timer->slot].tail = timer->prev;
        if (timer->prev != NULL)
            timer->prev->next = NULL;
    }
    else
        timer->next->prev = timer->prev;

    /*
        DO NOT clear function pointers or args!
        Callbacks are invoked AFTER removal from the wheel.
    */
#if SCRUB_TIMER_LINKS
    timer->next   = NULL;
    timer->prev   = NULL;
#if ERTS_MULTI_TIW
    timer->wheel  = NULL;
#endif
#endif
    timer->count  = 0;
    timer->slot   = INVALID_TIW_INDEX_T;

    DBG_FMT("wheel[%u]->to_cnt = %u",
        timer_wheel_id(wheel), (wheel->to_cnt - 1));

    return --(wheel->to_cnt);
}

/*
 * Private API exposed in time_internal.h
 */

erts_short_time_t erts_next_time(void)
{
#if ERTS_MULTI_TIW
    ErlTimerWheel * const swheel = sched_timer_wheel();
    ErlTimerWheel * wheel;
    unsigned  const wc = (tiw_instances + 1);
    unsigned  wx;
#define TIW_ITERATE for (wx = wc, wheel = swheel; --wx; wheel = wheel->next)
#else
    ErlTimerWheel * const wheel = timer_wheel;
#define TIW_ITERATE
#endif
    u_short_time_t  min_to;

    /* first see if anything's due right now */
    TIW_ITERATE
    if (wheel->min_set)
    {
        int now;
        tiw_lock_acquire(wheel);
        now = (wheel->min_set && wheel->min_to <= u_do_time_update());
        tiw_lock_release(wheel);
        if (now)
        {
            DBG_FMT("wheel[%u] timeout now", timer_wheel_id(wheel));
            return 0;
        }
    }

    /* now see if anything's due at all - may have been added during above */
    min_to  = INVALID_U_SHORT_TIME;
    TIW_ITERATE
    if (wheel->to_cnt)
    {
        u_short_time_t  next_to;

        tiw_lock_acquire(wheel);
        if (wheel->min_set)
            next_to = wheel->min_to;
        else
        {
            TimerWheelEntry *   const timers  = wheel->timers;
            tiw_index_t         const to_cnt  = wheel->to_cnt;
            tiw_index_t         depth, found, to_cur;
            next_to = INVALID_U_SHORT_TIME;
            /*
                there should be at least one timeout in the wheel when we
                get here, and when done the lowest timeout will be in
                'next_to'

                in all cases, stop if we've gone through the whole wheel,
                which should be a redundant check - probably should abort
                if we get all the way through without finding 'to_cnt'
                timeouts
            */
            for (depth = 0, found = 0, to_cur = wheel->to_cur;
                found < to_cnt && depth < ERTS_TIW_SIZE; ++depth)
            {
                ErlTimer  * timer;
                for (timer = timers[to_cur].head;
                    timer != NULL; timer = timer->next)
                {
                    ++found;
                    if (timer->count)
                    {
                        /* keep shortest time in 'next_to' */
                        const u_short_time_t cand_to
                            = (depth + (timer->count * ERTS_TIW_SIZE));
                        if (cand_to < next_to)
                            set_wheel_min_timer(wheel, timer, (next_to = cand_to));
                    }
                    else    /* count is zero, found next timeout */
                    {
                        set_wheel_min_timer(wheel, timer, (next_to = depth));
                        break;
                    }
                }
                if (++to_cur >= ERTS_TIW_SIZE)
                    to_cur = 0;
            }
        }
        tiw_lock_release(wheel);
        if (next_to != INVALID_U_SHORT_TIME)
        {
            const u_short_time_t cur_tm = u_do_time_update();
            if (next_to <= cur_tm)
                return 0;
            if ((next_to - cur_tm) > ((u_short_time_t) ERTS_SHORT_TIME_T_MAX))
                next_to = (u_short_time_t) ERTS_SHORT_TIME_T_MAX;
            if (next_to < min_to)
                min_to = next_to;
        }
    }
    DBG_FMT("next timeout %u", min_to);
    return (erts_short_time_t) min_to;
#undef  TIW_ITERATE
}

/*
 *  Public API exposed in erl_time.h
 */

/*
    called ONLY by erts_bump_timer

    PRE: wheel->sync is NOT held by caller
*/
static void bump_timer_wheel(ErlTimerWheel * wheel, erts_short_time_t dt)
{
    ErlTimer *  timer;
    ErlTimer ** prev;
    ErlTimer *  timeout_head;
    ErlTimer ** timeout_tail;
    Uint        count, dtime, dt_in;
    tiw_index_t keep_pos;

    /* no need to bump the position if there aren't any timeouts */
    if (wheel->to_cnt == 0)
        return;

    /* if do_time > ERTS_TIW_SIZE we want to go around just once */
    dt_in = (Uint) dt;
    dtime = (dt_in > ERTS_TIW_SIZE) ? ERTS_TIW_SIZE : dt_in;
    count = (dt_in / ERTS_TIW_SIZE) + 1;
    timeout_head = NULL;
    timeout_tail = & timeout_head;

    tiw_lock_acquire(wheel);

    /* dt_in could potentially be a larger type than tiw_index_t */
    keep_pos = (tiw_index_t) ((dt_in + wheel->to_cur) % ERTS_TIW_SIZE);

    while (dtime)
    {
        /* this is to decrease the counters with the right amount */
        /* when dtime >= ERTS_TIW_SIZE */
        if (wheel->to_cur == keep_pos)
            --count;
        prev = & wheel->timers[wheel->to_cur].head;
        while ((timer = *prev) != NULL)
        {
            ASSERT( timer != timer->next);
            if (timer->count < count)   /* we have a timeout */
            {
                DBG_FMT("wheel[%u]->slot[%u] timeout %p",
                    timer_wheel_id(wheel), wheel->to_cur, timer);

                /* Remove from list */
                unlink_timer(wheel, timer);

                *timeout_tail = timer;	/* Insert in timeout queue */
                timeout_tail = &timer->next;
            }
            else {
                /* no timeout, just decrease counter */
                timer->count -= count;
                prev = &timer->next;
            }
        }
        wheel->to_cur = (wheel->to_cur + 1) % ERTS_TIW_SIZE;
        --dtime;
    }
    wheel->to_cur = keep_pos;
    if (wheel->min_set)
        wheel->min_to -= (u_short_time_t) dt;

    tiw_lock_release(wheel);

    /* Call timedout timers callbacks */
    while (timeout_head) {
        timer = timeout_head;
        timeout_head = timer->next;
        /* Here comes hairy use of the timer fields!
         * They are reset without having the lock.
         * It is assumed that no code but this will
         * accesses any field until the ->timeout
         * callback is called.
         */
        timer->next = NULL;
        timer->prev = NULL;
        DBG_FMT("invoke timer timeout %p", timer);
        timer->timeout(timer->arg);
    }
}

/*
 *  If 'esd' is not NULL, process only the wheel associated with it.
 *  If 'esd' is NULL, process all wheels, starting with the current scheduler's.
 *
 *  'dt' is the value of 'do_time'
 *
 *  This implementation relies a LOT on compiler optimizations!
 */
void erts_bump_timer_s(ErtsSchedulerData * esd, erts_short_time_t dt)
{
    ErlTimerWheel * wheel = sched_timer_wheel();
    /* don't care about alignment, dt_in shouldn't actually be allocated */
    const u_short_time_t  dt_in = (u_short_time_t) dt;
    /* we only want to go around once, at most */
    const Uint  slots_in = (dt_in > ERTS_TIW_SIZE) ? ERTS_TIW_SIZE : dt_in;
    const Uint  count_in = (dt_in / ERTS_TIW_SIZE) + 1;
#if ERTS_MULTI_TIW
    unsigned    wc, wx;
    if (esd != NULL)
    {
        wheel = esd_timer_wheel(esd);
        wc = 1;
    }
    else
    {
        wheel = sched_timer_wheel();
        wc = tiw_instances;
    }
#define TIW_ITERATE for (wx = 0; wx < wc; ++wx, wheel = wheel->next)
#else
#define TIW_ITERATE
#endif
    /* no need to bump the position if there aren't any timeouts */
    TIW_ITERATE
    if (wheel->to_cnt)
    {
        /*
            TODO:
                This traverses every timer in every slot - it should reset
                the min_xxx fields in the process.
        */
        TimerWheelEntry * const timers = wheel->timers;
        ErlTimer *  timeout_head = NULL;
        ErlTimer ** timeout_tail = & timeout_head;
        Uint        count = count_in;
        Uint        slots = slots_in;
        tiw_index_t final_pos, cur_pos;

        tiw_lock_acquire(wheel);
        /*
            dt_in could potentially be a larger type than tiw_index_t, but
            the result has to fit because of the range limit
        */
        final_pos = (tiw_index_t) ((dt_in + wheel->to_cur) % ERTS_TIW_SIZE);

        for (cur_pos = wheel->to_cur; slots; --slots)
        {
            ErlTimer *  timer;
            ErlTimer ** prev;

            /*
                decrease counters by the right amount when we cross the
                eventual position, which will happen exactly once
            */
            if (cur_pos == final_pos)
                --count;

            prev = & timers[cur_pos].head;
            while ((timer = *prev) != NULL)
            {
                /* has to be a refugee from some hopefully dead bug */
                ASSERT(timer != timer->next);

                if (timer->count < count)   /* we have a timeout */
                {
                    DBG_FMT("wheel[%u]->slot[%u] timeout %p",
                        timer_wheel_id(wheel), cur_pos, timer);

                    *timeout_tail = timer;  /* Insert in timeout queue */
                    timeout_tail = & timer->next;

                    /* Remove from slot, which may empty the wheel */
                    if (unlink_timer(wheel, timer) == 0)
                        break;
                }
                else
                {
                    /* no timeout, just decrease counter */
                    timer->count -= count;
                    prev = & timer->next;
                }
            }
            if (++cur_pos >= ERTS_TIW_SIZE)
                cur_pos = 0;
        }
#if ! SCRUB_TIMER_LINKS
        /* make sure the last timer in the list ends traversal! */
        *timeout_tail = NULL;
#endif
        wheel->to_cur = final_pos;
        if (wheel->min_set)
            wheel->min_to -= dt_in;

        tiw_lock_release(wheel);

        /* Call timed-out timers' callbacks */
        while (timeout_head)
        {
            ErlTimer * const timer = timeout_head;
            /*
                The timer is no longer in a wheel, so its fields that
                pertain to its position in a wheel and its timeout are
                invalid. Its 'next' pointer is now used for the transient
                timeout list only.
            */
            timeout_head = timer->next;
            DBG_FMT("invoke timer timeout %p", timer);
            timer->timeout(timer->arg);
#if SCRUB_TIMER_LINKS
            timer->next     = NULL;
#endif
#if SCRUB_TIMER_FUNCS
            timer->timeout  = NULL;
            timer->cancel   = NULL;
            timer->arg      = NULL;
#endif
        }
    }
    /* nothing after here, may be outside the processing loop! */
#undef  TIW_ITERATE
}

void erts_bump_timer(erts_short_time_t dt) /* dt is value from do_time */
{
#if ERTS_MULTI_TIW
    ErlTimerWheel * wheel = sched_timer_wheel();
    unsigned  const wc = tiw_instances;
    unsigned  wx;
    for (wx = 0; wx < wc; ++wx, wheel = wheel->next)
#else
    ErlTimerWheel * const wheel = timer_wheel;
#endif
    bump_timer_wheel(wheel, dt);
}

void erts_set_timer(
    ErlTimer *      timer,
    ErlTimeoutProc  on_timeout,
    ErlCancelProc   on_cancel,
    ErlTimerProcArg cb_arg,
    Uint            timeout )
{
    ErlTimerWheel *     wheel;
    TimerWheelEntry *   entry;
    Uint64              ticks;  /* 64 bits to protect from possible overflow */
    Uint                count;

    if (timer->active)  /* XXX assert ? */
        return;

    erts_deliver_time();

#if ERTS_MULTI_TIW
    timer->wheel = wheel = sched_timer_wheel();
#else
    wheel = timer_wheel;
#endif
    timer->timeout = on_timeout;
    timer->cancel = on_cancel;
    timer->arg = cb_arg;
    timer->active = 1;

    /*
     * The current slot (to_cur) in the timer wheel is the next slot to be
     * processed. Hence no extra time tick is needed.
     *
     * (x + y - 1)/y is precisely the "number of bins" formula.
     */
    ticks = (timeout + TIW_ITIME - 1) / TIW_ITIME;

    tiw_lock_acquire(wheel);
    /*
        if something is doing a fast read on to_cnt, it will wait on the
        mutex until the insertion is completed to process the wheel
    */
    ++(wheel->to_cnt);

    /* wait until we have the lock for the update, in case it took some time */
    ticks += u_do_time_update();    /* Add backlog of unprocessed time */
    timer->count = count = (Uint) (ticks / ERTS_TIW_SIZE);

    /* calculate the slot, wrapping to eliminate the potential overflow */
    timer->slot = (Uint) ((ticks + wheel->to_cur) % ERTS_TIW_SIZE);
    entry = (wheel->timers + timer->slot);

    /*
     *  Discrete cases to avoid duplicate tests, speed is of the essence.
     *
     *  If 'head' is/isn't NULL, we can assume 'tail' is/isn't also.
     *  Keep the list in any given slot sorted by count and insertion order,
     *  so timeout processing can stop when it hits a greater count.
     */
    if (entry->head == NULL)
    {
        /* slot is empty, inserted timer becomes head and tail */
        DBG_FMT("insert timer %p count %u in empty wheel[%u]->slot[%u]",
            timer, count, timer_wheel_id(wheel), timer->slot);

        timer->prev = NULL;
        timer->next = NULL;
        entry->head = timer;
        entry->tail = timer;
    }
    else if (count < entry->head->count)
    {
        /* slot is not empty, inserted timer becomes tail */
        DBG_FMT("insert timer %p count %u at head of wheel[%u]->slot[%u]",
            timer, count, timer_wheel_id(wheel), timer->slot);

        timer->next = entry->head;
        timer->prev = NULL;
        entry->head->prev = timer;
        entry->head = timer;
    }
    else if (count >= entry->tail->count)
    {
        /* slot is not empty, inserted timer becomes tail */
        DBG_FMT("insert timer %p count %u at tail of wheel[%u]->slot[%u]",
            timer, count, timer_wheel_id(wheel), timer->slot);

        timer->next = NULL;
        timer->prev = entry->tail;
        entry->tail->next = timer;
        entry->tail = timer;
    }
    else    /* slot is not empty, insert timer between head and tail */
    {
        /* find the last timer with a count <= to this one */
        /* 'pos' should never be NULL after the 'tail' check above */
        DBG_FMT("insert timer %p count %u into wheel[%u]->slot[%u]",
            timer, count, timer_wheel_id(wheel), timer->slot);

        ErlTimer *  pos = entry->head;
        while (pos->count <= count)
            pos = pos->next;
        timer->next = pos;
        timer->prev = pos->prev;
        pos->prev->next = timer;
        pos->prev = timer;
    }

    /* insert min time, to_cnt has already been incremented */
    if (wheel->to_cnt == 1 || (wheel->min_set && ticks < wheel->min_to))
        set_wheel_min_timer(wheel, timer, ticks);

    /* some other timer might be 'min' now */
    else if (wheel->min_set && wheel->min_ptr == timer && ticks > wheel->min_to)
        clear_wheel_min_timer(wheel);

    tiw_lock_release(wheel);

#ifdef ERTS_SMP
    if (timeout <= (Uint) ERTS_SHORT_TIME_T_MAX)
        erts_sys_schedule_interrupt_timed(1, (erts_short_time_t) timeout);
#endif
}

void erts_cancel_timer(ErlTimer * timer)
{
    ErlTimerWheel * wheel;

    /* allow repeated cancel (drivers) */
    if (timer->active == 0)
        return;

    wheel = timer_timer_wheel(timer);

    DBG_FMT("wheel[%u]->slot[%u] cancel %p",
        timer_wheel_id(wheel), timer->slot, timer);

    tiw_lock_acquire(wheel);
    unlink_timer(wheel, timer);
    tiw_lock_release(wheel);

    if (timer->cancel != NULL)
    {
        DBG_FMT("invoke timer cancel %p", timer);
        timer->cancel(timer->arg);
#if SCRUB_TIMER_FUNCS
        timer->cancel = NULL;
#endif
    }
#if SCRUB_TIMER_FUNCS
    timer->timeout  = NULL;
    timer->arg      = NULL;
#endif
}

/*
 *  Returns the amount of time left in ms until 'timer' is triggered.
 *  0 is returned if 'timer' isn't active.
 *  0 is returned also if the timer is overdue (i.e., would have triggered
 *      immediately if it hadn't been cancelled).
*/
Uint erts_time_left(ErlTimer * timer)
{
    ErlTimerWheel * wheel;
    Uint            left;
    tiw_index_t     slot, wcur;
    u_short_time_t  dt;

    if (! timer->active)
        return 0;

    wheel = timer_timer_wheel(timer);

    left = timer->count;
    slot = timer->slot;
    wcur = wheel->to_cur;
    if (slot < wcur)
        ++left;

    left *= ERTS_TIW_SIZE;
    left += slot;
    left -= wcur;

    dt = u_do_time_read();
    if (left < dt)
        return 0;

    return (Uint) ((left - dt) * TIW_ITIME);
}

/*
 * Initialization
 */

Uint erts_timer_wheel_memory_size(void)
{
#if ERTS_MULTI_TIW
    return (Uint) (sizeof(ErlTimerWheel)
            * (tiw_instances ? tiw_instances : TARGET_TIW_COUNT));
#else
    return (Uint) sizeof(ErlTimerWheel);
#endif
}

#if ERTS_MULTI_TIW
static void validate_wheel_links(void)
{
    unsigned        const count = tiw_instances;
    unsigned        const lastx = (count - 1);
    ErlTimerWheel * const first = timer_wheels;
    ErlTimerWheel * wheel;
    unsigned        index;
    for (index = 0, wheel = first; index < count; ++index, ++wheel)
    {
        ErlTimerWheel * const target =
            (index < lastx) ? & first[index + 1] : first;
        if (wheel->next != target)
            erl_exit(ERTS_ABORT_EXIT, "\n"
                "timer_wheels[%u] is %p, should be %p\n"
                "wheel size: %u, wheels: %p, count: %u\n",
                index, wheel->next, target, sizeof(*first), first, count);
    }
    DBG_FMT("wheel size: 0x0%x, wheels: %p, count: %u",
        sizeof(ErlTimerWheel), timer_wheels, count);
}
#endif

/*
 *  Allocates and initializes timer wheels, one per scheduler (sort of).
 *
 *  On completion all timer wheels are empty, and the timer API is useable.
 */
void erts_init_time(void)
{
    ErlTimerWheel * wheel;
#if ERTS_MULTI_TIW
    unsigned        wx;
    unsigned  const wc  = TARGET_TIW_COUNT;
    size_t    const sz  = (sizeof(ErlTimerWheel) * wc);
#define TIW_ALLOC_DATA  timer_wheels
#define TIW_ALLOC_SIZE  sz
#else
#define TIW_ALLOC_DATA  timer_wheel
#define TIW_ALLOC_SIZE  sizeof(ErlTimerWheel)
#endif
    DBG_NL();
    DBG_MSG("Initializing timer wheels");
    ASSERT(TIW_ALLOC_DATA == NULL);
    /*
        system dependent init;
        must be done before do_time_init()  if timer thread is enabled
    */
#ifdef TIW_ITIME_IS_CONSTANT
    int itime = erts_init_time_sup();
    if (itime != TIW_ITIME)
        erl_exit(ERTS_ABORT_EXIT,
            "timer resolution mismatch %d != %d", itime, TIW_ITIME);
#else
    tiw_itime = erts_init_time_sup();
#endif
    do_time_init();

    wheel = TIW_ALLOC_DATA = sys_memset(
        erts_alloc(ERTS_ALC_T_TIMER_WHEEL, TIW_ALLOC_SIZE),
        0, TIW_ALLOC_SIZE);
#if ERTS_MULTI_TIW
    tiw_instances = wc;
    for (wx = 0; wx < wc; ++wx, ++wheel)
#endif
    {
        /*
            NULL is not required to be zero, but it's often more complex
            than the preprocessor can handle, so try to structure this so
            the compiler will make inclusion or removal unconditional.
        */
        if ((((char *) NULL) - ((char *) 0)) != 0)
        {
            TimerWheelEntry *   timer = wheel->timers;
            unsigned            tx;
            for (tx = 0; tx < ERTS_TIW_SIZE; ++tx, ++timer)
            {
                timer->head = NULL;
                timer->tail = NULL;
            }
            wheel->min_ptr = NULL;
        }
        wheel->min_to = INVALID_U_SHORT_TIME;
#if ERTS_MULTI_TIW
        wheel->id = wx;
        wheel->next = ((wc - wx) > 1) ? (wheel + 1) : timer_wheels;
#endif
        tiw_lock_init(wheel);
    }
#undef  TIW_ALLOC_DATA
#undef  TIW_ALLOC_SIZE

#if ERTS_MULTI_TIW
    validate_wheel_links();
#endif
}

#ifdef DEBUG

void erts_p_slpq(void)
{
#if ERTS_MULTI_TIW
    ErlTimerWheel * wheel;
    unsigned        wc = (tiw_instances + 1);
#define TIW_ITERATE for (wheel = timer_wheels; --wc; wheel = wheel->next)
#else
    ErlTimerWheel * const wheel = timer_wheel;
#define TIW_ITERATE
#endif

    TIW_ITERATE
    {
        TimerWheelEntry * const timers = wheel->timers;
        tiw_index_t tx, tc;

        tiw_lock_acquire(wheel);

        /* print the whole wheel, starting at the current position */
        for (tc = 0, tx = wheel->to_cur; tc < ERTS_TIW_SIZE; ++tc)
        {
            ErlTimer * timer = timers[tx].head;
            if (timer != NULL)
            {
                erts_printf("%d:\n", tx);
                do
                {
                    erts_printf(
                        " (count %d, slot %d)\n", timer->count, timer->slot);
                    timer = timer->next;
                }
                while (timer != NULL);
            }
            if (++tx >= ERTS_TIW_SIZE)
                tx = 0;
        }

        tiw_lock_release(wheel);
    }
}

#endif /* DEBUG */
