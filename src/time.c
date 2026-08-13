#define _GNU_SOURCE

#include "time.h"
#include "runtime.h"

#include <dlfcn.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#define LINUWUX_TICKS_PER_SECOND \
    INT64_C(10000000)

#define LINUWUX_TICKS_1601_TO_1970 \
    INT64_C(116444736000000000)

typedef int (*clock_gettime_fn)(
    clockid_t,
    struct timespec *
);

typedef int (*gettimeofday_fn)(
    struct timeval *,
    void *
);

static clock_gettime_fn real_clock_gettime_fn;
static gettimeofday_fn real_gettimeofday_fn;

static _Atomic int64_t faketime_offset;
static _Atomic int faketime_active;

#define LINUWUX_FAKETIME_SHM_MAGIC \
    UINT32_C(0x4c575446)

#define LINUWUX_FAKETIME_SHM_VERSION \
    UINT32_C(1)

struct linuwux_faketime_shared
{
    uint32_t magic;
    uint32_t version;
    int64_t offset;
    uint32_t active;
    uint32_t reserved;
};

static struct linuwux_faketime_shared *shared_faketime;
static int current_process_is_wineserver;

static uint64_t linuwux_hash_prefix(
    const char *prefix)
{
    uint64_t hash =
        UINT64_C(1469598103934665603);

    size_t length;
    size_t i;

    if (!prefix)
        return 0;

    length = strlen(prefix);

    /*
     * Treat a prefix with or without trailing slashes as the
     * same Wine prefix.
     */
    while (length > 1 &&
           prefix[length - 1] == '/')
    {
        length--;
    }

    for (i = 0; i < length; i++)
    {
        hash ^= (unsigned char)prefix[i];
        hash *= UINT64_C(1099511628211);
    }

    return hash;
}

static int linuwux_detect_wineserver(void)
{
    char path[4096];
    const char *name;
    ssize_t length;

    length =
        readlink(
            "/proc/self/exe",
            path,
            sizeof(path) - 1
        );

    if (length <= 0)
        return 0;

    path[length] = '\0';

    name = strrchr(path, '/');

    if (name)
        name++;
    else
        name = path;

    return strcmp(name, "wineserver") == 0;
}

static int linuwux_shared_faketime_init(void)
{
    const char *prefix;
    struct stat st;
    char name[128];
    uint64_t hash;
    int fd;
    int initialize = 0;
    void *mapping;

    prefix = getenv("WINEPREFIX");

    if (!prefix || !*prefix)
        return -1;

    hash = linuwux_hash_prefix(prefix);

    if (snprintf(
            name,
            sizeof(name),
            "/linuwux-faketime-%lu-%016llx",
            (unsigned long)getuid(),
            (unsigned long long)hash
        ) >= (int)sizeof(name))
    {
        return -1;
    }

    fd =
        shm_open(
            name,
            O_CREAT | O_RDWR,
            0600
        );

    if (fd < 0)
        return -1;

    if (flock(fd, LOCK_EX) != 0)
    {
        close(fd);
        return -1;
    }

    if (fstat(fd, &st) != 0)
    {
        (void)flock(fd, LOCK_UN);
        close(fd);
        return -1;
    }

    if (st.st_size !=
        (off_t)sizeof(struct linuwux_faketime_shared))
    {
        if (ftruncate(
                fd,
                sizeof(struct linuwux_faketime_shared)
            ) != 0)
        {
            (void)flock(fd, LOCK_UN);
            close(fd);
            return -1;
        }

        initialize = 1;
    }

    mapping =
        mmap(
            NULL,
            sizeof(struct linuwux_faketime_shared),
            PROT_READ | PROT_WRITE,
            MAP_SHARED,
            fd,
            0
        );

    if (mapping == MAP_FAILED)
    {
        (void)flock(fd, LOCK_UN);
        close(fd);
        return -1;
    }

    shared_faketime = mapping;

    if (initialize ||
        shared_faketime->magic !=
            LINUWUX_FAKETIME_SHM_MAGIC ||
        shared_faketime->version !=
            LINUWUX_FAKETIME_SHM_VERSION)
    {
        memset(
            shared_faketime,
            0,
            sizeof(*shared_faketime)
        );

        shared_faketime->magic =
            LINUWUX_FAKETIME_SHM_MAGIC;

        shared_faketime->version =
            LINUWUX_FAKETIME_SHM_VERSION;
    }

    current_process_is_wineserver =
        linuwux_detect_wineserver();

    /*
     * A new wineserver starts a new Wine session for this
     * prefix. Do not inherit faketime from an older session.
     */
    if (current_process_is_wineserver)
    {
        __atomic_store_n(
            &shared_faketime->offset,
            INT64_C(0),
            __ATOMIC_RELEASE
        );

        __atomic_store_n(
            &shared_faketime->active,
            UINT32_C(0),
            __ATOMIC_RELEASE
        );
    }

    (void)flock(fd, LOCK_UN);
    close(fd);

    return 0;
}

static int linuwux_get_local_faketime(
    int64_t *offset)
{
    if (!atomic_load(&faketime_active))
        return 0;

    *offset =
        atomic_load(&faketime_offset);

    return 1;
}

static int linuwux_get_gettimeofday_faketime(
    int64_t *offset)
{
    /*
     * Only the wineserver consumes the prefix-shared value.
     * This mirrors the original LinUwUx design, where faketime
     * changed wineserver current_time rather than every Wine
     * process's libc clock.
     */
    if (current_process_is_wineserver &&
        shared_faketime &&
        __atomic_load_n(
            &shared_faketime->active,
            __ATOMIC_ACQUIRE
        ))
    {
        *offset =
            __atomic_load_n(
                &shared_faketime->offset,
                __ATOMIC_ACQUIRE
            );

        return 1;
    }

    return linuwux_get_local_faketime(offset);
}

/*
 * Return the faketime currently published for this Wine prefix.
 *
 * Unlike the libc interposition path, this is intentionally usable
 * from every Wine process. A subsequent 0x336967 handshake must be
 * calculated from the already-faked wineserver time even when the
 * new request comes from a different process.
 */
static int linuwux_get_published_faketime(
    int64_t *offset)
{
    if (shared_faketime &&
        __atomic_load_n(
            &shared_faketime->active,
            __ATOMIC_ACQUIRE
        ))
    {
        *offset =
            __atomic_load_n(
                &shared_faketime->offset,
                __ATOMIC_ACQUIRE
            );

        return 1;
    }

    return linuwux_get_local_faketime(offset);
}

static int64_t unix_to_windows_ticks(
    time_t seconds,
    long nanoseconds)
{
    return
        (int64_t)seconds * LINUWUX_TICKS_PER_SECOND +
        (int64_t)nanoseconds / 100 +
        LINUWUX_TICKS_1601_TO_1970;
}

static void windows_ticks_to_unix(
    int64_t ticks,
    time_t *seconds,
    long *nanoseconds)
{
    int64_t unix_ticks;

    unix_ticks =
        ticks - LINUWUX_TICKS_1601_TO_1970;

    if (unix_ticks < 0)
        unix_ticks = 0;

    *seconds =
        (time_t)(
            unix_ticks /
            LINUWUX_TICKS_PER_SECOND
        );

    *nanoseconds =
        (long)(
            unix_ticks %
            LINUWUX_TICKS_PER_SECOND
        ) * 100;
}

int linuwux_time_init(void)
{
    if (!real_clock_gettime_fn)
    {
        real_clock_gettime_fn =
            (clock_gettime_fn)dlsym(
                RTLD_NEXT,
                "clock_gettime"
            );
    }

    if (!real_gettimeofday_fn)
    {
        real_gettimeofday_fn =
            (gettimeofday_fn)dlsym(
                RTLD_NEXT,
                "gettimeofday"
            );
    }

    if (!real_clock_gettime_fn ||
        !real_gettimeofday_fn)
    {
        linuwux_log(
            "faketime init failed: libc clock functions unavailable"
        );

        return -1;
    }

    if (linuwux_shared_faketime_init() != 0)
    {
        linuwux_log(
            "faketime shared state unavailable"
        );
    }
    else
    {
        linuwux_log(
            "faketime shared state ready%s",
            current_process_is_wineserver ?
                " (wineserver)" :
                ""
        );
    }

    return 0;
}

int linuwux_time_set_faketime(
    int64_t requested)
{
    struct timespec now;
    uint64_t now_ticks;
    uint64_t effective_ticks;
    uint64_t effective_high;
    uint64_t requested_value;
    uint64_t offset_bits;
    int64_t previous_offset;
    int64_t offset;

    if (!real_clock_gettime_fn)
        return -1;

    /*
     * Read the real host clock first. We then subtract the faketime
     * already published for this prefix, reproducing wineserver's
     * already-adjusted current_time semantics for repeated requests.
     */
    if (real_clock_gettime_fn(
            CLOCK_REALTIME,
            &now) != 0)
    {
        return -1;
    }

    now_ticks =
        (uint64_t)unix_to_windows_ticks(
            now.tv_sec,
            now.tv_nsec
        );

    effective_ticks = now_ticks;

    if (linuwux_get_published_faketime(
            &previous_offset))
    {
        /*
         * Work modulo 2^64. This also preserves the bit pattern of
         * a negative two's-complement offset without signed overflow.
         */
        effective_ticks -=
            (uint64_t)previous_offset;
    }

    effective_high =
        effective_ticks >> 32;

    requested_value =
        (uint64_t)requested;

    /*
     * Preserve the original LinUwUx arithmetic:
     *
     *   ((current_time >> 32) - requested) << 32
     *
     * Do it entirely as unsigned arithmetic so left-shifting a
     * negative signed value cannot invoke undefined behavior.
     */
    offset_bits =
        (effective_high - requested_value) << 32;

    /*
     * Preserve the resulting 64-bit bit pattern in the signed API
     * used by the rest of the runtime.
     */
    memcpy(
        &offset,
        &offset_bits,
        sizeof(offset)
    );

    atomic_store(
        &faketime_offset,
        offset
    );

    atomic_store(
        &faketime_active,
        1
    );

    if (shared_faketime)
    {
        /*
         * The mapping is created during normal constructor
         * execution. The CPUID signal path only performs atomic
         * stores here.
         */
        __atomic_store_n(
            &shared_faketime->active,
            UINT32_C(0),
            __ATOMIC_RELEASE
        );

        __atomic_store_n(
            &shared_faketime->offset,
            offset,
            __ATOMIC_RELEASE
        );

        __atomic_store_n(
            &shared_faketime->active,
            UINT32_C(1),
            __ATOMIC_RELEASE
        );
    }

    return 0;
}

int linuwux_time_is_active(void)
{
    return atomic_load(&faketime_active);
}

int64_t linuwux_time_offset(void)
{
    return atomic_load(&faketime_offset);
}

LINUWUX_EXPORT int clock_gettime(
    clockid_t clock_id,
    struct timespec *result)
{
    int ret;

    if (!real_clock_gettime_fn)
    {
        real_clock_gettime_fn =
            (clock_gettime_fn)dlsym(
                RTLD_NEXT,
                "clock_gettime"
            );

        if (!real_clock_gettime_fn)
            return -1;
    }

    ret =
        real_clock_gettime_fn(
            clock_id,
            result
        );

    if (ret != 0)
        return ret;

    if (clock_id == CLOCK_REALTIME
#ifdef CLOCK_REALTIME_COARSE
        || clock_id == CLOCK_REALTIME_COARSE
#endif
       )
    {
        int64_t ticks;
        int64_t offset;

        /*
         * clock_gettime keeps the existing process-local
         * behavior. Shared faketime is intentionally consumed
         * only by wineserver gettimeofday().
         */
        if (!linuwux_get_local_faketime(
                &offset))
        {
            return ret;
        }

        ticks =
            unix_to_windows_ticks(
                result->tv_sec,
                result->tv_nsec
            );

        ticks -= offset;

        windows_ticks_to_unix(
            ticks,
            &result->tv_sec,
            &result->tv_nsec
        );
    }

    return ret;
}

LINUWUX_EXPORT int gettimeofday(
    struct timeval *result,
    void *timezone)
{
    int ret;

    if (!real_gettimeofday_fn)
    {
        real_gettimeofday_fn =
            (gettimeofday_fn)dlsym(
                RTLD_NEXT,
                "gettimeofday"
            );

        if (!real_gettimeofday_fn)
            return -1;
    }

    ret =
        real_gettimeofday_fn(
            result,
            timezone
        );

    if (ret != 0)
        return ret;

    {
        int64_t ticks;
        int64_t offset;
        time_t seconds;
        long nanoseconds;

        if (!linuwux_get_gettimeofday_faketime(
                &offset))
        {
            return ret;
        }

        ticks =
            unix_to_windows_ticks(
                result->tv_sec,
                (long)result->tv_usec * 1000
            );

        ticks -= offset;

        windows_ticks_to_unix(
            ticks,
            &seconds,
            &nanoseconds
        );

        result->tv_sec = seconds;
        result->tv_usec =
            (suseconds_t)(
                nanoseconds / 1000
            );
    }

    return ret;
}
