#define _GNU_SOURCE

#include "cpuid.h"
#include "runtime.h"
#include "kuser.h"
#include "time.h"

#include <asm/prctl.h>
#include <errno.h>
#include <stdint.h>
#include <sys/syscall.h>
#include <ucontext.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>

static uint32_t spoof_leaf1_eax;
static uint32_t spoof_leaf1_ebx;
static uint32_t spoof_leaf1_ecx;
static uint32_t spoof_leaf1_edx;

static uint32_t spoof_leaf40000000_eax;
static uint32_t spoof_leaf40000000_ebx;
static uint32_t spoof_leaf40000000_ecx;
static uint32_t spoof_leaf40000000_edx;

static uint32_t spoof_leaf40000001_eax;
static uint32_t spoof_leaf40000001_ebx;
static uint32_t spoof_leaf40000001_ecx;
static uint32_t spoof_leaf40000001_edx;

static int spoof_leaf1_ready;

/*
 * Game-side syscall redirection target announced through
 * LinUwUx magic CPUID leaf 0x336933.
 */
static uintptr_t target_sys_handler;

#ifndef ARCH_SET_CPUID
#define ARCH_SET_CPUID 0x1012
#endif

static int set_cpuid_enabled(unsigned long enabled)
{
    return (int)syscall(
        SYS_arch_prctl,
        ARCH_SET_CPUID,
        enabled
    );
}

int linuwux_cpuid_init(void)
{
    uint32_t eax;
    uint32_t ebx;
    uint32_t ecx;
    uint32_t edx;
    const char *proton_avx;
    int avx;

    /*
     * CPUID faulting is not enabled yet when this runs, so use
     * a normal native CPUID to identify the host vendor.
     */
    __asm__ volatile(
        "cpuid"
        : "=a"(eax),
          "=b"(ebx),
          "=c"(ecx),
          "=d"(edx)
        : "a"(0),
          "c"(0)
        : "memory"
    );

    proton_avx = getenv("PROTON_AVX");
    avx = proton_avx && strcmp(proton_avx, "1") == 0;

    if (ebx == 0x756e6547 &&
        edx == 0x49656e69 &&
        ecx == 0x6c65746e)
    {
        /* GenuineIntel */
        spoof_leaf1_eax = 0x000a0655;
        spoof_leaf1_ebx = 0x00200800;
        spoof_leaf1_ecx = avx ? 0x7bfafbff : 0x01faebff;
        spoof_leaf1_edx = 0xbfebfbff;

        spoof_leaf40000000_eax = 0x40000001;
        spoof_leaf40000000_ebx = 0x65707948;
        spoof_leaf40000000_ecx = 0x67624472;
        spoof_leaf40000000_edx = 0x00000000;

        spoof_leaf40000001_eax = 0x30237648;
        spoof_leaf40000001_ebx = 0x00000000;
        spoof_leaf40000001_ecx = 0x00000000;
        spoof_leaf40000001_edx = 0x00000000;

        spoof_leaf1_ready = 1;
        linuwux_log(
            "CPUID spoof initialized: GenuineIntel, PROTON_AVX=%d",
            avx
        );

        return 0;
    }

    if (ebx == 0x68747541 &&
        edx == 0x69746e65 &&
        ecx == 0x444d4163)
    {
        /* AuthenticAMD */
        spoof_leaf1_eax = 0x00a20f12;
        spoof_leaf1_ebx = 0x00100800;
        spoof_leaf1_ecx = avx ? 0x7ad8320b : 0x00f8220b;
        spoof_leaf1_edx = 0x178bfbff;

        spoof_leaf40000000_eax = 0x40000001;
        spoof_leaf40000000_ebx = 0x706d6953;
        spoof_leaf40000000_ecx = 0x7653656c;
        spoof_leaf40000000_edx = 0x2020206d;

        spoof_leaf40000001_eax = 0x30237648;
        spoof_leaf40000001_ebx = 0x00000000;
        spoof_leaf40000001_ecx = 0x00000000;
        spoof_leaf40000001_edx = 0x00000000;

        spoof_leaf1_ready = 1;
        linuwux_log(
            "CPUID spoof initialized: AuthenticAMD, PROTON_AVX=%d",
            avx
        );

        return 0;
    }

    spoof_leaf1_ready = 0;
    linuwux_log("CPUID spoof disabled: unsupported CPU vendor");

    return -1;
}

uintptr_t linuwux_cpuid_target_sys_handler(void)
{
    return target_sys_handler;
}

int linuwux_cpuid_enable_faulting(void)
{
    if (set_cpuid_enabled(0) != 0)
    {
        linuwux_log(
            "ARCH_SET_CPUID faulting unavailable: errno=%d",
            errno
        );

        return -1;
    }

    linuwux_log("CPUID faulting enabled");
    return 0;
}

static int native_cpuid(
    uint32_t leaf,
    uint32_t subleaf,
    uint32_t *eax,
    uint32_t *ebx,
    uint32_t *ecx,
    uint32_t *edx)
{
    /*
     * CPUID is disabled for this thread while handling the fault.
     * Temporarily enable it, execute one native CPUID, then disable it
     * again before returning to the faulting context.
     */
    if (set_cpuid_enabled(1) != 0)
        return -1;

    __asm__ volatile(
        "cpuid"
        : "=a"(*eax),
          "=b"(*ebx),
          "=c"(*ecx),
          "=d"(*edx)
        : "a"(leaf),
          "c"(subleaf)
        : "memory"
    );

    if (set_cpuid_enabled(0) != 0)
        return -1;

    return 0;
}

int linuwux_cpuid_handle_sigsegv(
    siginfo_t *info,
    void *ucontext_ptr)
{
#if defined(__x86_64__)
    ucontext_t *ucontext;
    greg_t *gregs;
    const unsigned char *rip;
    uint32_t leaf;
    uint32_t subleaf;
    uint32_t eax;
    uint32_t ebx;
    uint32_t ecx;
    uint32_t edx;

    (void)info;

    if (!ucontext_ptr)
        return 0;

    ucontext = (ucontext_t *)ucontext_ptr;
    gregs = ucontext->uc_mcontext.gregs;

    rip = (const unsigned char *)(uintptr_t)gregs[REG_RIP];

    /*
     * x86 CPUID opcode: 0f a2
     */
    if (!rip || rip[0] != 0x0f || rip[1] != 0xa2)
        return 0;

    leaf = (uint32_t)gregs[REG_RAX];
    subleaf = (uint32_t)gregs[REG_RCX];

    /*
     * ARCH_SET_CPUID faults normally arrive from the kernel.
     * Keep the original LinUwUx exception for the magic registration
     * leaf, which must be recognized independently of si_code.
     */
    if (leaf != 0x336933 &&
        (!info || info->si_code != SI_KERNEL))
    {
        return 0;
    }

    /*
     * Match the original LinUwUx CPUID(1) behavior.
     *
     * Keep the hypervisor-present bit set until TargetSysHandler is
     * armed through the 0x336933 handshake.
     */
    if (leaf == 1 && spoof_leaf1_ready)
    {
        gregs[REG_RAX] = spoof_leaf1_eax;
        gregs[REG_RBX] = spoof_leaf1_ebx;
        gregs[REG_RCX] =
            spoof_leaf1_ecx |
            (target_sys_handler ? 0 : (1u << 31));
        gregs[REG_RDX] = spoof_leaf1_edx;

        gregs[REG_RIP] += 2;
        return 1;
    }

    if (leaf == 0x336933)
    {
        /*
         * LinUwUx registration handshake.
         *
         * RCX carries the game-side syscall handler address.
         */
        target_sys_handler = (uintptr_t)gregs[REG_RCX];

        /*
         * Match the original LinUwUx behavior: patch
         * KUSER_SHARED_DATA when the syscall handler is registered.
         */
        (void)linuwux_kuser_patch();

        gregs[REG_RAX] = 0;
        gregs[REG_RBX] = 0;
        gregs[REG_RCX] = 0;
        gregs[REG_RDX] = 0;

        gregs[REG_RIP] += 2;
        return 1;
    }

    if (leaf == 0x336967)
    {
        /*
         * LinUwUx faketime handshake.
         *
         * RCX carries the requested high 32-bit Windows-time value.
         */
        (void)linuwux_time_set_faketime(
            (int64_t)gregs[REG_RCX]
        );

        gregs[REG_RAX] = 0;
        gregs[REG_RBX] = 0;
        gregs[REG_RCX] = 0;
        gregs[REG_RDX] = 0;

        gregs[REG_RIP] += 2;
        return 1;
    }

    if (leaf == 0x40000000 && spoof_leaf1_ready)
    {
        gregs[REG_RAX] = spoof_leaf40000000_eax;
        gregs[REG_RBX] = spoof_leaf40000000_ebx;
        gregs[REG_RCX] = spoof_leaf40000000_ecx;
        gregs[REG_RDX] = spoof_leaf40000000_edx;

        gregs[REG_RIP] += 2;
        return 1;
    }

    if (leaf == 0x40000001 && spoof_leaf1_ready)
    {
        gregs[REG_RAX] = spoof_leaf40000001_eax;
        gregs[REG_RBX] = spoof_leaf40000001_ebx;
        gregs[REG_RCX] = spoof_leaf40000001_ecx;
        gregs[REG_RDX] = spoof_leaf40000001_edx;

        gregs[REG_RIP] += 2;
        return 1;
    }

    if (leaf == 0x80000002)
    {
        gregs[REG_RAX] = 0x756e6544;
        gregs[REG_RBX] = 0x4f774f76;
        gregs[REG_RCX] = 0x55504320;
        gregs[REG_RDX] = 0x31204020;

        gregs[REG_RIP] += 2;
        return 1;
    }

    if (leaf == 0x80000003)
    {
        gregs[REG_RAX] = 0x20373333;
        gregs[REG_RBX] = 0x007a4847;
        gregs[REG_RCX] = 0x00000000;
        gregs[REG_RDX] = 0x00000000;

        gregs[REG_RIP] += 2;
        return 1;
    }

    if (leaf == 0x80000004)
    {
        gregs[REG_RAX] = 0x00000000;
        gregs[REG_RBX] = 0x00000000;
        gregs[REG_RCX] = 0x00000000;
        gregs[REG_RDX] = 0x00000000;

        gregs[REG_RIP] += 2;
        return 1;
    }

    if (native_cpuid(
            leaf,
            subleaf,
            &eax,
            &ebx,
            &ecx,
            &edx) != 0)
    {
        return 0;
    }

    gregs[REG_RAX] = eax;
    gregs[REG_RBX] = ebx;
    gregs[REG_RCX] = ecx;
    gregs[REG_RDX] = edx;

    /*
     * Skip the faulting CPUID instruction.
     */
    gregs[REG_RIP] += 2;

    return 1;
#else
    (void)info;
    (void)ucontext_ptr;

    return 0;
#endif
}
