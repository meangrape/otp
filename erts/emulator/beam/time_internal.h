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

#ifndef TIME_INTERNAL_H__
#define TIME_INTERNAL_H__

#include "erl_int_sizes_config.h"
#include "erl_cpu_features.h"

#ifndef TIME_INTERNAL_DEBUG
#ifdef  DEBUG
#define TIME_INTERNAL_DEBUG 1
#endif  /* DEBUG */
#endif  /* ! TIME_INTERNAL_DEBUG */
#ifndef TIME_INTERNAL_DEBUG
#define TIME_INTERNAL_DEBUG 0
#endif  /* ! TIME_INTERNAL_DEBUG */

/*
 * Use descriptive constants wherever we can.
 */
#define SECONDS_PER_MINUTE  60
#define SECONDS_PER_HOUR    (60 * SECONDS_PER_MINUTE)
#define SECONDS_PER_DAY     (24L * SECONDS_PER_HOUR)

#define ONE_HUNDRED         100
#define ONE_THOUSAND        1000
#define TEN_THOUSAND        10000
#define HND_THOUSAND        100000L
#define ONE_MILLION         1000000L
#define TEN_MILLION         10000000L
#define HND_MILLION         100000000L
#define ONE_BILLION         1000000000L
#define TEN_BILLION         10000000000LL
#define HND_BILLION         100000000000LL

/*
 * Memory alignment constraints, MUST be power of 2!
 */
#if CPU_HAVE_DIRECT_ATOMIC_128
#define TIME_SUP_DATA_ALIGN     16
#else
#define TIME_SUP_DATA_ALIGN     SIZEOF_VOID_P
#endif

/* ERTS_CACHE_LINE_SIZE may have a type, so the preprocessor can't grok it  */
#define TIME_SUP_ALLOC_ALIGN \
    ((TIME_SUP_DATA_ALIGN > ERTS_CACHE_LINE_SIZE) \
        ? TIME_SUP_DATA_ALIGN : ERTS_CACHE_LINE_SIZE)

#define TIME_SUP_ALLOC_SIZE(Size) \
    (((Size) + TIME_SUP_ALLOC_ALIGN - 1) & ~(TIME_SUP_ALLOC_ALIGN - 1))

/*
 * This is expensive!
 * Take entire CPU cache line(s) for the named variable, which will be an
 * array that should only be accessed as a pointer to its first element.
 *
 * If sizeof(Type) is not a power of two, the variable will still be usable,
 * and will be aligned on a cache line, but other variables may share the
 * last cache line in the allocation and affect access behavior.
 */
#define TIME_SUP_ALIGNED_VAR(Type, Name)  \
Type Name[TIME_SUP_ALLOC_SIZE(sizeof(Type)) / sizeof(Type)] \
erts_align_attribute(TIME_SUP_ALLOC_ALIGN);

/*
 * erts_short_time_t is 32 bits
 * need to be able to manipulate values as signed and unsigned
 * keep all this together because it's size-dependent
 */
typedef Sint32  s_short_time_t;
typedef Uint32  u_short_time_t;
#define INVALID_S_SHORT_TIME    -1
#define INVALID_U_SHORT_TIME    ((u_short_time_t) INVALID_S_SHORT_TIME)
#define INVALID_ERTS_SHORT_TIME ((erts_short_time_t) INVALID_S_SHORT_TIME)

/*
 * 64-bit time values
 */
typedef Sint64  s_millisecs_t;
typedef Uint64  u_millisecs_t;
typedef Sint64  s_microsecs_t;
typedef Uint64  u_microsecs_t;
typedef Sint64  s_nanosecs_t;
typedef Uint64  u_nanosecs_t;
typedef Sint64  s_ticks_t;
typedef Uint64  u_ticks_t;

typedef Sint64  ErtsEpochMicros;

/*
 * ERTS really should have global typedefs for these
 */
#if (defined(__WIN32__) || defined(_WIN32) || defined(_WIN32_))
typedef erts_time_t sys_tv_sec_t;
typedef erts_time_t sys_tv_usec_t;
#else
typedef time_t      sys_tv_sec_t;
typedef suseconds_t sys_tv_usec_t;
#endif

static CPU_FORCE_INLINE s_millisecs_t s_get_tv_millis(SysTimeval * tv)
{
    return  ( (((s_millisecs_t) tv->tv_sec) / ONE_THOUSAND)
            + (((s_millisecs_t) tv->tv_usec) * ONE_THOUSAND) );
}
static CPU_FORCE_INLINE u_millisecs_t u_get_tv_millis(SysTimeval * tv)
{
    return  ( (((u_millisecs_t) tv->tv_sec) / ONE_THOUSAND)
            + (((u_millisecs_t) tv->tv_usec) * ONE_THOUSAND) );
}
static CPU_FORCE_INLINE s_microsecs_t s_get_tv_micros(SysTimeval * tv)
{
    return  ( (((s_microsecs_t) tv->tv_sec) * ONE_MILLION)
            + ((s_microsecs_t) tv->tv_usec) );
}
static CPU_FORCE_INLINE u_microsecs_t u_get_tv_micros(SysTimeval * tv)
{
    return  ( (((u_microsecs_t) tv->tv_sec) * ONE_MILLION)
            + ((u_microsecs_t) tv->tv_usec) );
}
static CPU_FORCE_INLINE s_nanosecs_t s_get_tv_nanos(SysTimeval * tv)
{
    return  ( (((s_nanosecs_t) tv->tv_sec) * ONE_BILLION)
            + (((s_nanosecs_t) tv->tv_usec) * ONE_THOUSAND) );
}
static CPU_FORCE_INLINE u_nanosecs_t u_get_tv_nanos(SysTimeval * tv)
{
    return  ( (((u_nanosecs_t) tv->tv_sec) * ONE_BILLION)
            + (((u_nanosecs_t) tv->tv_usec) * ONE_THOUSAND) );
}

static CPU_FORCE_INLINE void s_set_tv_millis(SysTimeval * tv, s_millisecs_t v)
{
    tv->tv_sec  = (sys_tv_sec_t) (v / ONE_THOUSAND);
    tv->tv_usec = (sys_tv_usec_t) ((v % ONE_THOUSAND) * ONE_THOUSAND);
}
static CPU_FORCE_INLINE void u_set_tv_millis(SysTimeval * tv, u_millisecs_t v)
{
    tv->tv_sec  = (sys_tv_sec_t) (v / ONE_THOUSAND);
    tv->tv_usec = (sys_tv_usec_t) ((v % ONE_THOUSAND) * ONE_THOUSAND);
}
static CPU_FORCE_INLINE void s_set_tv_micros(SysTimeval * tv, s_microsecs_t v)
{
    tv->tv_sec  = (sys_tv_sec_t) (v / ONE_MILLION);
    tv->tv_usec = (sys_tv_usec_t) (v % ONE_MILLION);
}
static CPU_FORCE_INLINE void u_set_tv_micros(SysTimeval * tv, u_microsecs_t v)
{
    tv->tv_sec  = (sys_tv_sec_t) (v / ONE_MILLION);
    tv->tv_usec = (sys_tv_usec_t) (v % ONE_MILLION);
}
static CPU_FORCE_INLINE void s_set_tv_nanos(SysTimeval * tv, s_nanosecs_t v)
{
    tv->tv_sec  = (sys_tv_sec_t) (v / ONE_BILLION);
    tv->tv_usec = (sys_tv_usec_t) ((v % ONE_BILLION) / ONE_THOUSAND);
}
static CPU_FORCE_INLINE void u_set_tv_nanos(SysTimeval * tv, u_nanosecs_t v)
{
    tv->tv_sec  = (sys_tv_sec_t) (v / ONE_BILLION);
    tv->tv_usec = (sys_tv_usec_t) ((v % ONE_BILLION) / ONE_THOUSAND);
}

/*
 * Some platforms have this API for normalizing time_t according to different
 * rules for leap second correction, so make using it transparent.
 */
#ifdef  HAVE_POSIX2TIME
#if defined(HAVE_DECL_POSIX2TIME) && !HAVE_DECL_POSIX2TIME
extern time_t posix2time(time_t);
extern time_t time2posix(time_t);
#endif
#else
#define posix2time(T)   (T)
#define time2posix(T)   (T)
#endif

/*
    private time.c API for erl_time_sup.c

    returns:
        0:
            there's a timer due for processing right now
        0 < Ticks <= ERTS_SHORT_TIME_T_MAX:
            the next timer is due for processing in 'Ticks' ticks
        INVALID_ERTS_SHORT_TIME:
            there are no timers due for processing
*/
erts_short_time_t erts_next_time(void);

#if TIME_INTERNAL_DEBUG
#define DBG_NL()    erts_fprintf(stderr, "\n")
#define DBG_LOC()   erts_fprintf(stderr, "%s:%u\n", __FILE__, __LINE__)
#define DBG_UINT(V) erts_fprintf(stderr, "%s:%u %s = %u\n", __FILE__, __LINE__, #V, V)
#define DBG_SINT(V) erts_fprintf(stderr, "%s:%u %s = %u\n", __FILE__, __LINE__, #V, V)
#define DBG_MSG(S)  erts_fprintf(stderr, "%s:%u %s\n", __FILE__, __LINE__, S)
#define DBG_PTR(P)  erts_fprintf(stderr, "%s:%u %s = %p\n", __FILE__, __LINE__, #P, P)
#define DBG_FMT(F,...)  erts_fprintf(stderr, "%s:%u " F "\n", __FILE__, __LINE__, __VA_ARGS__)
#else
#define DBG_NL()
#define DBG_LOC()
#define DBG_UINT(V)
#define DBG_SINT(V)
#define DBG_MSG(S)
#define DBG_PTR(P)
#define DBG_FMT(F,...)
#endif

#endif  /* TIME_INTERNAL_H__ */
