/*
 * %CopyrightBegin%
 *
 * Copyright Ericsson AB 2006-2011. All Rights Reserved.
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

#ifndef ERL_TIME_H__
#define ERL_TIME_H__

#define ERTS_SHORT_TIME_T_MAX ERTS_AINT32_T_MAX
#define ERTS_SHORT_TIME_T_MIN ERTS_AINT32_T_MIN
typedef erts_aint32_t erts_short_time_t;

/*
 * ERTS_TIW_SIZE absolutely MUST be a power of 2!
 *
 * This is a tradeoff - the larger the wheel, the fewer entries there are
 * likely to be in any given slot, so list traversal in the slot is shorter.
 * OTOH, traversing empty slots wastes time, when bumping timers.
 * All of the timer traversal is done holding a lock on the wheel, and a
 * couple of operations traverse all of the timers, so optimizing traversal
 * is desirable to reduce the time the lock is held.
 * The timers in a wheel will take up the same amount of space regardless of
 * the slot they're in, so the only difference is how long a list has to be
 * traversed on insertion vs how many empty slots have to be traverersed on
 * bump.
 * It MAY be desirable to make this size tunable for applications that know
 * they use lots of or very few timers.
 */
#ifdef SMALL_MEMORY
#define ERTS_TIW_SIZE   (1 << 13)   /*  8192 */
#else
#define ERTS_TIW_SIZE   (1 << 16)   /* 65536 */
#endif
#ifdef  ERTS_SMP
#define ERTS_MULTI_TIW  1
#else
#define ERTS_MULTI_TIW  0
#endif

/*
 * Timer entry:
 */
typedef void * ErlTimerProcArg;
typedef void (* ErlTimeoutProc)(ErlTimerProcArg);
typedef void (* ErlCancelProc)(ErlTimerProcArg);
typedef struct erl_timer_wheel_ ErlTimerWheel;
typedef struct erl_timer_ ErlTimer;

/*
    tiw_index_t is an unsigned integer type that
        - can hold at least ERTS_TIW_SIZE
        - can be read in a single operation by the CPU
    It IS NOT to be used in general atomic operations, but may be checked
    before acquiring a lock.

    This was originally a Uint in all cases, although that type is larger than
    the native CPU word size on certain platform configurations (e.g. 64 bits
    on most 32-bit platforms). ERTS_TIW_SIZE fits comfortably within 32 bits,
    so prefer the native 'unsigned' type, which should be an atomic read on
    pretty much anything unless it CANNOT hold the value, which would probably
    only happen on some crufty old 16-bit harware that we REALLY don't care
    about.
*/
#if (ERTS_TIW_SIZE < UINT_MAX)
typedef unsigned    tiw_index_t;
#define INVALID_TIW_INDEX_T UINT_MAX
#else
typedef Uint        tiw_index_t;
#define INVALID_TIW_INDEX_T ERTS_UINT_MAX
#endif

/*
 *  values are only relevant if active != 0
 *
 *  ordered to maintain alignment on sizeof(pointer) as long as we can
 */
struct erl_timer_
{
    ErlTimer *          next;       /* next entry tiw slot or chain */
    ErlTimer *          prev;       /* prev entry tiw slot or chain */
#if ERTS_MULTI_TIW
    ErlTimerWheel *     wheel;      /* timer wheel instance */
#endif
    ErlTimeoutProc      timeout;    /* called when timeout (can't be NULL) */
    ErlCancelProc       cancel;     /* called when cancel (may be NULL) */
    ErlTimerProcArg     arg;        /* argument to timeout/cancel procs */
    Uint                count;      /* number of loops remaining */
    tiw_index_t         slot;       /* slot in timer wheel */
    int        volatile active;     /* 1=activated, 0=deactivated */
};

ERTS_GLB_INLINE ErlTimer * erts_init_timer(ErlTimer *);

#if ERTS_GLB_INLINE_INCL_FUNC_DEF

ERTS_GLB_INLINE ErlTimer * erts_init_timer(ErlTimer * timer)
{
    /* compiler SHOULD optimize this to something like memset */
    timer->next     = NULL;
    timer->prev     = NULL;
#if ERTS_MULTI_TIW
    timer->wheel    = NULL;
#endif
    timer->timeout  = NULL;
    timer->cancel   = NULL;
    timer->arg      = NULL;
    timer->count    = 0;
    timer->active   = 0;
    timer->slot     = INVALID_TIW_INDEX_T;
    return timer;
}

#endif /* #if ERTS_GLB_INLINE_INCL_FUNC_DEF */

#ifdef ERTS_SMP
/*
 * Process and port timer
 */
typedef union ErtsSmpPTimer_ ErtsSmpPTimer;
union ErtsSmpPTimer_ {
    struct {
        ErlTimer tm;
        Eterm id;
        ErlTimeoutProc timeout_func;
        ErtsSmpPTimer ** timer_ref;
        Uint32 flags;
    } timer;
    ErtsSmpPTimer * next;
};

void erts_create_smp_ptimer(ErtsSmpPTimer **timer_ref,
                            Eterm id,
                            ErlTimeoutProc timeout_func,
                            Uint timeout);
void erts_cancel_smp_ptimer(ErtsSmpPTimer *ptimer);
#endif

/* timer-wheel api */

void erts_set_timer(ErlTimer *,
        ErlTimeoutProc, ErlCancelProc, ErlTimerProcArg, Uint);
void erts_cancel_timer(ErlTimer *);
void erts_bump_timer(erts_short_time_t);
Uint erts_time_left(ErlTimer *);

Uint erts_timer_wheel_memory_size(void);
void erts_init_time(void);
#ifdef DEBUG
void erts_p_slpq(void);
#endif

#ifndef HIDE_ERTS_DO_TIME
/* set at clock interrupt */
extern erts_smp_atomic32_t  erts_do_time[1];
ERTS_GLB_INLINE erts_short_time_t erts_do_time_read_and_reset(void);

#if ERTS_GLB_INLINE_INCL_FUNC_DEF
ERTS_GLB_INLINE erts_short_time_t erts_do_time_read_and_reset(void)
{
    erts_short_time_t time = erts_smp_atomic32_xchg_acqb(erts_do_time, 0);
    if (time < 0)
        erl_exit(ERTS_ABORT_EXIT, "Internal time management error\n");
    return time;
}
#endif  /* ERTS_GLB_INLINE_INCL_FUNC_DEF */
#endif  /* HIDE_ERTS_DO_TIME */

/* time_sup */

#if (defined(HAVE_GETHRVTIME) || defined(HAVE_CLOCK_GETTIME))
#  ifndef HAVE_ERTS_NOW_CPU
#    define HAVE_ERTS_NOW_CPU
#    ifdef HAVE_GETHRVTIME
#      define erts_start_now_cpu() sys_start_hrvtime()
#      define erts_stop_now_cpu()  sys_stop_hrvtime()
#    endif
#  endif
void erts_get_now_cpu(Uint* megasec, Uint* sec, Uint* microsec);
#endif

typedef UWord erts_approx_time_t;
erts_approx_time_t erts_get_approx_time(void);

void erts_get_timeval(SysTimeval *tv);
erts_time_t erts_get_time(void);

ERTS_GLB_INLINE int erts_cmp_timeval(SysTimeval *t1p, SysTimeval *t2p);

#if ERTS_GLB_INLINE_INCL_FUNC_DEF

ERTS_GLB_INLINE int erts_cmp_timeval(SysTimeval *t1p, SysTimeval *t2p)
{
    if (t1p->tv_sec == t2p->tv_sec) {
        if (t1p->tv_usec < t2p->tv_usec)
            return -1;
        else if (t1p->tv_usec > t2p->tv_usec)
            return 1;
        return 0;
    }
    return t1p->tv_sec < t2p->tv_sec ? -1 : 1;
}

#endif /* #if ERTS_GLB_INLINE_INCL_FUNC_DEF */

#endif /* ERL_TIME_H__ */
