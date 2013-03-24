#ifndef SVNPROTO_BYTES_H
#define SVNPROTO_BYTES_H

#include "mrkcommon/array.h"

typedef struct _svnproto_bytes {
    size_t sz;
    char data[];
} svnproto_bytes_t;

/*
 * This macro is also defined in bytestream.h for a different structure.
 * Surprisingly, it appears to be exactly the same.
 *
 */
#ifndef BDATA
#   define BDATA(b) ((b) != NULL ? (b)->data : NULL)
#endif

/*
 * Helpers
 */
void svnproto_init_bytes_array(array_t *ar);
void svnproto_fini_bytes_array(array_t *ar);
void svnproto_dump_bytes_array(array_t *ar);
#endif

