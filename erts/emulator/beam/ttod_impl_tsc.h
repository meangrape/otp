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

#if     CPU_ARCH_X86_64 \
    &&  CPU_HAVE_DIRECT_ATOMIC_OPS && CPU_HAVE_DIRECT_ATOMIC_128 \
    &&  (CPU_HAVE_GCC_INTRINSICS || CPU_HAVE_MSVC_INTRINSICS)
#undef  HAVE_TTOD_TSC
#define HAVE_TTOD_TSC   1
#if     CPU_HAVE_GCC_INTRINSICS
#include <x86intrin.h>
#ifndef __IA32INTRIN_H
#define CPU_MISSING_GCC_X86_RDTSC   1
#endif  /* ! __IA32INTRIN_H */
#elif   CPU_HAVE_MSVC_INTRINSICS
#include <intrin.h>
#pragma intrinsic(__rdtsc)
#endif  /* CPU_HAVE_xxx_INTRINSICS */
#endif  /* requirements check */

#if HAVE_TTOD_TSC

/* minimum microseconds since state->init before calculating frequency  */
#define TTOD_TSC_MIN_CALC_MICROS    ONE_MILLION
/* once we have TSC frequency, how many microseconds between resyncs    */
#define TTOD_TSC_MICROS_PER_RESYNC  (ONE_THOUSAND * 750)

/*
 * We're using CmpXchg16b on ttod_tsc_ts_pair_t and ttod_tsc_freq_t, which
 * requires 16-byte alignment.
 */

/*
 * Keep the higher-frequency value (most likely to change) first, in case
 * the non-atomic load_ttod_tsc_ts_pair() implementation turns out to be faster.
 */
typedef struct      /* 16 bytes */
{
    u_ticks_t       tsc;    /* time since reset in TSC ticks    */
    u_microsecs_t   tod;    /* time since epoch in microseconds */
}
    ttod_tsc_ts_pair_t;

typedef struct      /* 16 bytes */
{
    u_ticks_t       uticks; /* TSC ticks in one microsecond     */
    u_ticks_t       resync; /* re-sync interval in TSC ticks    */
}
    ttod_tsc_freq_t;

static volatile TIME_SUP_ALIGNED_VAR(ttod_tsc_freq_t,       ttod_tsc_freq);
static volatile TIME_SUP_ALIGNED_VAR(ttod_tsc_ts_pair_t,    ttod_tsc_last);
static volatile TIME_SUP_ALIGNED_VAR(ttod_tsc_ts_pair_t,    ttod_tsc_init);
static volatile TIME_SUP_ALIGNED_VAR(u_ticks_t,             ttod_tsc_minmax);

/* be paranoid - Intel or AMD x86_64 only! */
#define TTOD_TSC_REQ_CPU_VENDS  (ERTS_CPU_VEND_INTEL|ERTS_CPU_VEND_AMD)
#define TTOD_TSC_REQ_CPU_FEATS  (ERTS_CPU_ARCH_X86_64 \
    |ERTS_CPU_FEAT_X86_TSCP|ERTS_CPU_FEAT_X86_TSCS|ERTS_CPU_FEAT_X86_CX16)

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
fetch_ttod_tsc_ts_pair_data(SysTimeval * tod)
{
    sys_gettimeofday(tod);
    return  ttod_tsc_read_tsc();
}

static CPU_FORCE_INLINE void
fetch_ttod_tsc_ts_pair(ttod_tsc_ts_pair_t * dest)
{
    SysTimeval  tod[1];
    dest->tsc = fetch_ttod_tsc_ts_pair_data(tod);
    dest->tod = u_get_tv_micros(tod);
}

/*
 * Atomic operations on 16-byte structures
 */
static CPU_FORCE_INLINE void
load_ttod_tsc_freq(
    volatile ttod_tsc_freq_t * src, ttod_tsc_freq_t * dest)
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
load_ttod_tsc_ts_pair(
    volatile ttod_tsc_ts_pair_t * src, ttod_tsc_ts_pair_t * dest)
{
    cpu_atomic_load_128(src, dest);
}

static CPU_FORCE_INLINE int
swap_ttod_tsc_ts_pair(volatile ttod_tsc_ts_pair_t * dest,
    ttod_tsc_ts_pair_t * src, ttod_tsc_ts_pair_t * expect)
{
    return cpu_compare_and_swap_128(dest, src, expect);
}

static CPU_FORCE_INLINE void
load_ttod_tsc_minmax(volatile u_ticks_t * src, u_ticks_t * dest)
{
    cpu_atomic_load_128(src, dest);
}

static CPU_FORCE_INLINE int
swap_ttod_tsc_minmax(
    volatile u_ticks_t * dest, u_ticks_t * src, u_ticks_t * expect)
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
 * Return the number of milliseconds since 1-Jan-1970 UTC on success or one
 * of the 'TTOD_FAIL_xxx' results to try the next strategy.
 *
 * Each implementation is responsible for figuring out when it has failed
 * permanently, but should not blindly continue trying when it's clear it's
 * just not working.
 */
static u_microsecs_t get_ttod_tsc(void)
{
    ttod_tsc_ts_pair_t  curr_tp, last_tp, init_tp;
    u_microsecs_t       micros;

    if (erts_tolerant_timeofday.disable)
        return  gettimeofday_us();

    load_ttod_tsc_ts_pair(ttod_tsc_last, & last_tp);
    if ((ttod_tsc_init->tod + TTOD_TSC_MIN_CALC_MICROS) <= last_tp.tod)
    {
        const u_ticks_t tsc = ttod_tsc_read_tsc();
        /* can we just calculate and return fast? */
        if ((last_tp.tsc + ttod_tsc_freq->resync) > tsc)
        {
            const u_ticks_t     tsc_diff  = (tsc - last_tp.tsc);
            const u_microsecs_t us_diff   = (tsc_diff / ttod_tsc_freq->uticks);
            return  (last_tp.tod + us_diff);
        }
    }

    /* no matter what comes next, we're going to need the system time */
    fetch_ttod_tsc_ts_pair(& curr_tp);

    /* ... and [sane] initialization time */
    load_ttod_tsc_ts_pair(ttod_tsc_init, & init_tp);

    if (curr_tp.tsc <= init_tp.tsc || curr_tp.tod <= init_tp.tod)
    {
        /* something went backward, start over */
        while (! swap_ttod_tsc_ts_pair(ttod_tsc_init, & curr_tp, & init_tp))
        {
            load_ttod_tsc_ts_pair(ttod_tsc_init, & init_tp);
            if (init_tp.tsc >= curr_tp.tsc && init_tp.tod >= curr_tp.tod)
            {
                /* someone else initialized it */
                break;
            }
        }
    }
    /* init time should be sane now */

    /* long enough to recalculate? */
    micros = (curr_tp.tod - init_tp.tod);
    if (micros >= TTOD_TSC_MIN_CALC_MICROS)
    {
        ttod_tsc_freq_t freq, stored;
        u_ticks_t       min_max[2];
        const u_ticks_t ticks = (curr_tp.tsc - init_tp.tsc);
        const u_ticks_t tps = ((ticks * ONE_MILLION) / micros);

        load_ttod_tsc_minmax(ttod_tsc_minmax, min_max);
        while (tps < min_max[0] || tps > min_max[1])
        {
            u_ticks_t   new_range[2];
            new_range[0] = (min_max[0] && min_max[0] < tps) ? min_max[0] : tps;
            new_range[1] = (min_max[1] && min_max[1] > tps) ? min_max[1] : tps;
            if (swap_ttod_tsc_minmax(ttod_tsc_minmax, new_range, min_max))
            {
                /*
                 * 1% may be too lenient, TSC is suposed to be constant, but
                 * 0.1% might be too strict, as we're calculating against
                 * microseconds
                 */
                if ((min_max[1] - min_max[0]) > (min_max[0] / ONE_HUNDRED))
                {
#if TTOD_REPORT_IMPL_STATE
                    erts_fprintf(stderr, "Excessive TSC wobble\n");
#endif  /* TTOD_REPORT_IMPL_STATE */
                    return  get_ttod_fail(get_ttod_tsc);
                }
                break;
            }
        }
        freq.uticks = (ticks / micros);
        freq.resync = ((ticks * TTOD_TSC_MICROS_PER_RESYNC) / micros);
        load_ttod_tsc_freq(ttod_tsc_freq, & stored);
        /* ignore the result, failure means someone else just updated */
        swap_ttod_tsc_freq(ttod_tsc_freq, & freq, & stored);

        /* only set 'last' AFTER there are valid frequencies! */
        while (! swap_ttod_tsc_ts_pair(ttod_tsc_last, & curr_tp, & last_tp))
        {
            load_ttod_tsc_ts_pair(ttod_tsc_last, & last_tp);
            if (last_tp.tsc >= curr_tp.tsc && last_tp.tod >= curr_tp.tod)
            {
                /*
                 * someone else updated it, update current time as needed
                 *
                 * the compiler SHOULD optimize this to avoid
                 * repeating comparisons
                 */
                if (last_tp.tod > curr_tp.tod)
                    curr_tp = last_tp;
                else if (last_tp.tsc > curr_tp.tsc)
                    curr_tp.tsc = last_tp.tsc;
                break;
            }
        }
    }

    return  curr_tp.tod;
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

    /* initially, only activate when set in the environment */
    if (erts_sys_getenv("ERTS_ENABLE_TTOD_TSC", NULL, & evsz) < 0 || evsz < 2)
        return  NULL;

    /* minimum required capabilities */
    if ((erts_cpu_features & TTOD_TSC_REQ_CPU_FEATS) == TTOD_TSC_REQ_CPU_FEATS
    &&  (erts_cpu_features & TTOD_TSC_REQ_CPU_VENDS) != 0)
    {
        sys_memset((void *) ttod_tsc_freq, 0, sizeof(ttod_tsc_freq));
        sys_memset((void *) ttod_tsc_last, 0, sizeof(ttod_tsc_last));
        sys_memset((void *) ttod_tsc_init, 0, sizeof(ttod_tsc_init));
        sys_memset((void *) ttod_tsc_minmax, 0, sizeof(ttod_tsc_minmax));

        fetch_ttod_tsc_ts_pair((ttod_tsc_ts_pair_t *) ttod_tsc_init);

        return  get_ttod_tsc;
    }
    return  NULL;
}

#endif  /* HAVE_TTOD_TSC */
