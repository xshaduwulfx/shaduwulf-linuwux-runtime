#ifndef LINUWUX_CPUID_H
#define LINUWUX_CPUID_H

#include <signal.h>
#include <stdint.h>

int linuwux_cpuid_init(void);
int linuwux_cpuid_enable_faulting(void);

uintptr_t linuwux_cpuid_target_sys_handler(void);

int linuwux_cpuid_handle_sigsegv(
    siginfo_t *info,
    void *ucontext
);

#endif
