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

#ifndef ERL_CPU_FEATURES_H__
#define ERL_CPU_FEATURES_H__

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include "sys.h"

#undef  CPU_HAVE_DIRECT_ATOMIC_OPS
#undef  CPU_HAVE_DIRECT_ATOMIC_128
#undef  CPU_HAVE_GCC_ASM
#undef  CPU_HAVE_GCC_INTRINSICS
#undef  CPU_HAVE_MSVC_ASM
#undef  CPU_HAVE_MSVC_INTRINSICS
#undef  CPU_FORCE_INLINE

#if     defined(__GNUC__)
#define CPU_HAVE_GCC_ASM            1
#define CPU_HAVE_GCC_INTRINSICS     1
#define CPU_HAVE_DIRECT_ATOMIC_OPS  1
# if    defined(__x86_64__)
#define CPU_ARCH_X86                1
#define CPU_ARCH_X86_64             1
#define CPU_HAVE_DIRECT_ATOMIC_128  1
/* TODO: come up with a better test for this */
#ifdef  DEBUG
#define CPU_USE_GCC_ATOMIC_128_ASM  1
#else
#define CPU_USE_GCC_ATOMIC_128_ASM  0
#endif  /* use old GCC 128-bit intrinsics */
#include <x86intrin.h>
# elif  defined(__i386__)
#define CPU_ARCH_X86                1
#include <x86intrin.h>
# endif /* architecture */
#define CPU_FORCE_INLINE    __inline__ __attribute__((__always_inline__))

#elif   defined(_MSC_VER)
#define CPU_HAVE_MSVC_INTRINSICS    1
#define CPU_HAVE_DIRECT_ATOMIC_OPS  1
# if    defined(_M_AMD64)
#define CPU_ARCH_X86                1
#define CPU_ARCH_X86_64             1
#define CPU_HAVE_DIRECT_ATOMIC_128  1
# elif  defined(_M_IX86)
#define CPU_ARCH_X86                1
#define CPU_HAVE_MSVC_ASM           1
# endif /* architecture */
#include <intrin.h>
#define CPU_FORCE_INLINE    __forceinline

#endif  /* compiler */
#ifndef CPU_FORCE_INLINE
#define CPU_FORCE_INLINE    ERTS_INLINE
#endif  /* CPU_FORCE_INLINE */

/*
 * Assume all CPUs in the system are the same, because we're just completely
 * hosed if they're not!
 *
 * All feature flags are defined in all compilation units, but they're only
 * relevant if the matching 'ERTS_CPU_ARCH_nnn' flag is set.
 */

#define ERTS_CPU_FEAT_INITIALIZED   1uLL
#define ERTS_CPU_FEAT_MAX_FLAG      (1uLL << 31)

/*
 * Where 'ERTS_CPU_ARCH_xxx' instructions are present in 64-bit mode, the
 * 'ERTS_CPU_ARCH_xxx_64' flag and 'ERTS_CPU_ARCH_xxx' are both set - check
 * for the 'ERTS_CPU_ARCH_xxx_64' variant if that's what you need.
 *
 * For each of the 'ERTS_CPU_ARCH_...' flags, a corresponding
 * 'CPU_ARCH_...' macro is defined at compile time to a non-zero value if
 * indicating the compilation environment (if supported internally).
 */
#define ERTS_CPU_ARCH_X86           (1uLL << 1)
#define ERTS_CPU_ARCH_X86_64        (1uLL << 2)
#define ERTS_CPU_ARCH_AMD64         ERTS_CPU_ARCH_X86_64
#define ERTS_CPU_ARCH_PPC           (1uLL << 3)
#define ERTS_CPU_ARCH_PPC_64        (1uLL << 4)
#define ERTS_CPU_ARCH_SPARC         (1uLL << 5)
#define ERTS_CPU_ARCH_SPARC_64      (1uLL << 6)

#define ERTS_CPU_VEND_INTEL         (1uLL << 11)
#define ERTS_CPU_VEND_AMD           (1uLL << 12)
#define ERTS_CPU_VEND_IBM           (1uLL << 13)
#define ERTS_CPU_VEND_SUN           (1uLL << 14)
#define ERTS_CPU_VEND_HP            (1uLL << 15)

/*
 * Features applicable to all architectures
 */
#define ERTS_CPU_FEAT_64_BIT        (1uLL << 18)
#define ERTS_CPU_FEAT_ATOMIC_128    (1uLL << 19)

/*
 * Flags below here overlap by architecture
 */

#define ERTS_CPU_FEAT_X86_CPUID     (1uLL << 20)   /* Have CPUID            */
#define ERTS_CPU_FEAT_X86_AES       (1uLL << 21)   /* Have AES instructions */
#define ERTS_CPU_FEAT_X86_CMOV      (1uLL << 22)   /* Have CMOVcc           */
#define ERTS_CPU_FEAT_X86_CRC32     (1uLL << 23)   /* Have CRC32            */
#define ERTS_CPU_FEAT_X86_CX8       (1uLL << 24)   /* Have CMPXCHG8B        */
#define ERTS_CPU_FEAT_X86_CX16      (1uLL << 25)   /* Have CMPXCHG16B       */
#define ERTS_CPU_FEAT_X86_RAND      (1uLL << 26)   /* Have RNG instructions */
#define ERTS_CPU_FEAT_X86_TSC       (1uLL << 27)   /* Have RDTSC            */
#define ERTS_CPU_FEAT_X86_TSCP      (1uLL << 28)   /* Have RDTSCP           */
#define ERTS_CPU_FEAT_X86_TSCS      (1uLL << 29)   /* TSC is Stable         */
#define ERTS_CPU_FEAT_X86_MAX_FLAG  ERTS_CPU_FEAT_X86_TSCS
#if (ERTS_CPU_FEAT_X86_MAX_FLAG > ERTS_CPU_FEAT_MAX_FLAG)
#undef  ERTS_CPU_FEAT_MAX_FLAG
#define ERTS_CPU_FEAT_MAX_FLAG  ERTS_CPU_FEAT_X86_MAX_FLAG
#endif

#if (ERTS_CPU_FEAT_MAX_FLAG > (1uLL << 31))
typedef Uint64  erts_cpu_features_t;
#else
typedef Uint32  erts_cpu_features_t;
#endif

extern erts_cpu_features_t erts_cpu_features;

extern void erts_init_cpu_features(void);

/*
 * We use distinct implementations here to be able to place the variables in
 * an assortment of situations directly without the union structures used by
 * the ERTS atomic types, and to be certain of inlining and behavior
 * characteristics.
 *
 * Variables of the types used here MUST be aligned on address boundaries of
 * their size or hardware faults may occur!
 *
 * Conceivably, this code could all be made to run on 64-bit CPUs in 32-bit
 * OSes using some combination of inline assembler and emitted machine
 * inctructions, but why bother.
 */
#if CPU_HAVE_DIRECT_ATOMIC_OPS
#if     defined(CPU_HAVE_GCC_INTRINSICS)
static CPU_FORCE_INLINE int
cpu_compare_and_swap_32(volatile void * dest, void * src, void * expect)
{
    return __atomic_compare_exchange(
        (volatile Uint32 *) dest, (Uint32 *) expect, (Uint32 *) src,
        0, __ATOMIC_RELAXED, __ATOMIC_RELAXED);
}
static CPU_FORCE_INLINE int
cpu_compare_and_swap_64(volatile void * dest, void * src, void * expect)
{
    return __atomic_compare_exchange(
        (volatile Uint64 *) dest, (Uint64 *) expect, (Uint64 *) src,
        0, __ATOMIC_RELAXED, __ATOMIC_RELAXED);
}
#elif   defined(CPU_HAVE_MSVC_INTRINSICS)
#pragma intrinsic(_InterlockedCompareExchange)
#pragma intrinsic(_InterlockedCompareExchange64)
static CPU_FORCE_INLINE int
cpu_compare_and_swap_32(volatile void * dest, void * src, void * expect)
{
    const __int32 out = _InterlockedCompareExchange(
        (volatile __int32 *) dest, *((__int32 *) src), *((__int32 *) expect));
    if (out == *((__int32 *) expect))
        return  1;
    *((__int32 *) expect) = out;
    return  0;
}
static CPU_FORCE_INLINE int
cpu_compare_and_swap_64(volatile void * dest, void * src, void * expect)
{
    const __int64 out = _InterlockedCompareExchange64(
        (volatile __int64 *) dest, *((__int64 *) src), *((__int64 *) expect));
    if (out == *((__int64 *) expect))
        return  1;
    *((__int64 *) expect) = out;
    return  0;
}
#else
#error  Missing intrinsics for CPU_HAVE_DIRECT_ATOMIC_OPS
#endif  /* CPU_HAVE_xxx_INTRINSICS */

#if CPU_HAVE_DIRECT_ATOMIC_128
#if     defined(CPU_HAVE_GCC_INTRINSICS)
static CPU_FORCE_INLINE void
cpu_atomic_load_128(volatile void * src, void * dest)
{
#if CPU_USE_GCC_ATOMIC_128_ASM
    do
    {   /* yields a consistent copy at some instant */
        ((Uint64 *) dest)[0] = ((volatile Uint64 *) src)[0];
        ((Uint64 *) dest)[1] = ((volatile Uint64 *) src)[1];
    }
    while (((Uint64 *) dest)[0] != ((volatile Uint64 *) src)[0]);
#else
    __atomic_load(
        (volatile __int128 *) src, (__int128 *) dest, __ATOMIC_RELAXED);
#endif  /* CPU_USE_GCC_ATOMIC_128_ASM */
}
static CPU_FORCE_INLINE int
cpu_compare_and_swap_128(volatile void * dest, void * src, void * expect)
{
#if CPU_USE_GCC_ATOMIC_128_ASM
    /*
     * Not the absolute most efficient implementation, but good enough.
     * In particular, it always writes the value back to 'expect' even if it
     * hasn't changed. Then again, that may not cost any more than a
     * conditional jump.
     * This code SHOULD only get used in debug builds, where the intrinsic
     * isn't found by the linker.
     */
    unsigned char ret;
    __asm__ __volatile__ (
                "lock"
        "\n\t"  "cmpxchg16b (%0)"
        "\n\t"  "setz    %1"
        : "+mr" (dest), "=c" (ret),
          "+a" (((Uint64 *) expect)[0]), "+d" (((Uint64 *) expect)[1])
        : "b" (((Uint64 *) src)[0]), "c" (((Uint64 *) src)[1])
        : "cc" );
    return  ret;
#else
    return __atomic_compare_exchange(
        (volatile __int128 *) dest, (__int128 *) expect, (__int128 *) src,
        0, __ATOMIC_RELAXED, __ATOMIC_RELAXED);
#endif  /* CPU_USE_GCC_ATOMIC_128_ASM */
}
#elif   defined(CPU_HAVE_MSVC_INTRINSICS)
#pragma intrinsic(_InterlockedCompareExchange128)
static CPU_FORCE_INLINE void
cpu_atomic_load_128(volatile void * src, void * dest)
{
    /*
     * TODO: See what MSVC generates from this.
     *
     * CmpXchg16b is the instruction we want, but without additional baggage.
     * If MSVC isn't smart enough to optimize the cruft away, use the
     * alternative below, which is likely to be pretty fast.
     */
#if 1
    _InterlockedCompareExchange128(
        (volatile __int64 *) src,
        ((__int64 *) dest)[1], ((__int64 *) dest)[0],
        (__int64 *) dest);
#else
    do
    {
        /*
         * IFF the value is ONLY ever written atomically, this will give us
         * some consistent snapshot of it.
         */
        ((__int64 *) dest)[0] = ((volatile __int64 *) src)[0];
        ((__int64 *) dest)[1] = ((volatile __int64 *) src)[1];
    }
    while (((__int64 *) dest)[0] != ((volatile __int64 *) src)[0]);
#endif
}
static CPU_FORCE_INLINE int
cpu_compare_and_swap_128(volatile void * dest, void * src, void * expect)
{
    return _InterlockedCompareExchange128(
        (volatile __int64 *) dest,
        ((__int64 *) src)[1], ((__int64 *) src)[0],
        (__int64 *) expect);
}
#else
#error  Missing intrinsics for CPU_HAVE_DIRECT_ATOMIC_128
#endif  /* CPU_HAVE_xxx_INTRINSICS */

#endif  /* CPU_HAVE_DIRECT_ATOMIC_128 */
#endif  /* CPU_HAVE_DIRECT_ATOMIC_OPS */

#endif  /* ERL_CPU_FEATURES_H__ */
