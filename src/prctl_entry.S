#if defined(__x86_64__)

    .text

    .globl prctl
    .type prctl, @function

prctl:
    /*
     * SysV x86_64 already gives us:
     *
     * RDI = option
     * RSI = arg2
     * RDX = arg3
     * RCX = arg4
     * R8  = arg5
     *
     * linuwux_prctl_dispatch() uses exactly the same register ABI,
     * so this can be a tail call.
     */
    jmp linuwux_prctl_dispatch@PLT

    .size prctl, .-prctl

#else
#error "LinUwUx prctl interposition currently requires x86_64"
#endif

    .section .note.GNU-stack,"",@progbits
