#define _GNU_SOURCE

#include "kuser.h"
#include "runtime.h"

#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

static size_t cached_page_size;
static int cached_proton_avx;
static int kuser_initialized;


static void write_u8(
    uint8_t *base,
    size_t offset,
    uint8_t value)
{
    base[offset] = value;
}

static void write_u32(
    uint8_t *base,
    size_t offset,
    uint32_t value)
{
    base[offset + 0] = (uint8_t)(value >> 0);
    base[offset + 1] = (uint8_t)(value >> 8);
    base[offset + 2] = (uint8_t)(value >> 16);
    base[offset + 3] = (uint8_t)(value >> 24);
}

static void write_u64(
    uint8_t *base,
    size_t offset,
    uint64_t value)
{
    unsigned int i;

    for (i = 0; i < 8; i++)
        base[offset + i] = (uint8_t)(value >> (i * 8));
}

static void zero_range(
    uint8_t *base,
    size_t offset,
    size_t length)
{
    size_t i;

    for (i = 0; i < length; i++)
        base[offset + i] = 0;
}

static void copy_bytes(
    uint8_t *base,
    size_t offset,
    const uint8_t *source,
    size_t length)
{
    size_t i;

    for (i = 0; i < length; i++)
        base[offset + i] = source[i];
}

int linuwux_kuser_patch_buffer(
    uint8_t *kuser,
    size_t size,
    int proton_avx)
{
    static const uint8_t system_root[0x104] =
    {
        0x43, 0x00, 0x3a, 0x00,
        0x5c, 0x00, 0x57, 0x00,
        0x69, 0x00, 0x6e, 0x00,
        0x64, 0x00, 0x6f, 0x00,
        0x77, 0x00, 0x73, 0x00
        /* remainder is zero-initialized */
    };

    if (!kuser || size < LINUWUX_KUSER_SIZE)
        return -1;

    copy_bytes(kuser, 0x30, system_root, sizeof(system_root));

    write_u64(kuser, 0x260, UINT64_C(0x0100006658));
    write_u32(kuser, 0x268, UINT32_C(0x090001));
    write_u32(kuser, 0x26c, UINT32_C(0x0a));
    write_u32(kuser, 0x270, UINT32_C(0x00));

    /*
     * ProcessorFeatures.
     */
    write_u32(kuser, 0x274, UINT32_C(0x01010000));
    write_u32(kuser, 0x278, UINT32_C(0x010000));
    write_u32(kuser, 0x27c, UINT32_C(0x010101));
    write_u32(kuser, 0x280, UINT32_C(0x010101));
    write_u32(kuser, 0x284, UINT32_C(0x0100));
    write_u32(kuser, 0x288, UINT32_C(0x01010101));
    write_u32(kuser, 0x28c, UINT32_C(0x0));
    write_u32(kuser, 0x290, UINT32_C(0x01));
    write_u32(kuser, 0x294, UINT32_C(0x01000101));
    write_u32(kuser, 0x298, UINT32_C(0x01010101));
    write_u32(kuser, 0x29c, UINT32_C(0x010001));
    write_u32(kuser, 0x2a0, UINT32_C(0x0));
    write_u32(kuser, 0x2a4, UINT32_C(0x0));
    write_u32(kuser, 0x2a8, UINT32_C(0x0));
    write_u32(kuser, 0x2ac, UINT32_C(0x0));
    write_u32(kuser, 0x2b0, UINT32_C(0x1));

    /*
     * Disable MONITORX, RDTSCP, RDPID and RDRAND.
     */
    write_u8(kuser, 0x290, UINT8_C(0x0));
    write_u8(kuser, 0x294, UINT8_C(0x0));
    write_u8(kuser, 0x295, UINT8_C(0x0));
    write_u8(kuser, 0x297, UINT8_C(0x0));

    if (!proton_avx)
    {
        /*
         * Disable XSAVE, AVX and AVX2.
         */
        write_u8(kuser, 0x285, UINT8_C(0x0));
        write_u8(kuser, 0x29b, UINT8_C(0x0));
        write_u8(kuser, 0x29c, UINT8_C(0x0));
    }

    write_u64(kuser, 0x3d8, UINT64_C(0x0));
    write_u64(kuser, 0x3e0, UINT64_C(0x0));
    write_u32(kuser, 0x3ec, UINT32_C(0x0));

    zero_range(kuser, 0x3f0, 0x200);

    write_u64(kuser, 0x5f0, UINT64_C(0x0));
    write_u64(kuser, 0x5f8, UINT64_C(0x0));

    zero_range(kuser, 0x604, 0x200);

    write_u64(kuser, 0x808, UINT64_C(0x0));
    write_u64(kuser, 0x810, UINT64_C(0x0));

    write_u64(kuser, 0x2d0, UINT64_C(0x320a0000000110));
    write_u64(kuser, 0x2e8, UINT64_C(0x0100007fb10b));
    write_u32(kuser, 0x2f4, UINT32_C(0x0));

    write_u64(kuser, 0x36c, UINT64_C(0x0));
    write_u64(kuser, 0x374, UINT64_C(0x0));
    write_u32(kuser, 0x37c, UINT32_C(0x1));

    write_u64(kuser, 0x3c0, UINT64_C(0x83000100000010));

    write_u32(kuser, 0xffc, UINT32_C(0x13371337));

    return 0;
}

int linuwux_kuser_init(void)
{
    long page_size;
    const char *proton_avx_env;

    page_size = sysconf(_SC_PAGESIZE);

    if (page_size <= 0)
    {
        linuwux_log("KUSER init failed: invalid page size");
        return -1;
    }

    proton_avx_env = getenv("PROTON_AVX");

    cached_proton_avx =
        proton_avx_env &&
        strcmp(proton_avx_env, "1") == 0;

    cached_page_size = (size_t)page_size;
    kuser_initialized = 1;

    linuwux_log(
        "KUSER initialized, page_size=%zu PROTON_AVX=%d",
        cached_page_size,
        cached_proton_avx
    );

    return 0;
}

int linuwux_kuser_patch(void)
{
    uint8_t *kuser;
    uintptr_t page_start;

    if (!kuser_initialized || !cached_page_size)
        return -1;

    kuser = (uint8_t *)(uintptr_t)LINUWUX_KUSER_ADDRESS;

    page_start =
        (uintptr_t)LINUWUX_KUSER_ADDRESS &
        ~((uintptr_t)cached_page_size - 1);

    if (mprotect(
            (void *)page_start,
            cached_page_size,
            PROT_READ | PROT_WRITE) != 0)
    {
        return -1;
    }

    return linuwux_kuser_patch_buffer(
        kuser,
        LINUWUX_KUSER_SIZE,
        cached_proton_avx
    );
}
