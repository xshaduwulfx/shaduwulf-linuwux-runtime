#ifndef LINUWUX_REGISTRY_H
#define LINUWUX_REGISTRY_H

/*
 * Ensure the LinUwUx hardware-profile GUID exists in the
 * current Wine prefix.
 *
 * Returns:
 *   0  success
 *  -1  ntdll registry API is not available yet
 *  -2  registry operation failed
 */
int linuwux_registry_ensure_hwprofileguid(void);

#endif
