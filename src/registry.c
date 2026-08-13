#define _GNU_SOURCE

#include "registry.h"

#include <dlfcn.h>
#include <stddef.h>
#include <stdatomic.h>
#include <stdint.h>
#include <string.h>

typedef int32_t linuwux_ntstatus;
typedef uint32_t linuwux_ulong;
typedef uint32_t linuwux_access_mask;
typedef uintptr_t linuwux_handle;
typedef uint16_t linuwux_wchar;

typedef struct
{
    uint16_t length;
    uint16_t maximum_length;
    linuwux_wchar *buffer;
} linuwux_unicode_string;

typedef struct
{
    linuwux_ulong length;
    linuwux_handle root_directory;
    linuwux_unicode_string *object_name;
    linuwux_ulong attributes;
    void *security_descriptor;
    void *security_quality_of_service;
} linuwux_object_attributes;

typedef linuwux_ntstatus (*nt_create_key_fn)(
    linuwux_handle *,
    linuwux_access_mask,
    const linuwux_object_attributes *,
    linuwux_ulong,
    const linuwux_unicode_string *,
    linuwux_ulong,
    linuwux_ulong *
);

typedef linuwux_ntstatus (*nt_set_value_key_fn)(
    linuwux_handle,
    const linuwux_unicode_string *,
    linuwux_ulong,
    linuwux_ulong,
    const void *,
    linuwux_ulong
);

typedef linuwux_ntstatus (*nt_close_fn)(
    linuwux_handle
);

#define LINUWUX_NT_SUCCESS(status) \
    ((linuwux_ntstatus)(status) >= 0)

#define LINUWUX_OBJ_CASE_INSENSITIVE \
    UINT32_C(0x00000040)

#define LINUWUX_KEY_SET_VALUE \
    UINT32_C(0x00000002)

#define LINUWUX_REG_OPTION_NON_VOLATILE \
    UINT32_C(0x00000000)

#define LINUWUX_REG_SZ \
    UINT32_C(1)

_Static_assert(
    sizeof(linuwux_wchar) == 2,
    "NT WCHAR must be 16-bit"
);

_Static_assert(
    sizeof(linuwux_unicode_string) == 16,
    "unexpected UNICODE_STRING ABI"
);

_Static_assert(
    offsetof(linuwux_unicode_string, buffer) == 8,
    "unexpected UNICODE_STRING buffer offset"
);

_Static_assert(
    sizeof(linuwux_object_attributes) == 48,
    "unexpected OBJECT_ATTRIBUTES ABI"
);

static linuwux_wchar registry_path[] =
{
    '\\','R','e','g','i','s','t','r','y',
    '\\','M','a','c','h','i','n','e',
    '\\','S','y','s','t','e','m',
    '\\','C','u','r','r','e','n','t','C','o','n','t','r','o','l','S','e','t',
    '\\','C','o','n','t','r','o','l',
    '\\','I','D','C','o','n','f','i','g','D','B',
    '\\','H','a','r','d','w','a','r','e',' ','P','r','o','f','i','l','e','s',
    '\\','0','0','0','1',
    0
};

static linuwux_wchar value_name[] =
{
    'H','w','P','r','o','f','i','l','e','G','u','i','d',
    0
};

static linuwux_wchar value_data[] =
{
    '{','1','2','3','4','5','6','7','8','-',
    '1','2','3','4','-',
    '1','2','3','4','-',
    '1','2','3','4','-',
    '1','2','3','4','5','6','7','8','9','0','1','2',
    '}',
    0
};

static void init_unicode_string(
    linuwux_unicode_string *string,
    linuwux_wchar *buffer,
    size_t character_count)
{
    string->length =
        (uint16_t)(
            character_count *
            sizeof(*buffer)
        );

    string->maximum_length =
        (uint16_t)(
            (character_count + 1) *
            sizeof(*buffer)
        );

    string->buffer = buffer;
}

static _Atomic int hwprofileguid_done;

int linuwux_registry_ensure_hwprofileguid(void)
{
    void *ntdll;
    nt_create_key_fn nt_create_key;
    nt_set_value_key_fn nt_set_value_key;
    nt_close_fn nt_close;
    linuwux_unicode_string key_name;
    linuwux_unicode_string name;
    linuwux_object_attributes attributes;
    linuwux_handle key = 0;
    linuwux_ntstatus status;

    if (atomic_load(&hwprofileguid_done))
        return 0;

    ntdll =
        dlopen(
            "ntdll.so",
            RTLD_NOW | RTLD_NOLOAD
        );

    if (!ntdll)
        return -1;

    nt_create_key =
        (nt_create_key_fn)dlsym(
            ntdll,
            "NtCreateKey"
        );

    nt_set_value_key =
        (nt_set_value_key_fn)dlsym(
            ntdll,
            "NtSetValueKey"
        );

    nt_close =
        (nt_close_fn)dlsym(
            ntdll,
            "NtClose"
        );

    if (!nt_create_key ||
        !nt_set_value_key ||
        !nt_close)
    {
        dlclose(ntdll);
        return -1;
    }

    init_unicode_string(
        &key_name,
        registry_path,
        sizeof(registry_path) /
            sizeof(registry_path[0]) - 1
    );

    init_unicode_string(
        &name,
        value_name,
        sizeof(value_name) /
            sizeof(value_name[0]) - 1
    );

    memset(
        &attributes,
        0,
        sizeof(attributes)
    );

    attributes.length =
        sizeof(attributes);

    attributes.object_name =
        &key_name;

    attributes.attributes =
        LINUWUX_OBJ_CASE_INSENSITIVE;

    status =
        nt_create_key(
            &key,
            LINUWUX_KEY_SET_VALUE,
            &attributes,
            0,
            NULL,
            LINUWUX_REG_OPTION_NON_VOLATILE,
            NULL
        );

    if (!LINUWUX_NT_SUCCESS(status))
    {
        dlclose(ntdll);
        return -2;
    }

    status =
        nt_set_value_key(
            key,
            &name,
            0,
            LINUWUX_REG_SZ,
            value_data,
            sizeof(value_data)
        );

    (void)nt_close(key);
    dlclose(ntdll);

    if (!LINUWUX_NT_SUCCESS(status))
        return -2;

    atomic_store(
        &hwprofileguid_done,
        1
    );

    return 0;
}
