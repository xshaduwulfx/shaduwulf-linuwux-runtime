#ifndef LINUWUX_KUSER_H
#define LINUWUX_KUSER_H

#include <stddef.h>
#include <stdint.h>

#define LINUWUX_KUSER_ADDRESS UINT64_C(0x000000007ffe0000)
#define LINUWUX_KUSER_SIZE    0x1000

int linuwux_kuser_init(void);

int linuwux_kuser_patch_buffer(
    uint8_t *kuser,
    size_t size,
    int proton_avx
);

int linuwux_kuser_patch(void);

#endif
