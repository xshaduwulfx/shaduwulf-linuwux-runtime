#define _GNU_SOURCE

#include "runtime.h"
#include "time.h"
#include "sud.h"
#include "syscall.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

static int debug_enabled;
static int debug_fd = STDERR_FILENO;

static pid_t debug_identity_pid = (pid_t)-1;
static char debug_process_name[64] = "unknown";

static void linuwux_debug_refresh_identity(void)
{
    pid_t pid;
    int fd;
    ssize_t length;

    pid = getpid();

    if (debug_identity_pid == pid)
        return;

    debug_identity_pid = pid;

    memcpy(
        debug_process_name,
        "unknown",
        sizeof("unknown")
    );

    fd = open(
        "/proc/self/comm",
        O_RDONLY | O_CLOEXEC
    );

    if (fd < 0)
        return;

    length = read(
        fd,
        debug_process_name,
        sizeof(debug_process_name) - 1
    );

    close(fd);

    if (length <= 0)
    {
        memcpy(
            debug_process_name,
            "unknown",
            sizeof("unknown")
        );
        return;
    }

    debug_process_name[length] = '\0';

    while (length > 0 &&
           (debug_process_name[length - 1] == '\n' ||
            debug_process_name[length - 1] == '\r'))
    {
        debug_process_name[length - 1] = '\0';
        length--;
    }

    if (length == 0)
    {
        memcpy(
            debug_process_name,
            "unknown",
            sizeof("unknown")
        );
    }
}

static int linuwux_debug_make_log_path(
    const char *directory,
    char *path,
    size_t path_size)
{
    struct timespec now;
    struct tm local;
    char timestamp[32];
    pid_t session_id;
    int written;
    int needs_slash;

    if (!directory ||
        !*directory ||
        !path ||
        path_size == 0)
    {
        return -1;
    }

    if (syscall(
            SYS_clock_gettime,
            CLOCK_REALTIME,
            &now) != 0)
    {
        return -1;
    }

    if (!localtime_r(
            &now.tv_sec,
            &local))
    {
        return -1;
    }

    if (strftime(
            timestamp,
            sizeof(timestamp),
            "%Y%m%d-%H%M%S",
            &local) == 0)
    {
        return -1;
    }

    session_id = getsid(0);

    if (session_id < 0)
        session_id = getpid();

    needs_slash =
        directory[strlen(directory) - 1] != '/';

    written = snprintf(
        path,
        path_size,
        "%s%slinuwux-runtime-%s-s%ld.log",
        directory,
        needs_slash ? "/" : "",
        timestamp,
        (long)session_id
    );

    if (written < 0 ||
        (size_t)written >= path_size)
    {
        return -1;
    }

    return 0;
}

static void linuwux_debug_init_output(void)
{
    const char *existing_log;
    const char *directory;
    char generated_path[PATH_MAX];
    const char *path;
    int fd;
    int saved_errno;

    if (!debug_enabled)
        return;

    /*
     * LINUWUX_DEBUG_LOG is internal state.
     *
     * The first runtime instance creates the automatic path and
     * exports it. Descendant processes inherit the exact same path
     * and therefore append to one session log.
     */
    existing_log = getenv("LINUWUX_DEBUG_LOG");

    if (existing_log && *existing_log)
    {
        path = existing_log;
    }
    else
    {
        directory = getenv("LINUWUX_DEBUG_DIR");

        if (!directory || !*directory)
            return;

        if (linuwux_debug_make_log_path(
                directory,
                generated_path,
                sizeof(generated_path)) != 0)
        {
            fprintf(
                stderr,
                "[linuwux-runtime] "
                "could not generate debug log path; "
                "using stderr\n"
            );
            return;
        }

        if (setenv(
                "LINUWUX_DEBUG_LOG",
                generated_path,
                1) != 0)
        {
            fprintf(
                stderr,
                "[linuwux-runtime] "
                "could not publish debug log path; "
                "using stderr\n"
            );
            return;
        }

        path = getenv("LINUWUX_DEBUG_LOG");

        if (!path || !*path)
            return;
    }

    fd = open(
        path,
        O_WRONLY |
        O_CREAT |
        O_APPEND |
        O_CLOEXEC,
        0644
    );

    if (fd < 0)
    {
        saved_errno = errno;

        fprintf(
            stderr,
            "[linuwux-runtime] "
            "could not open debug log '%s': %s; "
            "using stderr\n",
            path,
            strerror(saved_errno)
        );

        return;
    }

    debug_fd = fd;
}

static int linuwux_override_name_matches(
    const char *start,
    size_t length,
    const char *name)
{
    size_t name_length;

    if (!start || !name)
        return 0;

    name_length = strlen(name);

    if (length == name_length &&
        strncmp(start, name, name_length) == 0)
    {
        return 1;
    }

    if (length == name_length + 4 &&
        strncmp(start, name, name_length) == 0 &&
        strncmp(start + name_length, ".dll", 4) == 0)
    {
        return 1;
    }

    return 0;
}

static int linuwux_override_present(
    const char *overrides,
    const char *name)
{
    const char *entry;

    if (!overrides || !*overrides)
        return 0;

    entry = overrides;

    while (*entry)
    {
        const char *entry_end;
        const char *equals;

        entry_end = strchr(entry, ';');

        if (!entry_end)
            entry_end = entry + strlen(entry);

        equals = memchr(
            entry,
            '=',
            (size_t)(entry_end - entry)
        );

        if (equals &&
            linuwux_override_name_matches(
                entry,
                (size_t)(equals - entry),
                name))
        {
            return 1;
        }

        if (!*entry_end)
            break;

        entry = entry_end + 1;
    }

    return 0;
}

static int linuwux_append_override(
    char **buffer,
    size_t *length,
    const char *entry)
{
    char *new_buffer;
    size_t entry_length;
    size_t extra;

    if (!buffer || !length || !entry)
        return -1;

    entry_length = strlen(entry);

    extra =
        entry_length +
        (*length ? 1 : 0);

    new_buffer = realloc(
        *buffer,
        *length + extra + 1
    );

    if (!new_buffer)
        return -1;

    *buffer = new_buffer;

    if (*length)
    {
        (*buffer)[*length] = ';';
        (*length)++;
    }

    memcpy(
        *buffer + *length,
        entry,
        entry_length
    );

    *length += entry_length;
    (*buffer)[*length] = '\0';

    return 0;
}

static void linuwux_runtime_prepare_environment(void)
{
    static const struct
    {
        const char *name;
        const char *entry;
    } required[] =
    {
        { "winmm", "winmm=n,b" },
        { "version", "version=n,b" },
        { "reflex", "reflex=n,b" }
    };

    const char *existing;
    char *overrides;
    size_t length;
    size_t i;

    existing = getenv("WINEDLLOVERRIDES");

    overrides = NULL;
    length = 0;

    if (existing && *existing)
    {
        length = strlen(existing);

        overrides = malloc(length + 1);

        if (!overrides)
            return;

        memcpy(
            overrides,
            existing,
            length + 1
        );
    }

    for (i = 0;
         i < sizeof(required) / sizeof(required[0]);
         i++)
    {
        if (linuwux_override_present(
                existing,
                required[i].name))
        {
            continue;
        }

        if (linuwux_append_override(
                &overrides,
                &length,
                required[i].entry) != 0)
        {
            free(overrides);
            return;
        }
    }

    if (overrides)
    {
        (void)setenv(
            "WINEDLLOVERRIDES",
            overrides,
            1
        );

        free(overrides);
    }

    /*
     * Respect an explicit user or launcher choice.
     */
    (void)setenv(
        "PROTON_DISABLE_LSTEAMCLIENT",
        "1",
        0
    );
}

__attribute__((constructor))
static void linuwux_runtime_init(void)
{
    const char *value = getenv("LINUWUX_DEBUG");

    debug_enabled =
        value &&
        value[0] &&
        value[0] != '0';

    linuwux_debug_init_output();

    linuwux_runtime_prepare_environment();

    linuwux_log(
        "WINEDLLOVERRIDES=%s",
        getenv("WINEDLLOVERRIDES")
    );

    linuwux_log(
        "PROTON_DISABLE_LSTEAMCLIENT=%s",
        getenv("PROTON_DISABLE_LSTEAMCLIENT")
    );

    if (linuwux_time_init() != 0)
        linuwux_log("faketime initialization unavailable");

    (void)linuwux_sud_init();
    (void)linuwux_syscall_init();

    linuwux_log("runtime loaded");
}

int linuwux_debug_enabled(void)
{
    return debug_enabled;
}

void linuwux_log(const char *fmt, ...)
{
    char line[4096];
    va_list ap;
    pid_t pid;
    int saved_errno;
    int prefix_length;
    int message_length;
    size_t length;
    ssize_t result;

    if (!debug_enabled)
        return;

    saved_errno = errno;

    linuwux_debug_refresh_identity();

    pid = getpid();

    prefix_length = snprintf(
        line,
        sizeof(line),
        "[linuwux-runtime pid=%ld proc=%s] ",
        (long)pid,
        debug_process_name
    );

    if (prefix_length < 0)
    {
        errno = saved_errno;
        return;
    }

    if ((size_t)prefix_length >= sizeof(line))
    {
        errno = saved_errno;
        return;
    }

    va_start(ap, fmt);

    message_length = vsnprintf(
        line + prefix_length,
        sizeof(line) - (size_t)prefix_length,
        fmt,
        ap
    );

    va_end(ap);

    if (message_length < 0)
    {
        errno = saved_errno;
        return;
    }

    length =
        (size_t)prefix_length +
        (size_t)message_length;

    if (length >= sizeof(line) - 1)
        length = sizeof(line) - 2;

    line[length++] = '\n';

    do
    {
        result = write(
            debug_fd,
            line,
            length
        );
    }
    while (result < 0 && errno == EINTR);

    /*
     * If the selected log becomes unavailable during execution,
     * preserve diagnostics by falling back to stderr.
     */
    if (result < 0 &&
        debug_fd != STDERR_FILENO)
    {
        do
        {
            result = write(
                STDERR_FILENO,
                line,
                length
            );
        }
        while (result < 0 && errno == EINTR);
    }

    errno = saved_errno;
}
