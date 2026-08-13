#ifndef LINUWUX_SYSCALL_H
#define LINUWUX_SYSCALL_H

int linuwux_syscall_init(void);

int linuwux_syscall_handle_sigsys(
    void *ucontext
);

#endif
