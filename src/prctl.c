#define _GNU_SOURCE

#include "sud.h"
#include "runtime.h"

#include <dlfcn.h>
#include <errno.h>
#include <sys/prctl.h>

#ifndef PR_SET_SYSCALL_USER_DISPATCH
#define PR_SET_SYSCALL_USER_DISPATCH 59
#endif

#ifndef PR_SYS_DISPATCH_ON
#define PR_SYS_DISPATCH_ON 1
#endif

typedef int (*prctl_fn)(int, ...);

static prctl_fn real_prctl;

static int resolve_prctl(void)
{
    if (real_prctl)
        return 0;

    real_prctl =
        (prctl_fn)dlsym(
            RTLD_NEXT,
            "prctl"
        );

    if (!real_prctl)
    {
        errno = ENOSYS;
        return -1;
    }

    return 0;
}

/*
 * Called by the x86_64 assembly entry point.
 *
 * The assembly wrapper captures the argument registers directly,
 * avoiding unsafe va_arg reads when a caller supplies fewer
 * optional prctl arguments.
 */
int linuwux_prctl_dispatch(
    int option,
    unsigned long a2,
    unsigned long a3,
    unsigned long a4,
    unsigned long a5)
{
    int ret;

    if (resolve_prctl() != 0)
        return -1;

    if (option == PR_SET_SYSCALL_USER_DISPATCH)
    {
        linuwux_log(
            "SUD prctl: mode=%lu offset=%#lx length=%#lx selector=%p",
            a2,
            a3,
            a4,
            (void *)a5
        );
    }

    ret = real_prctl(
        option,
        a2,
        a3,
        a4,
        a5
    );

    if (ret >= 0 &&
        option == PR_SET_SYSCALL_USER_DISPATCH &&
        a2 == PR_SYS_DISPATCH_ON)
    {
        (void)linuwux_sud_learn_selector(
            (unsigned char *)a5
        );
    }

    return ret;
}
