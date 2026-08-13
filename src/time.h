#ifndef LINUWUX_TIME_H
#define LINUWUX_TIME_H

#include <stdint.h>

int linuwux_time_init(void);

int linuwux_time_set_faketime(
    int64_t requested
);

int linuwux_time_is_active(void);

int64_t linuwux_time_offset(void);

#endif
