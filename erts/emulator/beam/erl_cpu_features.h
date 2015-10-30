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
#include "erl_int_sizes_config.h"
#include "sys.h"

#undef  CPU_HAVE_DIRECT_ATOMIC_OPS
#undef  CPU_HAVE_DIRECT_ATOMIC_128
#undef  CPU_HAVE_ATOMIC_PTRPAIR_OPS
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
 *
 * Some GCC-ish compilers don't translate the __atomic_xxx builtins to machine
 * code, instead emitting calls to functions that don't actually exist,
 * resulting in linker errors. The old-style __sync_xxx operations don't write
 * back the 'expect' value, so we just use inline assembler throughout to get
 * the operations and behaviors we want without worrying about what works where.
 *
 * The 'dest', 'src', and 'expect' pointers are aliased to macros 'd', 's',
 * and 'x', respectively, instead of declaring transient pointer variables,
 * because at low optimization levels the macros won't generate as much code.
 */
#if CPU_HAVE_DIRECT_ATOMIC_OPS
#if     defined(CPU_HAVE_GCC_INTRINSICS)

static CPU_FORCE_INLINE int
cpu_compare_and_swap_32(volatile void * dest, void * src, void * expect)
{
#define s ((Uint32 *) src)
#define x ((Uint32 *) expect)
    unsigned char ret;
    __asm__ __volatile__( "lock"
        "\n\t"  "cmpxchgl %4, (%2)"
        "\n\t"  "setz    %0"
        "\n\t"  "jz      equal_%="
        "\n\t"  "movl    %%eax, %1"
        "\nequal_%=:"
        : "=r" (ret), "=mr" (*x)
        : "r" (dest), "a" (*x), "r" (*s)
        : "cc", "eax" );
    return  ret;
#undef x
#undef s
}
static CPU_FORCE_INLINE void
cpu_atomic_load_64(volatile void * src, void * dest)
{
#ifdef  ARCH_64
#define d ((Uint64 *) dest)
#define s ((volatile Uint64 *) src)
    *d = *s;
#else   /* ARCH_32 */
#define d ((Uint32 *) dest)
    __asm__ __volatile__( "lock"
        "\n\t"  "cmpxchg8b (%2)"
        : "=a" (d[0]), "=d" (d[1])
        : "r" (src), "a" (0), "d" (0), "b" (0), "c" (0)
        : "cc" );
#endif  /* ARCH_nn */
#undef s
#undef d
}
static CPU_FORCE_INLINE void
cpu_atomic_store_64(volatile void * dest, void * src)
{
#ifdef  ARCH_64
#define d ((volatile Uint64 *) dest)
#define s ((Uint64 *) src)
    *d = *s;
#else   /* ARCH_32 */
#define d ((volatile Uint32 *) dest)
#define s ((Uint32 *) src)
    __asm__ __volatile__(
        "store_%=:"
        "\n\t"  "lock"
        "\n\t"  "cmpxchg8b (%0)"
        "\n\t"  "jne     store_%="
        : : "r" (dest), "b" (s[0]), "c" (s[1]), "a" (d[0]), "d" (d[1])
        : "cc" );
#endif  /* ARCH_nn */
#undef s
#undef d
}
static CPU_FORCE_INLINE int
cpu_compare_and_swap_64(volatile void * dest, void * src, void * expect)
{
    unsigned char ret;
#ifdef  ARCH_64
#define s ((Uint64 *) src)
#define x ((Uint64 *) expect)
    __asm__ __volatile__( "lock"
        "\n\t"  "cmpxchgq %4, (%2)"
        "\n\t"  "setz    %0"
        "\n\t"  "jz      equal_%="
        "\n\t"  "movq    %%rax, %1"
        "\nequal_%=:"
        : "=r" (ret), "=mr" (*x)
        : "r" (dest), "a" (*x), "r" (*s)
        : "cc", "rax" );
#else   /* ARCH_32 */
#define s ((Uint32 *) src)
#define x ((Uint32 *) expect)
    __asm__ __volatile__( "lock"
        "\n\t"  "cmpxchg8b (%3)"
        "\n\t"  "setz    %0"
        "\n\t"  "jz      equal_%="
        "\n\t"  "movl    %%eax, %1"
        "\n\t"  "movl    %%edx, %2"
        "\nequal_%=:"
        : "=r" (ret), "=mr" (x[0]), "=mr" (x[1])
        : "r" (dest), "a" (x[0]), "d" (x[1]), "b" (s[0]), "c" (s[1])
        : "cc", "eax", "edx" );
#endif  /* ARCH_nn */
    return  ret;
#undef x
#undef s
}

#elif   defined(CPU_HAVE_MSVC_INTRINSICS)
#pragma intrinsic(_InterlockedCompareExchange)
#pragma intrinsic(_InterlockedCompareExchange64)
#ifdef  ARCH_32
#pragma intrinsic(_InterlockedExchange64)
#endif

static CPU_FORCE_INLINE int
cpu_compare_and_swap_32(volatile void * dest, void * src, void * expect)
{
#define d ((volatile __int32 *) dest)
#define s ((__int32 *) src)
#define x ((__int32 *) expect)
    const __int32 out = _InterlockedCompareExchange(d, *s, *x);
    if (out == *x)
        return  1;
    *x = out;
    return  0;
#undef x
#undef s
#undef d
}
static CPU_FORCE_INLINE void
cpu_atomic_load_64(volatile void * src, void * dest)
{
#define d ((__int64 *) dest)
#define s ((volatile __int64 *) src)
#ifdef  ARCH_64
    *d = *s;
#else
    *d = _InterlockedCompareExchange64(s, 0, 0);
#endif
#undef s
#undef d
}
static CPU_FORCE_INLINE void
cpu_atomic_store_64(volatile void * dest, void * src)
{
#define d ((volatile __int64 *) dest)
#define s ((__int64 *) src)
#ifdef  ARCH_64
    *d = *s;
#else
    _InterlockedExchange64(d, *s);
#endif
#undef s
#undef d
}
static CPU_FORCE_INLINE int
cpu_compare_and_swap_64(volatile void * dest, void * src, void * expect)
{
#define d ((volatile __int64 *) dest)
#define s ((__int64 *) src)
#define x ((__int64 *) expect)
    const __int64 out = _InterlockedCompareExchange64(d, *s, *x);
    if (out == *x)
        return  1;
    *x = out;
    return  0;
#undef x
#undef s
#undef d
}

#else
#error  Missing intrinsics for CPU_HAVE_DIRECT_ATOMIC_OPS
#endif  /* CPU_HAVE_xxx_INTRINSICS */

#if CPU_HAVE_DIRECT_ATOMIC_128
#if     defined(CPU_HAVE_GCC_INTRINSICS)

static CPU_FORCE_INLINE void
cpu_atomic_load_128(volatile void * src, void * dest)
{
#define d ((Uint64 *) dest)
    __asm__ __volatile__( "lock"
        "\n\t"  "cmpxchg16b (%2)"
        : "=a" (d[0]), "=d" (d[1])
        : "r" (src), "a" (0), "d" (0), "b" (0), "c" (0)
        : "cc" );
#undef d
}
static CPU_FORCE_INLINE void
cpu_atomic_store_128(volatile void * dest, void * src)
{
#define d ((volatile Uint64 *) dest)
#define s ((Uint64 *) src)
    __asm__ __volatile__(
        "store_%=:"
        "\n\t"  "lock"
        "\n\t"  "cmpxchg16b (%0)"
        "\n\t"  "jne     store_%="
        : : "r" (dest), "b" (s[0]), "c" (s[1]), "a" (d[0]), "d" (d[1])
        : "cc" );
#undef s
#undef d
}
static CPU_FORCE_INLINE int
cpu_compare_and_swap_128(volatile void * dest, void * src, void * expect)
{
#define s ((Uint64 *) src)
#define x ((Uint64 *) expect)
    unsigned char ret;
    __asm__ __volatile__( "lock"
        "\n\t"  "cmpxchg16b (%3)"
        "\n\t"  "setz    %0"
        "\n\t"  "jz      equal_%="
        "\n\t"  "movq	 %%rax, %1"
        "\n\t"  "movq	 %%rdx, %2"
        "\nequal_%=:"
        : "=r" (ret), "=mr" (x[0]), "=mr" (x[1])
        : "r" (dest), "a" (x[0]), "d" (x[1]), "b" (s[0]), "c" (s[1])
        : "cc", "rax", "rdx" );
    return  ret;
#undef x
#undef s
}

#elif   defined(CPU_HAVE_MSVC_INTRINSICS)
#pragma intrinsic(_InterlockedCompareExchange128)

static CPU_FORCE_INLINE void
cpu_atomic_load_128(volatile void * src, void * dest)
{
#define d ((__int64 *) dest)
#define s ((volatile __int64 *) src)
    _InterlockedCompareExchange128(s, d[1], d[0], d);
#undef s
#undef d
}
static CPU_FORCE_INLINE void
cpu_atomic_store_128(volatile void * dest, void * src)
{
#define d ((volatile __int64 *) dest)
#define s ((__int64 *) src)
    __int64 x[2] = {d[0], d[1]};
    while (! _InterlockedCompareExchange128(d, s[1], s[0], x));
#undef s
#undef d
}
static CPU_FORCE_INLINE int
cpu_compare_and_swap_128(volatile void * dest, void * src, void * expect)
{
#define d ((volatile __int64 *) dest)
#define s ((__int64 *) src)
#define x ((__int64 *) expect)
    return _InterlockedCompareExchange128(d, s[1], s[0], x);
#undef x
#undef s
#undef d
}

#else
#error  Missing intrinsics for CPU_HAVE_DIRECT_ATOMIC_128
#endif  /* CPU_HAVE_xxx_INTRINSICS */

#endif  /* CPU_HAVE_DIRECT_ATOMIC_128 */

#if     defined(ARCH_32)
#define cpu_compare_and_swap_ptr        cpu_compare_and_swap_32
#define cpu_atomic_load_ptr_pair        cpu_atomic_load_64
#define cpu_atomic_store_ptr_pair       cpu_atomic_store_64
#define cpu_compare_and_swap_ptr_pair   cpu_compare_and_swap_64
#define CPU_HAVE_ATOMIC_PTRPAIR_OPS     1
#elif   defined(ARCH_64)
#define cpu_compare_and_swap_ptr        cpu_compare_and_swap_64
#if     CPU_HAVE_DIRECT_ATOMIC_128
#define cpu_atomic_load_ptr_pair        cpu_atomic_load_128
#define cpu_atomic_store_ptr_pair       cpu_atomic_store_128
#define cpu_compare_and_swap_ptr_pair   cpu_compare_and_swap_128
#define CPU_HAVE_ATOMIC_PTRPAIR_OPS     1
#endif  /* CPU_HAVE_DIRECT_ATOMIC_128 */
#endif  /* SIZEOF_VOID_P */

#endif  /* CPU_HAVE_DIRECT_ATOMIC_OPS */

#endif  /* ERL_CPU_FEATURES_H__ */
