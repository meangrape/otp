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
 * This file implements ONE Tolerant Time Of Day (TTOD) strategy.
 *
 * It is included directly in erl_time_sup.c, and has access to static
 * declarations in that file. By convention, only static symbols are declared
 * here, and all such symbols at file scope include the moniker
 * 'ttod_<strategy>', where '<strategy>' matches 'ttod_impl_<strategy>' in
 * the file name.
 *
 * On entry, the macro HAVE_TTOD_<STRATEGY> is defined with the value '0'.
 * If the necessary resources are available to implement the strategy, this
 * macro should be defined to exactly '1' after inclusion of this file, and
 * 'init_ttod_<strategy>(char ** name)' should be a valid statement resolving
 * to a pointer to a get_ttod_f function on success or NULL if initialization
 * was not successful.
 *
 * If the strategy cannot (or should not) be used in the compilation
 * environment, no code should be included and the HAVE_TTOD_<STRATEGY>
 * macro should have a zero value after inclusion of this file.
 */

#if defined(HAVE_GETHRTIME) \
    &&  CPU_HAVE_DIRECT_ATOMIC_OPS && CPU_HAVE_DIRECT_ATOMIC_128
#undef  HAVE_TTOD_HRT
#define HAVE_TTOD_HRT 1
#endif  /* requirements check */

#if HAVE_TTOD_HRT

/*
 * We're using CmpXchg16b on ttod_tsc_ts_pair_t and ttod_tsc_freq_t, which
 * requires 16-byte alignment.
 */

/*
 * Keep the higher-frequency value (most likely to change) first, in case
 * the non-atomic load_ttod_hrt_ts_pair() implementation turns out to be faster.
 *
 * HRT is number of nanoseconds since some arbitrary instant, likely boot.
 *
 * Time Of Day is maintained in nanoseconds to avoid re-scaling all over the
 * place.  As an unsigned 64-bit number, this will roll over sometime around
 * year 2554, which really should be beyond the life of this code ;)
 */
typedef struct      /* 16 bytes */
{
    u_nanosecs_t    hrt;    /* time since reset in nanoseconds  */
    u_nanosecs_t    tod;    /* time since epoch in nanoseconds  */
}
    ttod_hrt_ts_pair_t;

typedef struct      /* 16 bytes */
{
    s_nanosecs_t    adj;    /* current correction bias  */
    u_nanosecs_t    hrt;    /* last update              */
}
    ttod_hrt_current_t;

static volatile TIME_SUP_ALIGNED_VAR(ttod_hrt_current_t,    ttod_hrt_stat);
static volatile TIME_SUP_ALIGNED_VAR(ttod_hrt_ts_pair_t,    ttod_hrt_sync);
static volatile TIME_SUP_ALIGNED_VAR(ttod_hrt_ts_pair_t,    ttod_hrt_init);

#define TTOD_HRT_REQ_CPU_FEATS  (ERTS_CPU_FEAT_64_BIT|ERTS_CPU_FEAT_ATOMIC_128)

/* how many nanroseconds between resyncs */
#define TTOD_HRT_NANOS_PER_RESYNC   (ONE_MILLION * 750)

#define s_sys_gethrtime()   ((s_nanosecs_t) sys_gethrtime())
#define u_sys_gethrtime()   ((u_nanosecs_t) sys_gethrtime())

/* ensures fixed order */
static CPU_FORCE_INLINE u_nanosecs_t
fetch_ttod_hrt_ts_pair_data(SysTimeval * tod)
{
    sys_gettimeofday(tod);
    return  u_sys_gethrtime();
}

static CPU_FORCE_INLINE void
fetch_ttod_hrt_ts_pair(ttod_hrt_ts_pair_t * dest)
{
    SysTimeval  tod[1];
    dest->hrt = fetch_ttod_hrt_ts_pair_data(tod);
    dest->tod = u_get_tv_nanos(tod);
}

/*
 * Atomic operations on 16-byte structures
 */
static CPU_FORCE_INLINE void
load_ttod_hrt_ts_pair(
    volatile ttod_hrt_ts_pair_t * src, ttod_hrt_ts_pair_t * dest)
{
    cpu_atomic_load_128(src, dest);
}

static CPU_FORCE_INLINE int
swap_ttod_hrt_ts_pair(volatile ttod_hrt_ts_pair_t * dest,
    ttod_hrt_ts_pair_t * src, ttod_hrt_ts_pair_t * expect)
{
    return cpu_compare_and_swap_128(dest, src, expect);
}

static CPU_FORCE_INLINE void
load_ttod_hrt_current(
    volatile ttod_hrt_current_t * src, ttod_hrt_current_t * dest)
{
    cpu_atomic_load_128(src, dest);
}

static CPU_FORCE_INLINE int
swap_ttod_hrt_current(volatile ttod_hrt_current_t * dest,
    ttod_hrt_current_t * src, ttod_hrt_current_t * expect)
{
    return cpu_compare_and_swap_128(dest, src, expect);
}
/*
 * End of 16-byte structure atomic operations
 */

/*
 * Return the number of microseconds since 1-Jan-1970 UTC on success or
 * get_ttod_fail(get_ttod_hrt) to disable this strategy.
 *
 * Each implementation is responsible for figuring out when it has failed
 * permanently, but should not blindly continue trying when it's clear it's
 * just not working.
 */
static u_microsecs_t get_ttod_hrt(void)
{
    ttod_hrt_ts_pair_t  init_tp, sync_tp;
    ttod_hrt_current_t  last, curr;
    s_nanosecs_t        diff_ns;

    /* EVERY implementation MUST do this! */
    if (erts_tolerant_timeofday.disable)
        return  gettimeofday_us();

    curr.hrt = u_sys_gethrtime();
    load_ttod_hrt_ts_pair(ttod_hrt_init, & init_tp);
    diff_ns = (s_nanosecs_t) (curr.hrt - init_tp.hrt);

    if (diff_ns < 0)
    {
#if TTOD_REPORT_IMPL_STATE
        erts_fprintf(stderr,
            "Unexpected behavior from operating system high resolution timer\n");
#endif  /* TTOD_REPORT_IMPL_STATE */
        /*
         * TODO: count errors, try to recover a couple of times, etc.
         */
        return  get_ttod_fail(get_ttod_hrt);
    }
    load_ttod_hrt_ts_pair(ttod_hrt_sync, & sync_tp);
    load_ttod_hrt_current(ttod_hrt_stat, & last);
    diff_ns += (curr.adj = last.adj);

    if ((curr.hrt - sync_tp.hrt) > TTOD_HRT_NANOS_PER_RESYNC)
    {
        ttod_hrt_ts_pair_t  curr_tp;
        s_nanosecs_t        diff_hrt, diff_tod, diff_calc, diff_abs;
        int                 new_sync = 0;

        fetch_ttod_hrt_ts_pair(& curr_tp);
        curr.hrt  = curr_tp.hrt;
        diff_hrt  = (s_nanosecs_t) (curr_tp.hrt - init_tp.hrt);
        diff_ns   = (diff_hrt + last.adj);
        diff_tod  = (s_nanosecs_t) (curr_tp.tod - init_tp.tod);
        diff_calc = (diff_ns - diff_tod);
#if 1   /* should be intrinsic in just about all cases */
        diff_abs  = llabs(diff_calc);
#else
        diff_abs  = (diff_calc < 0) ? (0 - diff_calc) : diff_calc;
#endif
        /*
         * only re-calculate the correction if they differ by more than
         * 0.01 second (ten milliseconds)
         *
         * change in correction is limited to 1% of the time since the last
         * time was fetched, so if that was less than 100ns ago no change
         * will be applied - might be a problem on a heavily loaded system
         */
        if (diff_abs > TEN_MILLION)
        {
            const s_nanosecs_t  corr_pct =
                (s_nanosecs_t) ((curr.hrt - last.hrt) / ONE_HUNDRED);
            if (corr_pct >= diff_abs)
            {
                curr.adj -= diff_calc;
                new_sync = 1;
            }
            else if (diff_calc < 0)
                curr.adj += corr_pct;
            else
                curr.adj -= corr_pct;

            diff_ns = (((s_nanosecs_t) diff_hrt) + curr.adj);
        }
        else
            new_sync = 1;

        if (new_sync)
            swap_ttod_hrt_ts_pair(ttod_hrt_sync, & curr_tp, & sync_tp);
    }
    swap_ttod_hrt_current(ttod_hrt_stat, & curr, & last);

    return  ((init_tp.tod + diff_ns) / ONE_THOUSAND);
}

/*
 * Return a ttod function to indicate successful initialization.
 * Even if NULL is returned, the value in the '*name' output variable MAY
 * be used for a status massage, so it MUST be initialized.
 *
 * This function should check the runtime environment to ensure support for
 * the strategy and only initialize if the necessary behavior is present.
 */
static get_ttod_f init_ttod_hrt(const char ** name)
{
    /* MUST be initialized before ANY return */
    *name = "HRT";

    /* minimum required capabilities */
    if ((erts_cpu_features & TTOD_HRT_REQ_CPU_FEATS) != TTOD_HRT_REQ_CPU_FEATS)
        return  NULL;

    /*
     * Not sure what the issue is here, so I'm leaving the code mostly as-is.
     * This test was introduced in the bulk R13 commit without explanation.
     * Possibly use more selective preprocessor defs for inclusion?
     */
#if defined(__sun__) || defined(__sun) || defined(sun)
    if (sysconf(_SC_NPROCESSORS_CONF) > 1)
    {
        char    buf[1024];
        os_flavor(buf, sizeof(buf));
        /* not certain whether this check is needed with the 'sun' macros */
        if (strcmp(buf, "sunos") == 0)
        {
            int maj, min, build;
            os_version(& maj, & min, & build);
            if (maj < 5 || (maj == 5 && min <= 7))
                return  NULL;
        }
    }
#endif
    sys_init_hrtime();

    sys_memset((void *) ttod_hrt_stat, 0, sizeof(ttod_hrt_stat));
    sys_memset((void *) ttod_hrt_sync, 0, sizeof(ttod_hrt_sync));
    sys_memset((void *) ttod_hrt_init, 0, sizeof(ttod_hrt_init));

    fetch_ttod_hrt_ts_pair((ttod_hrt_ts_pair_t *) ttod_hrt_init);
    *ttod_hrt_sync = *ttod_hrt_init;
    ttod_hrt_stat->hrt = ttod_hrt_init->hrt;

    return  get_ttod_hrt;
}

#endif  /* HAVE_TTOD_HRT */