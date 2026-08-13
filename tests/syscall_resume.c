#define _GNU_SOURCE

#include "../src/syscall.h"

#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ucontext.h>

static void dummy_segv_handler(
    int signal,
    siginfo_t *info,
    void *context)
{
    (void)signal;
    (void)info;
    (void)context;

    _Exit(100);
}

int main(void)
{
    static unsigned char syscall_insn[] =
    {
        0x0f, 0x05, 0x90, 0x90
    };

    const uintptr_t target =
        UINT64_C(0x12345000);

    const uint64_t rcx_sentinel =
        UINT64_C(0x1111222233334444);

    const uint64_t syscall_number =
        UINT64_C(0x123);

    struct sigaction action;
    ucontext_t ctx;
    struct _libc_fpstate fpstate;

    uint32_t eax;
    uint32_t ebx;
    uint32_t ecx;
    uint32_t edx;

    uint64_t xmm4_low = 0;
    uintptr_t rip;
    uintptr_t expected_resume;
    int handled;

    memset(&action, 0, sizeof(action));

    action.sa_sigaction = dummy_segv_handler;
    action.sa_flags = SA_SIGINFO;

    sigemptyset(&action.sa_mask);

    /*
     * liblinuwux interposes sigaction(). This initializes CPUID
     * faulting for the thread before the synthetic arm leaf below.
     */
    if (sigaction(SIGSEGV, &action, NULL) != 0)
    {
        perror("sigaction");
        return 1;
    }

    /*
     * Arm TargetSysHandler through the real LinUwUx CPUID protocol.
     * Keep the synthetic target below 4 GiB because CPUID ECX is
     * a 32-bit input register.
     */
    eax = UINT32_C(0x336933);
    ecx = (uint32_t)target;

    __asm__ volatile(
        "cpuid"
        : "+a"(eax),
          "=b"(ebx),
          "+c"(ecx),
          "=d"(edx)
    );

    printf(
        "arm returned eax=%08x ebx=%08x ecx=%08x edx=%08x\n",
        eax,
        ebx,
        ecx,
        edx
    );

    memset(&ctx, 0, sizeof(ctx));
    memset(&fpstate, 0, sizeof(fpstate));

    ctx.uc_mcontext.fpregs =
        &fpstate;

    rip =
        (uintptr_t)syscall_insn;

    expected_resume =
        rip + 2;

    ctx.uc_mcontext.gregs[REG_RIP] =
        (greg_t)rip;

    ctx.uc_mcontext.gregs[REG_RAX] =
        (greg_t)syscall_number;

    ctx.uc_mcontext.gregs[REG_RCX] =
        (greg_t)rcx_sentinel;

    handled =
        linuwux_syscall_handle_sigsys(&ctx);

    memcpy(
        &xmm4_low,
        &fpstate._xmm[4],
        sizeof(xmm4_low)
    );

    printf("handled       = %d\n", handled);
    printf(
        "input RIP      = 0x%016llx\n",
        (unsigned long long)rip
    );
    printf(
        "input RCX      = 0x%016llx\n",
        (unsigned long long)rcx_sentinel
    );
    printf(
        "expected resume = 0x%016llx\n",
        (unsigned long long)expected_resume
    );

    printf(
        "output RAX     = 0x%016llx\n",
        (unsigned long long)
            ctx.uc_mcontext.gregs[REG_RAX]
    );

    printf(
        "output RCX     = 0x%016llx\n",
        (unsigned long long)
            ctx.uc_mcontext.gregs[REG_RCX]
    );

    printf(
        "output RIP     = 0x%016llx\n",
        (unsigned long long)
            ctx.uc_mcontext.gregs[REG_RIP]
    );

    printf(
        "XMM4.low64     = 0x%016llx\n",
        (unsigned long long)xmm4_low
    );

    if (handled != 1)
    {
        fprintf(
            stderr,
            "FAIL: SIGSYS was not redirected\n"
        );

        return 1;
    }

    if ((uintptr_t)ctx.uc_mcontext.gregs[REG_RCX] !=
        target)
    {
        fprintf(
            stderr,
            "FAIL: RCX does not contain TargetSysHandler\n"
        );

        return 1;
    }

    if ((uintptr_t)ctx.uc_mcontext.gregs[REG_RIP] !=
        target)
    {
        fprintf(
            stderr,
            "FAIL: RIP does not contain TargetSysHandler\n"
        );

        return 1;
    }

    if ((uint32_t)xmm4_low !=
        (uint32_t)syscall_number)
    {
        fprintf(
            stderr,
            "FAIL: XMM4 syscall number mismatch\n"
        );

        return 1;
    }

    if ((uint64_t)ctx.uc_mcontext.gregs[REG_RAX] ==
        rcx_sentinel)
    {
        printf(
            "RESULT: current runtime uses original LinUwUx "
            "RAX=old-RCX semantics\n"
        );
    }
    else if ((uintptr_t)ctx.uc_mcontext.gregs[REG_RAX] ==
             expected_resume)
    {
        printf(
            "RESULT: runtime uses "
            "RAX=RIP+2 syscall resume semantics\n"
        );
    }
    else
    {
        fprintf(
            stderr,
            "FAIL: unexpected RAX value\n"
        );

        return 1;
    }

    /*
     * Test the one-shot XMM5 bypass marker.
     *
     * A marked syscall must not redirect to TargetSysHandler.
     * The marker itself must be cleared so the bypass applies
     * only once.
     */
    {
        const uint64_t bypass_magic =
            UINT64_C(0x1337133713371337);

        ucontext_t bypass_ctx;
        struct _libc_fpstate bypass_fpstate;

        uint64_t bypass_xmm5 = 0;
        int bypass_handled;

        memset(
            &bypass_ctx,
            0,
            sizeof(bypass_ctx)
        );

        memset(
            &bypass_fpstate,
            0,
            sizeof(bypass_fpstate)
        );

        bypass_ctx.uc_mcontext.fpregs =
            &bypass_fpstate;

        bypass_ctx.uc_mcontext.gregs[REG_RIP] =
            (greg_t)rip;

        bypass_ctx.uc_mcontext.gregs[REG_RAX] =
            (greg_t)syscall_number;

        bypass_ctx.uc_mcontext.gregs[REG_RCX] =
            (greg_t)rcx_sentinel;

        memcpy(
            &bypass_fpstate._xmm[5],
            &bypass_magic,
            sizeof(bypass_magic)
        );

        bypass_handled =
            linuwux_syscall_handle_sigsys(
                &bypass_ctx
            );

        memcpy(
            &bypass_xmm5,
            &bypass_fpstate._xmm[5],
            sizeof(bypass_xmm5)
        );

        printf("\n");
        printf(
            "bypass handled = %d\n",
            bypass_handled
        );

        printf(
            "bypass XMM5    = 0x%016llx\n",
            (unsigned long long)bypass_xmm5
        );

        printf(
            "bypass RAX     = 0x%016llx\n",
            (unsigned long long)
                bypass_ctx.uc_mcontext.gregs[REG_RAX]
        );

        printf(
            "bypass RCX     = 0x%016llx\n",
            (unsigned long long)
                bypass_ctx.uc_mcontext.gregs[REG_RCX]
        );

        printf(
            "bypass RIP     = 0x%016llx\n",
            (unsigned long long)
                bypass_ctx.uc_mcontext.gregs[REG_RIP]
        );

        if (bypass_handled != 0)
        {
            fprintf(
                stderr,
                "FAIL: bypass syscall was redirected\n"
            );

            return 1;
        }

        if (bypass_xmm5 != 0)
        {
            fprintf(
                stderr,
                "FAIL: XMM5 bypass marker was not cleared\n"
            );

            return 1;
        }

        if ((uint64_t)bypass_ctx.uc_mcontext.gregs[REG_RAX] !=
            syscall_number)
        {
            fprintf(
                stderr,
                "FAIL: bypass changed RAX\n"
            );

            return 1;
        }

        if ((uint64_t)bypass_ctx.uc_mcontext.gregs[REG_RCX] !=
            rcx_sentinel)
        {
            fprintf(
                stderr,
                "FAIL: bypass changed RCX\n"
            );

            return 1;
        }

        if ((uintptr_t)bypass_ctx.uc_mcontext.gregs[REG_RIP] !=
            rip)
        {
            fprintf(
                stderr,
                "FAIL: bypass changed RIP\n"
            );

            return 1;
        }

        printf(
            "PASS: XMM5 bypass marker falls through once and is cleared\n"
        );
    }

    return 0;
}
