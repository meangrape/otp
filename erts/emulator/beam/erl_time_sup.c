/*
 * %CopyrightBegin%
 *
 * Copyright Basho Technologies, Inc 2015. All Rights Reserved.
 * Copyright Ericsson AB 1999-2012. All Rights Reserved.
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
** Support routines for the timer wheel
**
** This code contains two strategies for dealing with
** date/time changes in the system.
** If the system has some kind of high resolution timer (HAVE_GETHRTIME),
** the high resolution timer is used to correct the time-of-day and the
** timeouts, the base source is the hrtimer, but at certain intervals the
** OS time-of-day is checked and if it is not within certain bounds, the
** delivered time gets slowly adjusted for each call until
** it corresponds to the system time (built-in adjtime...).
** The call gethrtime() is detected by autoconf on Unix, but other
** platforms may define it in erl_*_sys.h and implement
** their own high resolution timer. The high resolution timer
** strategy is (probably) best on all systems where the timer have
** a resolution higher or equal to gettimeofday (or what's implemented
** is sys_gettimeofday()). The actual resolution is the interesting thing,
** not the unit's thats used (i.e. on VxWorks, nanoseconds can be
** retrieved in terms of units, but the actual resolution is the same as
** for the clock ticks).
** If the systems best timer routine is kernel ticks returned from
** sys_times(), and the actual resolution of sys_gettimeofday() is
** better (like most unixes that does not have any realtime extensions),
** another strategy is used. The tolerant gettimeofday() corrects
** the value with respect to uptime (sys_times() return value) and checks
** for correction both when delivering timeticks and delivering nowtime.
** this strategy is slower, but accurate on systems without better timer
** routines. The kernel tick resolution is not enough to implement
** a gethrtime routine. On Linux and other non solaris unix-boxes the second
** strategy is used, on all other platforms we use the first.
**
** The following is expected (from sys.[ch] and erl_*_sys.h):
**
** 64 bit integers. So it is, and so it will be.
**
** sys_init_time(), will return the clock resolution in MS and
** that's about it. More could be added of course
** If the clock-rate is constant (i.e. 1 ms) one can define
** SYS_CLOCK_RESOLUTION (to 1),
** which makes erts_deliver_time/erts_time_remaining a bit faster.
**
** if HAVE_GETHRTIME is defined:
**    sys_gethrtime() will return a SysHrTime (long long) representing
**    nanoseconds, sys_init_hrtime() will do any initialization.
** else
**    a long (64bit) integer type called Sint64 should be defined.
**
** sys_times() will return clock_ticks since start and
**    fill in a SysTimes structure (struct tms). Instead of CLK_TCK,
**    SYS_CLK_TCK is used to determine the resolution of kernel ticks.
**
** sys_gettimeofday() will take a SysTimeval (a struct timeval) as parameter
**    and fill it in as gettimeofday(X,NULL).
**
*/

/*
 * The conditional code based on HAVE_LOCALTIME_R and HAVE_GMTIME_R has
 * been replaced with code that assumes the presence of 'localtime_r' and
 * 'gmtime_r', because really, any competent system should have them.
 */

/* prevent type conflict in global.h */
#define HIDE_ERTS_TTOD_DISABLE  1

#if defined(__APPLE__) && defined(__MACH__)
#include <mach/mach_time.h>
#ifndef HAVE_MACH_ABSOLUTE_TIME
#define HAVE_MACH_ABSOLUTE_TIME 1
#endif
#endif

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include "erts_ttod_config.h"

#include "sys.h"
#include "erl_vm.h"
#include "global.h"
#include "time_internal.h"

#if     (defined(DEBUG) || 1)
#define TTOD_REPORT_IMPL_STATE  1
#else
#define TTOD_REPORT_IMPL_STATE  0
#endif

static  TIME_SUP_ALIGNED_VAR(erts_smp_atomic_t, gtv_ms);
static  TIME_SUP_ALIGNED_VAR(erts_smp_atomic_t, then_us);
static  TIME_SUP_ALIGNED_VAR(erts_smp_atomic_t, approx_secs);
static  TIME_SUP_ALIGNED_VAR(erts_smp_atomic_t, last_delivered_ms);

#define USE_LOCKED_GTOD 0

/*
 * Attempt to lay things out to maintain the best alignment as far into the
 * structure as possible.
 *
 * SysTimes SHOULD contain 4 clock_t values, which SHOULD be native integer
 * types, so no matter what it SHOULD take up some multiple of 64 bits.
 * s_millisecs_t is exactly 64 bits, so we're still aligned.
 * With a little luck, SysTimeval will be 2 integers of the same size, but
 * this is where alignment could go out the window.
 * Beyond here, we really don't know ...
 */
typedef struct
{
    s_millisecs_t   init_ms;
    SysTimeval      init_tv[1];
    erts_smp_mtx_t  tod_sync[1];
#ifndef SYS_CLOCK_RESOLUTION
    int             clock_res;
#endif
}
    time_sup_data_t;

static  TIME_SUP_ALIGNED_VAR(time_sup_data_t, ts_data);

#if     USE_LOCKED_GTOD
#define ACQUIRE_TOD_OUTER()     erts_smp_mtx_lock(ts_data->tod_sync)
#define RELEASE_TOD_OUTER()     erts_smp_mtx_unlock(ts_data->tod_sync)
#define ACQUIRE_TOD_INNER()     ((void) 1)
#define RELEASE_TOD_INNER()     ((void) 1)
#else   /* ! USE_LOCKED_GTOD */
#define ACQUIRE_TOD_OUTER()     ((void) 1)
#define RELEASE_TOD_OUTER()     ((void) 1)
#define ACQUIRE_TOD_INNER()     erts_smp_mtx_lock(ts_data->tod_sync)
#define RELEASE_TOD_INNER()     erts_smp_mtx_unlock(ts_data->tod_sync)
#endif  /* USE_LOCKED_GTOD */

typedef SysTimes    times_acct_rec_t;

typedef struct
{
    times_acct_rec_t    last[1];
    erts_smp_mtx_t      sync[1];
}
    times_acct_data_t;

static  TIME_SUP_ALIGNED_VAR(times_acct_data_t, ta_data);

/*
 * Why this? Well, most platforms have a constant clock resolution of 1,
 * we dont want the deliver_time/time_remaining routines to waste
 * time dividing and multiplying by/with a variable that's always one.
 * so the return value of sys_init_time is ignored on those platforms.
 */
#ifndef SYS_CLOCK_RESOLUTION
#define CLOCK_RESOLUTION    ts_data->clock_res
#else
#define CLOCK_RESOLUTION    SYS_CLOCK_RESOLUTION
#endif

static u_microsecs_t gettimeofday_us(void)
{
    SysTimeval  tod;
    sys_gettimeofday(& tod);
    return  u_get_tv_micros(& tod);
}

/*
 * Begin tolerant_timeofday stuff
 *
 * ERTS_TTOD_USE_xxx macros MUST be kept in sync with erts_ttod_config.h.in!
 */
#if !defined(ERTS_TTOD_ENABLED) || (ERTS_TTOD_ENABLED < 0)
#define ERTS_TTOD_ENABLED   0
#endif
#if ERTS_TTOD_ENABLED
/*
 * Values assigned here define the default strategies.
 * Define to '1' to build if supported on the platform, '0' to exclude.
 */
#if (ERTS_TTOD_ENABLED < 2)
#define ERTS_TTOD_USE_HPET  1
#define ERTS_TTOD_USE_HRC   1
#define ERTS_TTOD_USE_HRT   1
#define ERTS_TTOD_USE_MACH  1
#define ERTS_TTOD_USE_TSC   1
#define ERTS_TTOD_USE_UPT   1
#endif

#define ERTS_TTOD_IMPL_CHK  1
#include "ttod_impl_hpet.h"
#include "ttod_impl_hrc.h"
#include "ttod_impl_hrt.h"
#include "ttod_impl_mach.h"
#include "ttod_impl_tsc.h"
#include "ttod_impl_upt.h"
#undef  ERTS_TTOD_IMPL_CHK

/* maximum number of implementations we may have */
#define ERTS_TTOD_IMPL_COUNT \
    ( ERTS_TTOD_USE_HPET + ERTS_TTOD_USE_HRC + ERTS_TTOD_USE_HRT \
    + ERTS_TTOD_USE_MACH + ERTS_TTOD_USE_TSC + ERTS_TTOD_USE_UPT )

#else   /* ! ERTS_TTOD_ENABLED */
#define ERTS_TTOD_IMPL_COUNT 0
#endif  /* ERTS_TTOD_ENABLED */

#if (ERTS_TTOD_IMPL_COUNT > 0)

typedef u_microsecs_t (* get_ttod_f)(void);
typedef get_ttod_f (* init_ttod_f)(const char ** name);

typedef struct      /* 2 x sizeof(void *) bytes */
{
    get_ttod_f      call;
    const char *    name;
}
    ttod_impl_t;

/*
 * 'disable' is a boolean flag that is accessed externally at the address of
 * the global symbol 'erts_tolerant_timeofday'.
 */
struct
{
    char           volatile disable;    /* !!! MUST be the first byte !!!   */
    char                    fill[sizeof(ttod_impl_t) - 1];
    /* align on a sizeof(ttod_impl_t)-byte boundary */
    ttod_impl_t    volatile impl;
}
    erts_tolerant_timeofday erts_align_attribute(TIME_SUP_ALLOC_ALIGN);

/* MUST have one extra slot for the default implementation */
static ttod_impl_t ttod_impls[ERTS_TTOD_IMPL_COUNT + 1];
static unsigned    ttod_impl_count;

#if ERTS_TTOD_IMPL_NEED_GET_TTOD_FAIL || ERTS_TTOD_IMPL_NEED_GET_TTOD_NEXT
/* let the compiler decide whether to inline or not */
static unsigned get_ttod_impl_index(get_ttod_f impl)
{
    unsigned    index;
    for (index = 0; index < ttod_impl_count; ++index)
        if (ttod_impls[index].call == impl)
            break;
    return  index;
}
#endif  /* need get_ttod_impl_index */

#if ERTS_TTOD_IMPL_NEED_GET_TTOD_FAIL
#if     ! CPU_HAVE_ATOMIC_PTRPAIR_OPS
#error  Atomic operations on pairs of pointers required!
#endif
/*
 * When a tolerant_timeofday implementation can no longer reasonably expect
 * to be able to continue operating accurately, it should return via this
 * function, which resets the 'get_tolerant_timeofday' pointers to the next
 * available implementation.
 */
static u_microsecs_t get_ttod_fail(get_ttod_f cur_impl)
{
    ttod_impl_t     curr[1];
    ttod_impl_t *   next;
    unsigned        index = get_ttod_impl_index(cur_impl);

    *curr = ttod_impls[index++];
    next  = (ttod_impls + index);
    if (index == ttod_impl_count)
        erts_tolerant_timeofday.disable = -1;
    else if (index > ttod_impl_count)
        erl_exit(ERTS_ABORT_EXIT, "TTOD internal error in get_ttod_fail().");
#if TTOD_REPORT_IMPL_STATE
    erts_fprintf(stderr,
        "TTOD strategy '%s' failed, switching to '%s'\n",
        curr->name, next->name);
#endif  /* TTOD_REPORT_IMPL_STATE */

    cpu_compare_and_swap_ptr_pair(& erts_tolerant_timeofday.impl, next, curr);

    return  erts_tolerant_timeofday.impl.call();
}
#endif  /* ERTS_TTOD_IMPL_NEED_GET_TTOD_FAIL */

#if ERTS_TTOD_IMPL_NEED_GET_TTOD_NEXT
/*
 * When a tolerant_timeofday implementation can not currently provide a
 * non-default result, such as when it hasn't yet had enough time to calibrate
 * itself or has experienced a recoverable reset, it should return via this
 * function to allow the next available implementation to give it a try.
 */
static u_microsecs_t get_ttod_next(get_ttod_f cur_impl)
{
    unsigned  index = (get_ttod_impl_index(cur_impl) + 1);
    if (index > ttod_impl_count)
        erl_exit(ERTS_ABORT_EXIT, "TTOD internal error in get_ttod_next().");
    return  ttod_impls[index].call();
}
#endif  /* ERTS_TTOD_IMPL_NEED_GET_TTOD_NEXT */

#if ERTS_TTOD_IMPL_NEED_BOUND_US_ADJUSTMENT
/*
 * Encapsulate how we limit adjustment changes. Given a difference in current
 * vs calculated adjustment, returns the value to add to the current usecond
 * adjustment value to move it closer to the calculated adjustment.
 * Let the compiler decide whether to inline it or not.
 */
static s_microsecs_t bound_us_adjustment(s_microsecs_t offset)
{
    const u_microsecs_t abs = u_abs64(offset);
    /* maximum 10ms per bump */
    if (abs > ONE_MILLION)
        return  (offset < 0) ? -TEN_THOUSAND : TEN_THOUSAND;
    else if (abs > TEN_THOUSAND)
        return  (offset / ONE_HUNDRED);
    else if (abs > ONE_THOUSAND)
        return  (offset / 10);
    else
        return  offset;
}
#endif  /* ERTS_TTOD_IMPL_NEED_BOUND_US_ADJUSTMENT */

#include "ttod_impl_hpet.h"
#include "ttod_impl_hrc.h"
#include "ttod_impl_hrt.h"
#include "ttod_impl_mach.h"
#include "ttod_impl_tsc.h"
#include "ttod_impl_upt.h"

static void init_ttod_impl(init_ttod_f initfunc)
{
    ttod_impl_t * const impl = (ttod_impls + ttod_impl_count);

    impl->call = initfunc(& impl->name);

    if (impl->call != NULL)
#if TTOD_REPORT_IMPL_STATE
        erts_fprintf(stderr,
            "TTOD '%s' strategy initialized in slot %u\n",
            impl->name, ttod_impl_count++);
    else
        erts_fprintf(stderr,
            "TTOD '%s' strategy failed to initialize\n", impl->name);
#else   /* ! TTOD_REPORT_IMPL_STATE */
        ++ttod_impl_count;
#endif  /* TTOD_REPORT_IMPL_STATE */
}

static void init_tolerant_timeofday(void)
{
    ttod_impl_count = 0;

#if ERTS_TTOD_USE_TSC
    init_ttod_impl(init_ttod_tsc);
#endif

#if ERTS_TTOD_USE_MACH
    init_ttod_impl(init_ttod_mach);
#endif

#if ERTS_TTOD_USE_HPET
    init_ttod_impl(init_ttod_hpet);
#endif

#if ERTS_TTOD_USE_HRC
    init_ttod_impl(init_ttod_hrc);
#endif

#if ERTS_TTOD_USE_HRT
    init_ttod_impl(init_ttod_hrt);
#endif

#if ERTS_TTOD_USE_UPT
    init_ttod_impl(init_ttod_times);
#endif

    ttod_impls[ttod_impl_count].call = gettimeofday_us;
    ttod_impls[ttod_impl_count].name = "Default";

    erts_tolerant_timeofday.impl = ttod_impls[0];

#if TTOD_REPORT_IMPL_STATE
    if (! ttod_impl_count)
        erts_fprintf(stderr, "No TTOD strategy initialized successfully\n");
#endif  /* TTOD_REPORT_IMPL_STATE */
}

#define get_tolerant_timeofday()    erts_tolerant_timeofday.impl.call()

static CPU_FORCE_INLINE s_millisecs_t get_tolerant_timeofday_ms(void)
{
    return  ((s_millisecs_t) (get_tolerant_timeofday() / ONE_THOUSAND));
}

#else   /* ! ERTS_TTOD_IMPL_COUNT */

struct
{
    char volatile disable;
}
    erts_tolerant_timeofday;

#define init_tolerant_timeofday()   ((void) 1)

#define get_tolerant_timeofday()    gettimeofday_us()

static CPU_FORCE_INLINE s_millisecs_t get_tolerant_timeofday_ms(void)
{
    SysTimeval  tod;
    sys_gettimeofday(& tod);
    return  s_get_tv_millis(& tod);
}

#endif  /* ERTS_TTOD_IMPL_COUNT */

#define get_tolerant_timeofday_us() ((s_microsecs_t) get_tolerant_timeofday())

/*
 * End of tolerant_timeofday stuff
 */

static CPU_FORCE_INLINE void
init_approx_time(void)
{
    erts_smp_atomic_init_nob(approx_secs, 0);
}

static CPU_FORCE_INLINE erts_approx_time_t
get_approx_time(void)
{
    return (erts_approx_time_t) erts_smp_atomic_read_nob(approx_secs);
}

static CPU_FORCE_INLINE void
update_approx_time_sec(erts_approx_time_t new_secs)
{
    /*
    erts_approx_time_t old_secs = get_approx_time();
    if (old_secs != new_secs)
    */
        erts_smp_atomic_set_nob(approx_secs, new_secs);
}

static CPU_FORCE_INLINE void
erts_do_time_add(erts_short_time_t elapsed)
{
    erts_smp_atomic32_add_relb(erts_do_time, elapsed);
}

/*
 * erts_get_approx_time() returns an *approximate* time
 * in seconds. NOTE that this time may jump backwards!!!
 */
erts_approx_time_t
erts_get_approx_time(void)
{
    return get_approx_time();
}

/*
** The clock resolution should really be the resolution of the
** time function in use, which on most platforms
** is 1. On VxWorks the resolution should be
** the number of ticks per second (or 1, which would work nicely to).
**
** Setting lower resolutions is mostly interesting when timers are used
** instead of something like select.
*/

static void init_erts_deliver_time(s_millisecs_t init_ms)
{
    /* We set the initial values for deliver_time here */
    erts_smp_atomic_set_nob(last_delivered_ms, init_ms);
    /* ms resolution */
}

static void do_erts_deliver_time(s_millisecs_t curr_ms)
{
    /* Check whether we need to take lock and actually deliver ticks */
    if (((curr_ms - erts_smp_atomic_read_nob(last_delivered_ms)) / CLOCK_RESOLUTION) > 0)
    {
        s_millisecs_t elapsed;

        ACQUIRE_TOD_INNER();

        /* calculate and deliver appropriate number of ticks */
        elapsed = (curr_ms - erts_smp_atomic_read_nob(last_delivered_ms)) /
                CLOCK_RESOLUTION;

        /*
         * Sometimes the time jump backwards,
         * resulting in a negative elapsed time. We compensate for
         * this by simply pretend as if the time stood still. :)
         */
        if (elapsed > 0)
        {
            erts_do_time_add(elapsed);
            erts_smp_atomic_set_nob(last_delivered_ms, curr_ms);
        }

        RELEASE_TOD_INNER();
    }
}

int
erts_init_time_sup(void)
{
    sys_memset(ts_data, 0, sizeof(ts_data));
    sys_memset(ta_data, 0, sizeof(ta_data));

    erts_smp_mtx_init(ts_data->tod_sync, "timeofday");
    erts_smp_mtx_init(ta_data->sync, "time_sup");

    init_approx_time();

#ifndef SYS_CLOCK_RESOLUTION
    CLOCK_RESOLUTION = sys_init_time();
#else
    (void) sys_init_time();
#endif
    sys_gettimeofday(ts_data->init_tv);
    ts_data->init_ms = s_get_tv_millis(ts_data->init_tv);

    init_erts_deliver_time(ts_data->init_ms);
    erts_smp_atomic_init_nob(gtv_ms, ts_data->init_ms);
    erts_smp_atomic_init_nob(then_us, 0);

    init_tolerant_timeofday();

    erts_deliver_time();

    return CLOCK_RESOLUTION;
}

/*
 * info functions
 */

void
elapsed_time_both(
    UWord * ms_user, UWord * ms_sys,
    UWord * ms_user_diff, UWord * ms_sys_diff)
{
    UWord prev_total_user, prev_total_sys;
    UWord total_user, total_sys;
    SysTimes now;

    sys_times(& now);
    total_user = (now.tms_utime * 1000) / SYS_CLK_TCK;
    total_sys = (now.tms_stime * 1000) / SYS_CLK_TCK;

    if (ms_user != NULL)
        *ms_user = total_user;
    if (ms_sys != NULL)
        *ms_sys = total_sys;

    erts_smp_mtx_lock(ta_data->sync);

    prev_total_user = (ta_data->last->tms_utime * 1000) / SYS_CLK_TCK;
    prev_total_sys = (ta_data->last->tms_stime * 1000) / SYS_CLK_TCK;
    *(ta_data->last) = now;

    erts_smp_mtx_unlock(ta_data->sync);

    if (ms_user_diff != NULL)
        *ms_user_diff = total_user - prev_total_user;

    if (ms_sys_diff != NULL)
        *ms_sys_diff = total_sys - prev_total_sys;
}

/* wall clock routines */

void
wall_clock_elapsed_time_both(UWord * ms_total, UWord * ms_diff)
{
    s_millisecs_t   cur_ms, prev_ms;

    ACQUIRE_TOD_OUTER();

    cur_ms  = get_tolerant_timeofday_ms();
    prev_ms = erts_smp_atomic_xchg_nob(gtv_ms, cur_ms);

    /* must sync the machine's idea of time here */
    do_erts_deliver_time(cur_ms);

    RELEASE_TOD_OUTER();

    prev_ms  -= ts_data->init_ms;
    *ms_total = (UWord) (cur_ms - ts_data->init_ms);
    *ms_diff  = (*ms_total - (UWord) prev_ms);
}

/* get current time */
void
get_time(int *hour, int *minute, int *second)
{
    struct tm * tm_ptr;
    time_t      tm_clk;
    struct tm   tm_buf[1];

    tm_clk = time(NULL);
    tm_ptr = localtime_r(& tm_clk, tm_buf);

    *hour   = tm_ptr->tm_hour;
    *minute = tm_ptr->tm_min;
    *second = tm_ptr->tm_sec;
}

/* get current date */
void
get_date(int *year, int *month, int *day)
{
    struct tm * tm_ptr;
    time_t      tm_clk;
    struct tm   tm_buf[1];

    tm_clk = time(NULL);
    tm_ptr = localtime_r(& tm_clk, tm_buf);

    *year   = tm_ptr->tm_year + 1900;
    *month  = tm_ptr->tm_mon +1;
    *day    = tm_ptr->tm_mday;
}

/* get localtime */
void
get_localtime(int *year, int *month, int *day,
              int *hour, int *minute, int *second)
{
    struct tm * tm_ptr;
    time_t      tm_clk;
    struct tm   tm_buf[1];

    tm_clk = time(NULL);
    tm_ptr = localtime_r(& tm_clk, tm_buf);

    *year   = tm_ptr->tm_year + 1900;
    *month  = tm_ptr->tm_mon + 1;
    *day    = tm_ptr->tm_mday;
    *hour   = tm_ptr->tm_hour;
    *minute = tm_ptr->tm_min;
    *second = tm_ptr->tm_sec;
}


/* get universaltime */
void
get_universaltime(int *year, int *month, int *day,
                  int *hour, int *minute, int *second)
{
    struct tm * tm_ptr;
    time_t      tm_clk;
    struct tm   tm_buf[1];

    tm_clk = time(NULL);
    tm_ptr = gmtime_r(& tm_clk, tm_buf);

    *year   = tm_ptr->tm_year + 1900;
    *month  = tm_ptr->tm_mon + 1;
    *day    = tm_ptr->tm_mday;
    *hour   = tm_ptr->tm_hour;
    *minute = tm_ptr->tm_min;
    *second = tm_ptr->tm_sec;
}

/*
 * YEAR_MIN is the earliest year we are sure to be able to handle on all
 * platforms w/o problems.
 */
#define YEAR_MIN    1902
#define YEAR_MAX    (INT_MAX - 1)

/*
 * Dates are handled back to year 0. Because the Gregorian calendar was adopted
 * at different times in different areas, GREG_START is defined arbitrarily as
 * the transition year.
 * EPOCH_DAYS is is the number of days from the start ouf our calendar until
 * the Posix/Unix epoch 1-Jan-1970.
 */
#define GREG_START  1600
#define EPOCH_DAYS  135140

/*
 * days in month = 1, 2, ..., 12
 * index is 1-based, with zeroes at either end
 */
static const int MONTH_DAYS[] =
    {0, 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31, 0};

#define IN_RANGE(Min, Val, Max) (((Min) <= (Val)) && ((Val) <= (Max)))

#define is_leap_year(Year) \
    ((((Year) % 4) == 0) && (((Year) % 100 != 0) || (((Year) % 400) == 0)))

#define days_in_month(Year, Mon) \
    (((Mon) == 2) ? (is_leap_year(Year) ? 29 : 28) : MONTH_DAYS[Mon])

static int is_valid_time(
    Sint baseyear, Sint year, Sint mon, Sint day, Sint hour, Sint min, Sint sec)
{
    return (IN_RANGE(baseyear, year, YEAR_MAX)
        &&  IN_RANGE(1, mon, 12)
        &&  IN_RANGE(1, day, days_in_month(year, mon))
        &&  IN_RANGE(0, hour, 23)
        &&  IN_RANGE(0, min, 59)
        &&  IN_RANGE(0, sec, 59)
    );
}

/*
 * A more "clever" mktime
 * return  1, if successful
 * return -1, if not successful
 */
static int erl_mktime(time_t * tm_clk, struct tm * tm_ptr)
{
    time_t clock;

    clock = mktime(tm_ptr);
    *tm_clk = clock;

    if (clock != -1)
        return 1;

    /* in rare occasions mktime returns -1
     * when a correct value has been entered
     *
     * decrease seconds with one second
     * if the result is -2, epochs should be -1
     */

    tm_ptr->tm_sec -= 1;
    clock = mktime(tm_ptr);
    tm_ptr->tm_sec += 1;

    if (clock == -2)
        return 1;

    return -1;
}

/*
 * make sure nobody tries to roll back the minimum year constant, which would
 * break calc_epoch_day()
 */
#if (YEAR_MIN < GREG_START)
#error  Bad date constants, YEAR_MIN cannot be less than GREG_START
#endif

/*
 * Returns the number of days since 1-Jan-1970.
 * Internal use ONLY!
 * Parameters ARE NOT validated here, they MUST be verified with is_valid_time()
 * or equivalent before calling!
 */
static time_t calc_epoch_day(unsigned year, unsigned month, unsigned day)
{
    const unsigned  gyear = (year - GREG_START);
    Sint      ndays;
    unsigned  m;

    /* number of days in previous years */
    switch (gyear)
    {
        case 0 :
            ndays = 0;
            break;
        case 1 :
            ndays = 366;
            break;
        default :
        {
            const unsigned  pyear = (gyear - 1);
            ndays = (pyear / 4) - (pyear / 100) + (pyear / 400)
                    + (pyear * 365) + 366;
            break;
        }
    }
    /* number of days in all months preceeding month */
    for (m = 1; m < month; ++m)
        ndays += MONTH_DAYS[m];
    /* Extra day if after February in a leap year */
    if ((month > 2) && is_leap_year(year))
        ++ndays;
    ndays += (day - 1);
    return (time_t) (ndays - EPOCH_DAYS);
}

int seconds_to_univ(Sint64 time, Sint *year, Sint *month, Sint *day,
        Sint *hour, Sint *minute, Sint *second) {

    Sint y,mi;
    Sint days = time / SECONDS_PER_DAY;
    Sint secs = time % SECONDS_PER_DAY;
    Sint tmp;

    if (secs < 0) {
        days--;
        secs += SECONDS_PER_DAY;
    }

    tmp     = secs % SECONDS_PER_HOUR;

    *hour   = secs / SECONDS_PER_HOUR;
    *minute = tmp  / SECONDS_PER_MINUTE;
    *second = tmp  % SECONDS_PER_MINUTE;

    days   += 719468;
    y       = (10000*((Sint64)days) + 14780) / 3652425;
    tmp     = days - (365 * y + y/4 - y/100 + y/400);

    if (tmp < 0) {
        y--;
        tmp = days - (365*y + y/4 - y/100 + y/400);
    }
    mi = (100 * tmp + 52)/3060;
    *month = (mi + 2) % 12 + 1;
    *year  = y + (mi + 2) / 12;
    *day   = tmp - (mi * 306 + 5)/10 + 1;

    return 1;
}

int univ_to_seconds(
    Sint year, Sint month, Sint day,
    Sint hour, Sint minute, Sint second,
    Sint64 * time)
{
    Sint days;

    if (! is_valid_time(GREG_START, year, month, day, hour, minute, second))
        return 0;

    days   = calc_epoch_day(year, month, day);
    *time  = SECONDS_PER_DAY;
    *time *= days;             /* don't try overflow it, it hurts */
    *time += SECONDS_PER_HOUR * hour;
    *time += SECONDS_PER_MINUTE * minute;
    *time += second;

    return 1;
}

int local_to_univ(
    Sint * year, Sint * month, Sint * day,
    Sint * hour, Sint * minute, Sint * second,
    int isdst)
{
    struct tm * tm_ptr;
    time_t      tm_clk;
    struct tm   tm_buf[1];

    if (! is_valid_time(YEAR_MIN, *year, *month, *day, *hour, *minute, *second))
        return 0;

    tm_buf->tm_year = *year - 1900;
    tm_buf->tm_mon  = *month - 1;
    tm_buf->tm_mday = *day;
    tm_buf->tm_hour = *hour;
    tm_buf->tm_min  = *minute;
    tm_buf->tm_sec  = *second;
    tm_buf->tm_isdst = isdst;

    /*
     * the nature of mktime makes this a bit interesting,
     * up to four mktime calls could happen here
     */

    if (erl_mktime(& tm_clk, tm_buf) < 0)
    {
        if (isdst)
        {
            /*
             * If this is a timezone without DST and the OS (correctly)
             * refuses to give us a DST time, we simulate the Linux/Solaris
             * behaviour of giving the same data as if is_dst was not set.
             */
            tm_buf->tm_isdst = 0;
            if (erl_mktime(& tm_clk, tm_buf) < 0)
                /* Failed anyway, something else is bad - will be a badarg */
                return 0;
        }
        else
            /* Something else is the matter, badarg. */
            return 0;
    }

#ifdef HAVE_TIME2POSIX
    /* only if it's a real function, the macro would generate self-assignment */
    tm_clk = time2posix(tm_clk);
#endif
    tm_ptr = gmtime_r(& tm_clk, tm_buf);

    *year   = tm_ptr->tm_year + 1900;
    *month  = tm_ptr->tm_mon + 1;
    *day    = tm_ptr->tm_mday;
    *hour   = tm_ptr->tm_hour;
    *minute = tm_ptr->tm_min;
    *second = tm_ptr->tm_sec;

    return 1;
}

/*
 * Returns true/false indicating whether the input was valid and thus updated.
 */
int univ_to_local(
    Sint * year, Sint * month, Sint * day,
    Sint * hour, Sint * minute, Sint * second)
{
    struct tm * tm_ptr;
    time_t      tm_clk;
    struct tm   tm_buf[1];

    if (! is_valid_time(YEAR_MIN, *year, *month, *day, *hour, *minute, *second))
        return 0;

    tm_clk = time2posix(
        *second + (60 * (*minute + (60 * (*hour
        + (24 * calc_epoch_day(*year, *month, *day)))))));
    tm_ptr = localtime_r(& tm_clk, tm_buf);

    if (tm_ptr == NULL)
        return 0;

    *year   = tm_ptr->tm_year + 1900;
    *month  = tm_ptr->tm_mon + 1;
    *day    = tm_ptr->tm_mday;
    *hour   = tm_ptr->tm_hour;
    *minute = tm_ptr->tm_min;
    *second = tm_ptr->tm_sec;

    return 1;
}

/* get a timestamp */
void get_now(Uint * megasec, Uint * sec, Uint * microsec)
{
    Sint64 now_us, now_s, then;

    ACQUIRE_TOD_OUTER();

    now_us = get_tolerant_timeofday_us();
    do_erts_deliver_time(now_us / 1000);

    /* Make sure time is later than last */
    do
    {
        then = erts_smp_atomic_read_wb(then_us);
        if (then >= now_us)
            now_us = (then + 1);
    }
    while (erts_smp_atomic_cmpxchg_mb(then_us, now_us, then) != then);

    RELEASE_TOD_OUTER();

    now_s = (now_us / ONE_MILLION);
    *megasec  = (Uint) (now_s / ONE_MILLION);
    *sec      = (Uint) (now_s % ONE_MILLION);
    *microsec = (Uint) (now_us % ONE_MILLION);

    update_approx_time_sec(now_s);
}

void get_sys_now(Uint * megasec, Uint * sec, Uint * microsec)
{
    SysTimeval now;

    sys_gettimeofday(& now);

    *megasec  = (Uint) (now.tv_sec / ONE_MILLION);
    *sec      = (Uint) (now.tv_sec % ONE_MILLION);
    *microsec = (Uint) (now.tv_usec);

    update_approx_time_sec(now.tv_sec);
}


/* deliver elapsed *ticks* to the machine */

void erts_deliver_time(void)
{
    Sint64 now_ms;

    ACQUIRE_TOD_OUTER();

    now_ms = get_tolerant_timeofday_ms();
    do_erts_deliver_time(now_ms);

    RELEASE_TOD_OUTER();

    update_approx_time_sec(now_ms / ONE_THOUSAND);
}

/*
 * get *real* time (not ticks) remaining until next timeout - if there
 * isn't one, give a "long" time, that is guaranteed
 * to not cause overflow when we report elapsed time later on
 */
void erts_time_remaining(SysTimeval * rem_time)
{
    erts_time_t ticks;
    erts_time_t elapsed;

    /* erts_next_time() returns no of ticks to next timeout or -1 if none */

    ticks = (erts_time_t) erts_next_time();
    if (ticks == (erts_time_t) -1)
    {
        /* timer queue empty */
        /* this will cause at most 100 million ticks */
        rem_time->tv_sec  = HND_THOUSAND;
        rem_time->tv_usec = 0;
    }
    else
    {
        /* next timeout after ticks ticks */
        ticks *= CLOCK_RESOLUTION;

        ACQUIRE_TOD_OUTER();

        elapsed = (get_tolerant_timeofday_ms()
            - erts_smp_atomic_read_nob(last_delivered_ms));

        RELEASE_TOD_OUTER();

        if (ticks <= elapsed)   /* Ooops, better hurry */
        {
            rem_time->tv_sec = rem_time->tv_usec = 0;
            return;
        }
        rem_time->tv_sec  = ((ticks - elapsed) / ONE_THOUSAND);
        rem_time->tv_usec = (((ticks - elapsed) % ONE_THOUSAND) * ONE_THOUSAND);
    }
}

void erts_get_timeval(SysTimeval * tv)
{
    const u_microsecs_t usecs = get_tolerant_timeofday();
    u_set_tv_micros(tv, usecs);
    update_approx_time_sec(tv->tv_sec);
}

erts_time_t erts_get_time(void)
{
    const Sint64  secs = (get_tolerant_timeofday_us() /  ONE_MILLION);
    update_approx_time_sec(secs);
    return  secs;
}

#ifdef HAVE_ERTS_NOW_CPU
void erts_get_now_cpu(Uint * megasec, Uint * sec, Uint * microsec)
{
    SysCpuTime  t;
    SysTimespec tp;

    /* macro, 't' is a calculation buffer, result in 'tp' */
    sys_get_proc_cputime(t, tp);

    *megasec  = (Uint) ((tp.tv_sec / ONE_MILLION) % ONE_MILLION);
    *sec      = (Uint) (tp.tv_sec % ONE_MILLION);
    *microsec = (Uint) (tp.tv_nsec / ONE_THOUSAND);
}
#endif
