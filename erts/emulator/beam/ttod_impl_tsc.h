/*
 * %CopyrightBegin%
 *
 * Copyright Basho Technologies, Inc 2014,2015. All Rights Reserved.
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

#if     ERTS_TTOD_USE_TSC && CPU_ARCH_X86 \
    &&  CPU_HAVE_DIRECT_ATOMIC_OPS && CPU_HAVE_DIRECT_ATOMIC_128 \
    &&  (CPU_HAVE_GCC_INTRINSICS || CPU_HAVE_MSVC_INTRINSICS) \
    &&  (HAVE_MACH_ABSOLUTE_TIME || HAVE_GETHRTIME)
#undef  ERTS_TTOD_USE_TSC
#define ERTS_TTOD_USE_TSC   1
#define ERTS_TTOD_IMPL_NEED_GET_TTOD_FAIL 1
#define ERTS_TTOD_IMPL_NEED_GET_TTOD_NEXT 1
#define ERTS_TTOD_IMPL_NEED_BOUND_US_ADJUSTMENT 1
#else   /* requirements not met */
#undef  ERTS_TTOD_USE_TSC
#define ERTS_TTOD_USE_TSC   0
#endif  /* requirements check */

#elif   ERTS_TTOD_USE_TSC

/*
 * The intrinsic headers we want have have already been included, but there
 * are still some tweaks needed for getting at the TSC.
 */
#if     CPU_HAVE_GCC_INTRINSICS
#ifndef __IA32INTRIN_H
#define CPU_MISSING_GCC_X86_RDTSC   1
#endif  /* ! __IA32INTRIN_H */
#elif   CPU_HAVE_MSVC_INTRINSICS
#pragma intrinsic(__rdtsc)
#endif  /* CPU_HAVE_xxx_INTRINSICS */

/* minimum microseconds since state->init before calculating frequency  */
#define TTOD_TSC_MIN_CALC_MICROS    ONE_MILLION
/* once we have TSC frequency, how many microseconds between resyncs    */
#define TTOD_TSC_MICROS_PER_RESYNC  (ONE_THOUSAND * 750)

/*
 * We're using CmpXchg16b on pairs of 64-bit values - require 16-byte alignment.
 */
#define TTOD_TSC_REQ_CPU_FEATS  (ERTS_CPU_ARCH_X86_64 \
    |ERTS_CPU_FEAT_X86_TSCP|ERTS_CPU_FEAT_X86_TSCS|ERTS_CPU_FEAT_X86_CX16)

/* be paranoid - Intel or AMD x86_64 only! */
#define TTOD_TSC_REQ_CPU_VENDS  (ERTS_CPU_VEND_INTEL|ERTS_CPU_VEND_AMD)

/*
 * Keep the higher-frequency value (most likely to change) first, in case
 * the non-atomic load implementation turns out to be faster.
 */
typedef struct      /* 16 bytes */
{
    u_ticks_t       tsc;    /* time since reset in TSC ticks    */
    u_microsecs_t   tod;    /* time since epoch in microseconds */
}
    ttod_tsc_time_t;

typedef struct      /* 16 bytes */
{
    u_ticks_t       uticks; /* TSC ticks in one microsecond     */
    u_ticks_t       resync; /* re-sync interval in TSC ticks    */
}
    ttod_tsc_freq_t;

typedef struct      /* 16 bytes */
{
    u_ticks_t       lo;     /* minimum TSC frequency    */
    u_ticks_t       hi;     /* maximum TSC frequency    */
}
    ttod_tsc_range_t;

typedef struct      /* 16 bytes */
{
    u_ticks_t       tsc;    /* time since reset in TSC ticks        */
    u_ticks_t       ref;    /* time since reset in reference ticks  */
}
    ttod_tsc_calb_t;

/*
 * Make sure the size of this struture is a power of 2! Pad as needed.
 * Cacje lines are almost certainly 64 bytes, and this is larger, so try to
 * keep the stuff used on every call in one line.
 */
typedef struct
{
    ttod_tsc_time_t             init;       /* synced TSC/TOD baseline      */
    ttod_tsc_time_t    volatile last;       /* last TSC/TOD sync            */
    ttod_tsc_freq_t    volatile freq;       /* calculated frequency         */
    s_microsecs_t      volatile adjust;     /* current correction bias usec */
    u_ticks_t          volatile tsc_freq;   /* working TSC frequency in Hz  */
    /* end of 1st cache line                                                */
    ttod_tsc_calb_t             ref_init;   /* initial calibration point    */
    ttod_tsc_calb_t    volatile ref_last;   /* last calibration point       */
    ttod_tsc_range_t   volatile range;      /* min/max measured frequencies */
    u_ticks_t                   wobble;     /* allowable TSC wobble         */
    u_ticks_t                   ref_freq;   /* reference timer frequency    */
    /* end of 2nd cache line                                                */
}
    ttod_tsc_state_t;

static TIME_SUP_ALIGNED_VAR(ttod_tsc_state_t, ttod_tsc_state);

static CPU_FORCE_INLINE Uint64
ttod_tsc_read_tsc(void)
{
#if CPU_MISSING_GCC_X86_RDTSC
    union
    {
        Uint64  u64;
        Uint32  u32[2];
    }   tsc;
    __asm__ __volatile__ ( "rdtsc" : "=a" (tsc.u32[0]), "=d" (tsc.u32[1]) );
    return  tsc.u64;
#else
    return  __rdtsc();
#endif
}

/* ensures fixed order */
static CPU_FORCE_INLINE Uint64
fetch_ttod_tsc_time_data(SysTimeval * tod)
{
    sys_gettimeofday(tod);
    return  ttod_tsc_read_tsc();
}

static CPU_FORCE_INLINE void
fetch_ttod_tsc_time(ttod_tsc_time_t * dest)
{
    SysTimeval  tod[1];
    dest->tsc = fetch_ttod_tsc_time_data(tod);
    dest->tod = u_get_tv_micros(tod);
}

static CPU_FORCE_INLINE void
fetch_ttod_tsc_calb(ttod_tsc_calb_t * dest)
{
#if     HAVE_MACH_ABSOLUTE_TIME
    u_ticks_t ref = (u_ticks_t) mach_absolute_time();
#elif   HAVE_GETHRTIME
    u_ticks_t ref = (u_ticks_t) sys_gethrtime();
#else
#error  Unhandled calibrartion timer
#endif
    dest->tsc = ttod_tsc_read_tsc();
    dest->ref = ref;
}

/*
 * Atomic operations on 16-byte structures
 */
static CPU_FORCE_INLINE void
load_ttod_tsc_freq(volatile ttod_tsc_freq_t * src, ttod_tsc_freq_t * dest)
{
    cpu_atomic_load_128(src, dest);
}

static CPU_FORCE_INLINE int
swap_ttod_tsc_freq(volatile ttod_tsc_freq_t * dest,
    ttod_tsc_freq_t * src, ttod_tsc_freq_t * expect)
{
    return cpu_compare_and_swap_128(dest, src, expect);
}

static CPU_FORCE_INLINE void
load_ttod_tsc_time(volatile ttod_tsc_time_t * src, ttod_tsc_time_t * dest)
{
    cpu_atomic_load_128(src, dest);
}

static CPU_FORCE_INLINE int
swap_ttod_tsc_time(volatile ttod_tsc_time_t * dest,
    ttod_tsc_time_t * src, ttod_tsc_time_t * expect)
{
    return cpu_compare_and_swap_128(dest, src, expect);
}

static CPU_FORCE_INLINE void
load_ttod_tsc_calb(volatile ttod_tsc_calb_t * src, ttod_tsc_calb_t * dest)
{
    cpu_atomic_load_128(src, dest);
}

static CPU_FORCE_INLINE int
swap_ttod_tsc_calb(volatile ttod_tsc_calb_t * dest,
    ttod_tsc_calb_t * src, ttod_tsc_calb_t * expect)
{
    return cpu_compare_and_swap_128(dest, src, expect);
}

static CPU_FORCE_INLINE void
load_ttod_tsc_range(volatile ttod_tsc_range_t * src, ttod_tsc_range_t * dest)
{
    cpu_atomic_load_128(src, dest);
}

static CPU_FORCE_INLINE int
swap_ttod_tsc_range(volatile ttod_tsc_range_t * dest,
    ttod_tsc_range_t * src, ttod_tsc_range_t * expect)
{
    return cpu_compare_and_swap_128(dest, src, expect);
}

static CPU_FORCE_INLINE int
swap_ttod_tsc_val(
    volatile u_ticks_t * dest, u_ticks_t * src, u_ticks_t * expect)
{
    return cpu_compare_and_swap_64(dest, src, expect);
}
/*
 * End of 16-byte structure atomic operations
 */

/*
 * Return the number of microseconds since 1-Jan-1970 UTC on success or
 * get_ttod_fail(get_ttod_tsc) to disable this strategy.
 *
 * Each implementation is responsible for figuring out when it has failed
 * permanently, but should not blindly continue trying when it's clear it's
 * just not working.
 */
static u_microsecs_t get_ttod_tsc(void)
{
    ttod_tsc_time_t curr, last;
    u_ticks_t       ticks, span;

    /* EVERY implementation MUST do this! */
    if (erts_tolerant_timeofday.disable)
        return  gettimeofday_us();

    if (! ttod_tsc_state->tsc_freq)
    {
        ttod_tsc_calb_t     ref_curr, ref_last;
        ttod_tsc_freq_t     freq, new_freq;
        ttod_tsc_range_t    range, new_range;
        u_ticks_t           ref_span, tsc_span, tsc_freq;

        load_ttod_tsc_calb(& ttod_tsc_state->ref_last, & ref_last);
        fetch_ttod_tsc_calb(& ref_curr);
        swap_ttod_tsc_calb(& ttod_tsc_state->ref_last, & ref_curr, & ref_last);

        ref_span = (ref_curr.ref - ttod_tsc_state->ref_init.ref);
        /*
         * If it's been less than a second, just punt off to the next one.
         */
        if (ref_span < ttod_tsc_state->ref_freq)
            return  get_ttod_next(get_ttod_tsc);

        tsc_span = (ref_curr.tsc - ttod_tsc_state->ref_init.tsc);
        tsc_freq = ((tsc_span * ttod_tsc_state->ref_freq) / ref_span);

        load_ttod_tsc_range(& ttod_tsc_state->range, & range);
        do
        {
            new_range.lo = (range.lo == 0 || tsc_freq < range.lo)
                           ? tsc_freq : range.lo;
            new_range.hi = (tsc_freq > range.hi) ? tsc_freq : range.hi;
            /* very high initially, will be narrowed down over time */
            ttod_tsc_state->wobble = (new_range.hi / ONE_HUNDRED);
        }
        while (! swap_ttod_tsc_range(
                & ttod_tsc_state->range, & new_range, & range));

        if ((new_range.hi - new_range.lo) > ttod_tsc_state->wobble)
        {
#if TTOD_REPORT_IMPL_STATE
            erts_fprintf(stderr, "Excessive TSC wobble:%u: %llu:%llu\n",
                __LINE__, ttod_tsc_state->wobble, (new_range.hi - new_range.lo));
#endif  /* TTOD_REPORT_IMPL_STATE */
            return  get_ttod_fail(get_ttod_tsc);
        }
        tsc_freq = ((new_range.lo + new_range.hi) / 2);
        new_freq.uticks = (tsc_freq / ONE_MILLION);
        new_freq.resync = (new_freq.uticks * TTOD_TSC_MICROS_PER_RESYNC);
        load_ttod_tsc_freq(& ttod_tsc_state->freq, & freq);

        if (swap_ttod_tsc_freq(& ttod_tsc_state->freq, & new_freq, & freq))
            ttod_tsc_state->tsc_freq = tsc_freq;
    }
    /*
     * At this point, we have at least an initial idea of the TSC frequency ...
     */
    load_ttod_tsc_time(& ttod_tsc_state->last, & last);
    ticks = ttod_tsc_read_tsc();

    /* sanity check */
    if ((ticks + ttod_tsc_state->wobble) < last.tsc)
    {
#if TTOD_REPORT_IMPL_STATE
        erts_fprintf(stderr, "Excessive TSC wobble:%u: %llu:%llu\n",
            __LINE__, ttod_tsc_state->wobble, (last.tsc - ticks));
#endif  /* TTOD_REPORT_IMPL_STATE */
        return  get_ttod_fail(get_ttod_tsc);
    }

    /* can we extrapolate and return fast? */
    span = (ticks - last.tsc);
    if (span < ttod_tsc_state->freq.resync)
        return  (last.tod + ttod_tsc_state->adjust
                + (span / ttod_tsc_state->freq.uticks));
    /*
     * Time to resync and recalibrate ...
     * Recalculate the frequency first, if it's due, so we're always working
     * with the most accurate info.
     */
    if (ticks > (ttod_tsc_state->ref_last.tsc + ttod_tsc_state->tsc_freq))
    {
        ttod_tsc_calb_t ref_curr, ref_last;

        load_ttod_tsc_calb(& ttod_tsc_state->ref_last, & ref_last);
        fetch_ttod_tsc_calb(& ref_curr);
        /* if it's being updated on another thread, don't do it here, too */
        if (swap_ttod_tsc_calb(& ttod_tsc_state->ref_last, & ref_curr, & ref_last))
        {
            ttod_tsc_range_t    range;
            u_ticks_t           ref_span, tsc_span, tsc_freq;

            ref_span = (ref_curr.ref - ttod_tsc_state->ref_init.ref);
            tsc_span = (ref_curr.tsc - ttod_tsc_state->ref_init.tsc);
            tsc_freq = ((tsc_span * ttod_tsc_state->ref_freq) / ref_span);

            load_ttod_tsc_range(& ttod_tsc_state->range, & range);
            if (tsc_freq < range.lo || tsc_freq > range.hi)
            {
                ttod_tsc_freq_t     freq, new_freq;
                ttod_tsc_range_t    new_range;
                u_ticks_t           freq_span, avg_freq, avg_uticks;
                do
                {
                    new_range.lo = (tsc_freq < range.lo) ? tsc_freq : range.lo;
                    new_range.hi = (tsc_freq > range.hi) ? tsc_freq : range.hi;
                    freq_span = (new_range.hi - new_range.lo);
                    avg_freq = ((new_range.lo + new_range.hi) / 2);
                    avg_uticks = (avg_freq / ONE_MILLION);
                    ttod_tsc_state->wobble = (freq_span + avg_uticks);
                }
                while (! swap_ttod_tsc_range(
                        & ttod_tsc_state->range, & new_range, & range));

                if (freq_span > (avg_uticks * 2))
                {
#if TTOD_REPORT_IMPL_STATE
                    erts_fprintf(stderr, "Excessive TSC wobble:%u: %llu:%llu\n",
                        __LINE__, (avg_uticks * 2), freq_span);
#endif  /* TTOD_REPORT_IMPL_STATE */
                    return  get_ttod_fail(get_ttod_tsc);
                }
                new_freq.uticks = avg_uticks;
                new_freq.resync = (avg_uticks * TTOD_TSC_MICROS_PER_RESYNC);
                load_ttod_tsc_freq(& ttod_tsc_state->freq, & freq);
                if (swap_ttod_tsc_freq(& ttod_tsc_state->freq, & new_freq, & freq))
                    ttod_tsc_state->tsc_freq = avg_freq;
            }
        }
    }
    /* now figure out the adjustment */
    fetch_ttod_tsc_time(& curr);
    /* if 'last' has changed, adjustment is being updated elsewhere */
    if (swap_ttod_tsc_time(& ttod_tsc_state->last, & curr, & last))
    {
        u_microsecs_t   tod_diff, tod_calc;
        u_ticks_t       tsc_diff;
        s_microsecs_t   tod_off;

        tod_diff = (curr.tod - ttod_tsc_state->init.tod);
        tsc_diff = (curr.tsc - ttod_tsc_state->init.tsc);
        /*
         * Use the full frequency to get a more accurate result - uticks
         * could be off by nearly a million ticks per second due to rounding.
         */
#if HAVE_INT128
        tod_calc = (u_microsecs_t)
            (((Uint128) (tsc_diff * ONE_MILLION)) / ttod_tsc_state->tsc_freq);
#else
        tod_calc = (u_microsecs_t)
            ((((long double) tsc_diff) / ttod_tsc_state->tsc_freq) * ONE_MILLION);
#endif
        /* positive if clock has advanced, negative if it's slowed */
        tod_off = (tod_diff - tod_calc + ttod_tsc_state->adjust);
        if (tod_off)
        {
            s_microsecs_t new_adjust =
                (bound_us_adjustment(tod_off) + ttod_tsc_state->adjust);
            cpu_atomic_store_64(& ttod_tsc_state->adjust, & new_adjust);
            return  (curr.tod + new_adjust);
        }
    }
    return  (curr.tod + ttod_tsc_state->adjust);
}

/*
 * Return a ttod function to indicate successful initialization.
 * Even if NULL is returned, the value in the '*name' output variable MAY
 * be used for a status massage, so it MUST be initialized.
 *
 * This function should check the runtime environment to ensure support for
 * the strategy and only initialize if the necessary behavior is present.
 */
static get_ttod_f init_ttod_tsc(const char ** name)
{
    size_t  evsz = 0;

    /* MUST be initialized before ANY return */
    *name = "TSC";

    sys_memset(ttod_tsc_state, 0, sizeof(ttod_tsc_state));

    /* initially, only activate when set in the environment */
    if (erts_sys_getenv("ERTS_ENABLE_TTOD_TSC", NULL, & evsz) < 0 || evsz < 2)
        return  NULL;

    /* minimum required capabilities */
    if ((erts_cpu_features & TTOD_TSC_REQ_CPU_FEATS) == TTOD_TSC_REQ_CPU_FEATS
    &&  (erts_cpu_features & TTOD_TSC_REQ_CPU_VENDS) != 0)
    {
#if     HAVE_MACH_ABSOLUTE_TIME
        mach_timebase_info_data_t   freq;

        if (mach_timebase_info(& freq) != KERN_SUCCESS)
            return  NULL;

        ttod_tsc_state->ref_freq  = freq.numer;
        ttod_tsc_state->ref_freq *= ONE_BILLION;
        ttod_tsc_state->ref_freq /= freq.denom;
#elif   HAVE_GETHRTIME
        ttod_tsc_state->ref_freq  = ONE_BILLION;
#else
#error  Uninitialized calibration timer frequency
#endif  /* reference timer initialization */

        fetch_ttod_tsc_calb(& ttod_tsc_state->ref_init);
        fetch_ttod_tsc_time(& ttod_tsc_state->init);
        ttod_tsc_state->ref_last = ttod_tsc_state->ref_init;
        ttod_tsc_state->last = ttod_tsc_state->init;

        return  get_ttod_tsc;
    }
    return  NULL;
}

#endif  /* ERTS_TTOD_USE_TSC */

