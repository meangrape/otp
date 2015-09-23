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

#include "erl_cpu_features.h"

#undef  DECLARE_CPUIDENT_VARS
#if CPU_ARCH_X86 && (CPU_HAVE_GCC_INTRINSICS || CPU_HAVE_MSVC_INTRINSICS)
#define IDENTIFY_X86_FEATURES   1
#if     CPU_HAVE_GCC_INTRINSICS
#include <cpuid.h>
#undef  EAX
#undef  EBX
#undef  ECX
#undef  EDX
#define DECLARE_CPUIDENT_VARS   Uint32  EAX, EBX, ECX, EDX;
#define CPU_GET_CPUID(op) __cpuid(op, EAX, EBX, ECX, EDX)
#define CPU_GET_CPUIDX(op, xop) __cpuid_count(op, xop, EAX, EBX, ECX, EDX)
#elif   CPU_HAVE_MSVC_INTRINSICS
#pragma intrinsic(__cpuid)
#define DECLARE_CPUIDENT_VARS   Uint32  regs[4];
#define EAX regs[0]
#define EBX regs[1]
#define ECX regs[2]
#define EDX regs[3]
#define CPU_GET_CPUID(op) __cpuid(((int *) regs), ((int) op))
#define CPU_GET_CPUIDX(op, xop) __cpuidex(((int *) regs), ((int) op), ((int) xop))
#endif
#define REG1    EAX
#define REG2    EBX
#else
#undef  IDENTIFY_X86_FEATURES
#endif

erts_cpu_features_t erts_cpu_features;

void erts_init_cpu_features(void)
{
    DECLARE_CPUIDENT_VARS

#if CPU_ARCH_X86
    erts_cpu_features |= ERTS_CPU_ARCH_X86;
#if CPU_ARCH_X86_64
    erts_cpu_features |=
        (ERTS_CPU_ARCH_X86_64|ERTS_CPU_FEAT_64_BIT|ERTS_CPU_FEAT_X86_CPUID);
#endif  /* CPU_ARCH_X86_64 */

#if IDENTIFY_X86_FEATURES
#if !CPU_ARCH_X86_64
    /*
     * if the value of flag bit 21 can be toggled, cpuid is supported
     *
     * it's unclear what effect leaving the flag's value changed might have,
     * so make sure it has its original value in all cases when done
     */
#define CPUID_FLAG_BIT  (1u << 21)
#if     CPU_HAVE_GCC_ASM
    __asm__(    "pushfl"
        "\n\t"  "pushfl"
        "\n\t"  "popl    %0"
        "\n\t"  "movl    %0, %1"
        "\n\t"  "xorl    %2, %0"
        "\n\t"  "pushl   %0"
        "\n\t"  "popfl"
        "\n\t"  "pushfl"
        "\n\t"  "popl    %0"
        "\n\t"  "xorl    %1, %0"
        "\n\t"  "popfl"
        : "=&r" (REG1), "=&r" (REG2) : "i" (CPUID_FLAG_BIT)
    );
#elif   CPU_HAVE_MSVC_ASM
    __asm {
        pushfd
        pushfd
        pop     eax
        mov     ebx, eax
        xor     eax, CPUID_FLAG_BIT
        push    eax
        popfd
        pushfd
        pop     eax
        xor     eax, ebx
        popfd
        mov     REG1, eax
    };
#else
#error  Missing CPU_HAVE_xxx_ASM
#endif  /* CPU_HAVE_xxx_ASM */
#undef  CPUID_FLAG_BIT
    if (! REG1)
        goto x86_done;

    erts_cpu_features |= ERTS_CPU_FEAT_X86_CPUID;
#endif  /* ! CPU_ARCH_X86_64 */

#define CPUID_EBX_AMD   0x68747541u
#define CPUID_ECX_AMD   0x444d4163u
#define CPUID_EDX_AMD   0x69746e65u
#define CPUID_EBX_INTEL 0x756e6547u
#define CPUID_ECX_INTEL 0x6c65746eu
#define CPUID_EDX_INTEL 0x49656e69u

    CPU_GET_CPUID(0);
    if (EBX == CPUID_EBX_INTEL && ECX == CPUID_ECX_INTEL && EDX == CPUID_EDX_INTEL)
        erts_cpu_features |= ERTS_CPU_VEND_INTEL;
    else if (EBX == CPUID_EBX_AMD && ECX == CPUID_ECX_AMD && EDX == CPUID_EDX_AMD)
        erts_cpu_features |= ERTS_CPU_VEND_AMD;

#undef  CPUID_EBX_AMD
#undef  CPUID_ECX_AMD
#undef  CPUID_EDX_AMD
#undef  CPUID_EBX_INTEL
#undef  CPUID_ECX_INTEL
#undef  CPUID_EDX_INTEL

    if (EAX < 1u)
        goto x86_test_ext;
/*
x86_test_std_1:
*/
    CPU_GET_CPUID(1);
    if (ECX & (1u << 13))
        erts_cpu_features |= (ERTS_CPU_FEAT_X86_CX16|ERTS_CPU_FEAT_ATOMIC_128);
    if (ECX & (1u << 20))
        erts_cpu_features |= ERTS_CPU_FEAT_X86_CRC32;
    if (ECX & (1u << 25))
        erts_cpu_features |= ERTS_CPU_FEAT_X86_AES;
    if (ECX & (1u << 30))
        erts_cpu_features |= ERTS_CPU_FEAT_X86_RAND;
    if (EDX & (1u << 4))
        erts_cpu_features |= ERTS_CPU_FEAT_X86_TSC;
    if (EDX & (1u << 8))
        erts_cpu_features |= ERTS_CPU_FEAT_X86_CX8;
    if (EDX & (1u << 15))
        erts_cpu_features |= ERTS_CPU_FEAT_X86_CMOV;

x86_test_ext:

    CPU_GET_CPUID(0x80000000u);
    if (EAX >= 0x80000007u)
        goto x86_test_ext7;
    if (EAX >= 0x80000001u)
        goto x86_test_ext1;
    goto x86_done;

x86_test_ext7:

    CPU_GET_CPUIDX(0x80000007u, 0);
    if (EDX & (1u << 8))
         erts_cpu_features |= ERTS_CPU_FEAT_X86_TSCS;

x86_test_ext1:

    CPU_GET_CPUID(0x80000001u);
    if (EDX & (1u << 27))
        erts_cpu_features |= ERTS_CPU_FEAT_X86_TSCP;
#if !CPU_ARCH_X86_64
    if (EDX & (1u << 29))
        erts_cpu_features |= (ERTS_CPU_ARCH_X86_64|ERTS_CPU_FEAT_64_BIT);
#endif

#endif  /* IDENTIFY_X86_FEATURES */
x86_done:
#endif  /* CPU_ARCH_X86 */

    erts_cpu_features |= ERTS_CPU_FEAT_INITIALIZED;
}
