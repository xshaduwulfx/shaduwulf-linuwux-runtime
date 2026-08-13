#ifndef LINUWUX_RUNTIME_H
#define LINUWUX_RUNTIME_H

#include <signal.h>

#if defined(__GNUC__) || defined(__clang__)
#define LINUWUX_EXPORT __attribute__((visibility("default")))
#else
#define LINUWUX_EXPORT
#endif

int linuwux_debug_enabled(void);
void linuwux_log(const char *fmt, ...);

#endif
