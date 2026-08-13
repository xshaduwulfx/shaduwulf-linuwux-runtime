#define _GNU_SOURCE

#include "sud.h"
#include "runtime.h"

#include <stdint.h>

#if defined(__x86_64__)

static long sud_teb_offset = -1;

static unsigned char *linuwux_get_teb(void)
{
    unsigned char *teb;

    /*
     * Wine x86_64 keeps the current TEB at GS:0x30.
     */
    __asm__ volatile(
        "movq %%gs:0x30, %0"
        : "=r"(teb)
    );

    return teb;
}

int linuwux_sud_init(void)
{
    sud_teb_offset = -1;
    return 0;
}

int linuwux_sud_learn_selector(
    unsigned char *selector)
{
    unsigned char *teb;

    if (!selector)
        return -1;

    teb = linuwux_get_teb();

    if (!teb)
        return -1;

    sud_teb_offset =
        (long)(selector - teb);

    linuwux_log(
        "SUD selector learned: teb_offset=%#lx teb=%p selector=%p",
        sud_teb_offset,
        (void *)teb,
        (void *)selector
    );

    return 0;
}

void linuwux_sud_rearm(void)
{
    unsigned char *teb;

    if (sud_teb_offset < 0)
        return;

    teb = linuwux_get_teb();

    if (!teb)
        return;

    teb[sud_teb_offset] = 1;
}

long linuwux_sud_teb_offset(void)
{
    return sud_teb_offset;
}

#else

int linuwux_sud_init(void)
{
    return -1;
}

int linuwux_sud_learn_selector(
    unsigned char *selector)
{
    (void)selector;
    return -1;
}

void linuwux_sud_rearm(void)
{
}

long linuwux_sud_teb_offset(void)
{
    return -1;
}

#endif
