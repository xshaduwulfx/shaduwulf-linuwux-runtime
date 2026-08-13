#ifndef LINUWUX_SUD_H
#define LINUWUX_SUD_H

#include <stddef.h>

int linuwux_sud_init(void);

int linuwux_sud_learn_selector(
    unsigned char *selector
);

void linuwux_sud_rearm(void);

long linuwux_sud_teb_offset(void);

#endif
