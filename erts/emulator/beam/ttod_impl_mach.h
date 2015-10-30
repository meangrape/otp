/*
 * %CopyrightBegin%
 *
 * Copyright Basho Technologies, Inc 2015. All Rights Reserved.
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
 * It is included twice in erl_time_sup.c:
 *
 * First, it's included with ERTS_TTOD_IMPL_CHK defined to a non-zero value.
 * If the macro ERTS_TTOD_USE_<STRATEGY> is defined with a non-zero value AND
 * the necessary resources are available to implement the strategy, that macro
 * should be defined to exactly '1' after inclusion of this file, and any of
 * the ERTS_TTOD_IMPL_NEED_xxx macros (refer to erl_time_sup.c to see which
 * ones are available) needed for compilation should be defined to '1'.
 * During this inclusion, absolutely NO code should be emitted!
 *
 * If the strategy cannot (or should not) be used in the compilation
 * environment, the ERTS_TTOD_USE_<STRATEGY> macro should have a zero value
 * after the first inclusion of this file.
 *
 * Second, it's included with ERTS_TTOD_IMPL_CHK undefined, and if the
 * ERTS_TTOD_USE_<STRATEGY> macro is defined with a non-zero value then the
 * implementation of the strategy should be included, and the code has access
 * to the static declarations indicated by the ERTS_TTOD_IMPL_NEED_xxx macros.
 *
 * If any implementation code is included, 'init_ttod_<strategy>(char ** name)'
 * MUST be a valid statement resolving to a pointer to a get_ttod_f function
 * on success or NULL if initialization was not successful.
 *
 * By convention, only static symbols are declared here, and all such symbols
 * at file scope include the moniker 'ttod_<strategy>' in their name, where
 * '<strategy>' matches 'ttod_impl_<strategy>' in the file name.
 */

#if ERTS_TTOD_IMPL_CHK

#if     ERTS_TTOD_USE_MACH && HAVE_MACH_ABSOLUTE_TIME \
    &&  CPU_HAVE_DIRECT_ATOMIC_OPS && CPU_HAVE_DIRECT_ATOMIC_128
#undef  ERTS_TTOD_USE_MACH
#define ERTS_TTOD_USE_MACH  1
#define ERTS_TTOD_IMPL_NEED_GET_TTOD_FAIL 1
#define ERTS_TTOD_IMPL_NEED_BOUND_US_ADJUSTMENT 1
#else
#undef  ERTS_TTOD_USE_MACH
#define ERTS_TTOD_USE_MACH  0
#endif  /* requirements check */

#elif   ERTS_TTOD_USE_MACH

/*
 * How many microseconds between resyncs?
 * Since the MAT is fixed frequency, we could make this pretty long, but it's
 * also the interval on which the bias is adjusted, so we don't want too much
 * delay before we try to catch up to clock adjustments.
 */
#define TTOD_MACH_MICROS_PER_RESYNC (ONE_THOUSAND * 987)

/*
 * We're using CmpXchg16b on pairs of 64-bit values - require 16-byte alignment.
 * With a little finesse, we should be able to keep the full state in a single
 * CPU cache line.
 */
#define TTOD_MACH_REQ_CPU_FEATS (ERTS_CPU_FEAT_64_BIT|ERTS_CPU_FEAT_ATOMIC_128)

/*
 * Keep the higher-frequency value (most likely to change) first, in case
 * the non-atomic load implementation turns out to be faster.
 */
typedef struct      /* 16 bytes */
{
    u_ticks_t       mat;    /* time since reset in MAT ticks    */
    u_microsecs_t   tod;    /* time since epoch in microseconds */
}
    ttod_mach_time_t;

/*
 * Make sure the size of this struture is a power of 2! Pad as needed.
 */
typedef struct
{
    ttod_mach_time_t            init;   /* synced MAT/TOD baseline      */
    ttod_mach_time_t   volatile last;   /* last MAT/TOD sync            */
    s_microsecs_t      volatile adjust; /* current correction bias usec */
    u_ticks_t                   freq;   /* MAT ticks in one second      */
    u_ticks_t                   uticks; /* MAT ticks in one microsecond */
    u_ticks_t                   resync; /* resync interval in MAT ticks */
}
    ttod_mach_state_t;

static  TIME_SUP_ALIGNED_VAR(ttod_mach_state_t, ttod_mach_state);

/* ensures fixed order */
static CPU_FORCE_INLINE u_ticks_t
fetch_ttod_mach_time_data(SysTimeval * tod)
{
    sys_gettimeofday(tod);
    return  mach_absolute_time();
}

static CPU_FORCE_INLINE void
fetch_ttod_mach_time(ttod_mach_time_t * dest)
{
    SysTimeval  tod[1];
    dest->mat = fetch_ttod_mach_time_data(tod);
    dest->tod = u_get_tv_micros(tod);
}

static CPU_FORCE_INLINE void
load_ttod_mach_time(
    volatile ttod_mach_time_t * src, ttod_mach_time_t * dest)
{
    cpu_atomic_load_128(src, dest);
}

static CPU_FORCE_INLINE int
swap_ttod_mach_time(volatile ttod_mach_time_t * dest,
    ttod_mach_time_t * src, ttod_mach_time_t * expect)
{
    return cpu_compare_and_swap_128(dest, src, expect);
}

/*
 * Return the number of microseconds since 1-Jan-1970 UTC on success or
 * get_ttod_fail(get_ttod_mach) to disable this strategy.
 *
 * Each implementation is responsible for figuring out when it has failed
 * permanently, but should not blindly continue trying when it's clear it's
 * just not working.
 */
static u_microsecs_t get_ttod_mach(void)
{
    ttod_mach_time_t    curr, last;
    u_ticks_t           ticks, span;

    /* EVERY implementation MUST do this! */
    if (erts_tolerant_timeofday.disable)
        return  gettimeofday_us();

    load_ttod_mach_time(& ttod_mach_state->last, & last);
    ticks = mach_absolute_time();

    /* sanity check */
    if (ticks < last.mat)
    {
#if TTOD_REPORT_IMPL_STATE
        erts_fprintf(stderr, "Unexpected behavior from Mach tick counter\n");
#endif  /* TTOD_REPORT_IMPL_STATE */
        return  get_ttod_fail(get_ttod_mach);
    }

    /* can we extrapolate and return fast? */
    span = (ticks - last.mat);
    if (span < ttod_mach_state->resync)
        return  (last.tod + ttod_mach_state->adjust
                + (span / ttod_mach_state->uticks));
    /*
     * Time to resync and recalibrate ...
     */
    fetch_ttod_mach_time(& curr);
    /* if 'last' has changed, adjustment is being updated elsewhere */
    if (swap_ttod_mach_time(& ttod_mach_state->last, & curr, & last))
    {
        u_microsecs_t       tod_diff, tod_calc;
        u_ticks_t           mat_diff;
        s_microsecs_t       tod_off;

        fetch_ttod_mach_time(& curr);

        tod_diff = (curr.tod - ttod_mach_state->init.tod);
        mat_diff = (curr.mat - ttod_mach_state->init.mat);
        /*
         * Undocumented, but it's been claimed that MAT on Intel CPUs is
         * always exactly nanoseconds.
         */
        if (ttod_mach_state->freq == ONE_BILLION)
            tod_calc = (mat_diff / ONE_THOUSAND);
        else
            /*
             * Use the full frequency to get a more accurate result - uticks
             * could be off by nearly a million ticks per second due to rounding.
             */
#if HAVE_INT128
            tod_calc = (u_microsecs_t)
                (((Uint128) (mat_diff * ONE_MILLION)) / ttod_mach_state->freq);
#else
            tod_calc = (u_microsecs_t)
                ((((long double) mat_diff) * ONE_MILLION) / ttod_mach_state->freq);
#endif
        /* positive if clock has advanced, negative if it's slowed */
        tod_off = (tod_diff - tod_calc + ttod_mach_state->adjust);
        if (tod_off)
        {
            s_microsecs_t new_adjust =
                (bound_us_adjustment(tod_off) + ttod_mach_state->adjust);
            cpu_atomic_store_64(& ttod_mach_state->adjust, & new_adjust);
            return  (curr.tod + new_adjust);
        }
    }
    return  (curr.tod + ttod_mach_state->adjust);
}

/*
 * Return a ttod function to indicate successful initialization.
 * Even if NULL is returned, the value in the '*name' output variable MAY
 * be used for a status message, so it MUST be initialized.
 *
 * This function should check the runtime environment to ensure support for
 * the strategy and only initialize if the necessary behavior is present.
 */
static get_ttod_f init_ttod_mach(const char ** name)
{
    mach_timebase_info_data_t   freq;
    u_ticks_t                   ticks;

    /* MUST be initialized before ANY return */
    *name = "mach";

    sys_memset(ttod_mach_state, 0, sizeof(ttod_mach_state));

    /* minimum required capabilities */
    if ((erts_cpu_features & TTOD_MACH_REQ_CPU_FEATS) != TTOD_MACH_REQ_CPU_FEATS)
        return  NULL;

    if (mach_timebase_info(& freq) != KERN_SUCCESS)
        return  NULL;

    ticks   = freq.numer;
    ticks  *= ONE_BILLION;
    ticks  /= freq.denom;
    ttod_mach_state->freq = ticks;
    ticks  /= ONE_MILLION;
    ttod_mach_state->uticks = ticks;
    ticks  *= TTOD_MACH_MICROS_PER_RESYNC;
    ttod_mach_state->resync = ticks;

    fetch_ttod_mach_time(& ttod_mach_state->init);
    ttod_mach_state->last = ttod_mach_state->init;
    ttod_mach_state->adjust = 0;

    return  get_ttod_mach;
}

#endif  /* ERTS_TTOD_USE_MACH */
