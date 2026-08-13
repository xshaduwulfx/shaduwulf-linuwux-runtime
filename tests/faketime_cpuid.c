#define _GNU_SOURCE

#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#define TICKS_PER_SECOND UINT64_C(10000000)
#define TICKS_1601_TO_1970 UINT64_C(116444736000000000)

static void dummy_segv_handler(
    int signal,
    siginfo_t *info,
    void *context)
{
    (void)signal;
    (void)info;
    (void)context;

    /*
     * A real unexpected SIGSEGV is a test failure.
     * LinUwUx should consume the CPUID fault before this handler.
     */
    _Exit(100);
}

int main(void)
{
    struct sigaction action;
    struct timespec now;
    uint64_t ticks;
    uint32_t current_high;
    uint32_t requested_high;
    uint32_t eax;
    uint32_t ebx;
    uint32_t ecx;
    uint32_t edx;

    memset(&action, 0, sizeof(action));

    action.sa_sigaction = dummy_segv_handler;
    action.sa_flags = SA_SIGINFO;

    sigemptyset(&action.sa_mask);

    /*
     * This call is intentional: liblinuwux interposes sigaction().
     * Registering SIGSEGV causes the runtime to initialize its CPUID
     * handler and enable ARCH_SET_CPUID faulting for this thread.
     */
    if (sigaction(SIGSEGV, &action, NULL) != 0)
    {
        perror("sigaction");
        return 1;
    }

    if (clock_gettime(CLOCK_REALTIME, &now) != 0)
    {
        perror("clock_gettime");
        return 1;
    }

    ticks =
        (uint64_t)now.tv_sec * TICKS_PER_SECOND +
        (uint64_t)now.tv_nsec / 100 +
        TICKS_1601_TO_1970;

    current_high =
        (uint32_t)(ticks >> 32);

    /*
     * Make the expected LinUwUx offset deterministic:
     *
     *   (current_high - requested_high) << 32
     * = 1 << 32 ticks
     * = 429.4967296 seconds.
     */
    requested_high =
        current_high - UINT32_C(1);

    printf(
        "current_high=0x%08x\n"
        "requested_high=0x%08x\n",
        current_high,
        requested_high
    );

    eax = UINT32_C(0x336967);
    ecx = requested_high;

    __asm__ volatile(
        "cpuid"
        : "+a"(eax),
          "=b"(ebx),
          "+c"(ecx),
          "=d"(edx)
    );

    printf(
        "first returned  eax=%08x ebx=%08x ecx=%08x edx=%08x\n",
        eax,
        ebx,
        ecx,
        edx
    );

    /*
     * Under the original LinUwUx wineserver semantics, the first
     * request moves current_time down by one high-DWORD unit.
     *
     * Requesting one further unit behind that already-faked time
     * should therefore leave the resulting offset at 1 << 32,
     * not increase it to 2 << 32.
     */
    requested_high =
        current_high - UINT32_C(2);

    printf(
        "second requested_high=0x%08x\n",
        requested_high
    );

    eax = UINT32_C(0x336967);
    ecx = requested_high;

    __asm__ volatile(
        "cpuid"
        : "+a"(eax),
          "=b"(ebx),
          "+c"(ecx),
          "=d"(edx)
    );

    printf(
        "second returned eax=%08x ebx=%08x ecx=%08x edx=%08x\n",
        eax,
        ebx,
        ecx,
        edx
    );

    return 0;
}
