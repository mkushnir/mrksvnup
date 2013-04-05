#ifndef XMATCH_H
#define XMATCH_H

#include "mrkcommon/array.h"

typedef struct _xmatch {
    const char *pattern;
    array_t compiled_pattern;
    array_t input_stack;
} xmatch_t;

int xmatch_init(xmatch_t *, const char *);
int xmatch_fini(xmatch_t *);
void xmatch_push(xmatch_t *, const char *);
void xmatch_pop(xmatch_t *);
int xmatch_matches(xmatch_t *);

#endif
