#define _GNU_SOURCE

#include "runtime.h"
#include "time.h"
#include "sud.h"
#include "syscall.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int debug_enabled;

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
    va_list ap;

    if (!debug_enabled)
        return;

    fprintf(stderr, "[linuwux-runtime] ");

    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);

    fputc('\n', stderr);
}
