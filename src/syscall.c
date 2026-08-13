#define _GNU_SOURCE

#include "syscall.h"
#include "cpuid.h"
#include "runtime.h"
#include "sud.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <ucontext.h>

#define LINUWUX_SYSCALL_BYPASS_MAGIC \
    UINT64_C(0x1337133713371337)

#define LINUWUX_WINE_SYSTEM_RIP_MIN \
    UINT64_C(0x00006fffff000000)

#define LINUWUX_WINE_SYSTEM_RIP_MAX \
    UINT64_C(0x0000700000000000)

static int redirect_all;

static int read_xmm_low64(
    const ucontext_t *ctx,
    unsigned int index,
    uint64_t *value)
{
#if defined(__x86_64__)
    if (!ctx ||
        !ctx->uc_mcontext.fpregs ||
        !value ||
        index >= 16)
    {
        return -1;
    }

    memcpy(
        value,
        &ctx->uc_mcontext.fpregs->_xmm[index],
        sizeof(*value)
    );

    return 0;
#else
    (void)ctx;
    (void)index;
    (void)value;
    return -1;
#endif
}

static int write_xmm_low64(
    ucontext_t *ctx,
    unsigned int index,
    uint64_t value)
{
#if defined(__x86_64__)
    if (!ctx ||
        !ctx->uc_mcontext.fpregs ||
        index >= 16)
    {
        return -1;
    }

    memcpy(
        &ctx->uc_mcontext.fpregs->_xmm[index],
        &value,
        sizeof(value)
    );

    return 0;
#else
    (void)ctx;
    (void)index;
    (void)value;
    return -1;
#endif
}

static int write_xmm_syscall_id(
    ucontext_t *ctx,
    unsigned int index,
    uint32_t value)
{
#if defined(__x86_64__)
    unsigned char zero[16] = {0};

    if (!ctx ||
        !ctx->uc_mcontext.fpregs ||
        index >= 16)
    {
        return -1;
    }

    /*
     * Match LinUwUx:
     *
     *     xmm_regs[4] = syscall_id;
     *
     * This clears the full 128-bit XMM register and leaves
     * the syscall number in its low 32 bits.
     */
    memcpy(
        &ctx->uc_mcontext.fpregs->_xmm[index],
        zero,
        sizeof(zero)
    );

    memcpy(
        &ctx->uc_mcontext.fpregs->_xmm[index],
        &value,
        sizeof(value)
    );

    return 0;
#else
    (void)ctx;
    (void)index;
    (void)value;
    return -1;
#endif
}

static int rip_is_wine_system(
    uintptr_t rip)
{
    return
        rip >= LINUWUX_WINE_SYSTEM_RIP_MIN &&
        rip < LINUWUX_WINE_SYSTEM_RIP_MAX;
}


int linuwux_syscall_init(void)
{
    const char *value;

    value = getenv("LINUWUX_REDIRECT_ALL");

    redirect_all =
        value &&
        value[0] == '1' &&
        value[1] == '\0';

    linuwux_log(
        "SIGSYS redirect scope: %s",
        redirect_all
            ? "all"
            : "guest-only"
    );

    return 0;
}

int linuwux_syscall_handle_sigsys(
    void *ucontext_ptr)
{
#if defined(__x86_64__)
    ucontext_t *ctx;

    uintptr_t target;
    uintptr_t rip;
    uintptr_t resume;

    uint64_t xmm5_low;
    uint64_t syscall_number;

    if (!ucontext_ptr)
        return 0;

    ctx = (ucontext_t *)ucontext_ptr;

    target =
        linuwux_cpuid_target_sys_handler();

    if (read_xmm_low64(
            ctx,
            5,
            &xmm5_low) != 0)
    {
        return 0;
    }

    /*
     * LinUwUx bypass marker.
     *
     * The marked syscall must fall through to Wine's real
     * SIGSYS path. Clear the marker so it applies once only.
     */
    if (xmm5_low ==
        LINUWUX_SYSCALL_BYPASS_MAGIC)
    {
        (void)write_xmm_low64(
            ctx,
            5,
            UINT64_C(0)
        );

        return 0;
    }

    if (!target)
        return 0;

    rip =
        (uintptr_t)
        ctx->uc_mcontext.gregs[REG_RIP];

    /*
     * Do not redirect Wine system-PE faults unless explicitly
     * requested. Guest/game faults remain eligible.
     */
    if (!redirect_all &&
        rip_is_wine_system(rip))
    {
        return 0;
    }

    syscall_number =
        (uint64_t)
        ctx->uc_mcontext.gregs[REG_RAX];

    /*
     * The current LinUwUx trampoline expects RAX to contain the
     * address immediately after the guest syscall instruction.
     *
     * A SUD SIGSYS normally reports RIP at the faulting "syscall"
     * (0f 05), so advance by two bytes in that case. Keep RIP
     * unchanged for other entry forms.
     *
     * The syscall trampoline expects this resume-address convention.
     */
    resume = rip;

    if (((const unsigned char *)rip)[0] == 0x0f &&
        ((const unsigned char *)rip)[1] == 0x05)
    {
        resume = rip + 2;
    }

    /*
     * LinUwUx trampoline ABI:
     *
     * XMM4.low32 = syscall number
     * RAX        = guest resume address
     * RCX        = TargetSysHandler
     * RIP        = TargetSysHandler
     */
    if (write_xmm_syscall_id(
            ctx,
            4,
            (uint32_t)syscall_number) != 0)
    {
        return 0;
    }

    ctx->uc_mcontext.gregs[REG_RAX] =
        (greg_t)resume;

    ctx->uc_mcontext.gregs[REG_RCX] =
        (greg_t)target;

    ctx->uc_mcontext.gregs[REG_RIP] =
        (greg_t)target;

    /*
     * Wine's SUD signal path enters with the selector allowing
     * signal handling. Execution resumed in TargetSysHandler must
     * run with syscall dispatch blocked again.
     */
    linuwux_sud_rearm();

    return 1;
#else
    (void)ucontext_ptr;
    return 0;
#endif
}
