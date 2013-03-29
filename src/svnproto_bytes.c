#include <stdlib.h>

#include "diag.h"
#include "mrkcommon/array.h"
#include "mrkcommon/util.h"
//#define TRRET_DEBUG
#include "mrkcommon/dumpm.h"
#include "mrksvnup/svnproto_bytes.h"

static int
init_bytes(svnproto_bytes_t **v)
{
    *v = NULL;
    return 0;
}

static int
fini_bytes(svnproto_bytes_t **v)
{
    if (*v != NULL) {
        free(*v);
        *v = NULL;
    }
    return 0;
}

static int
dump_bytes(svnproto_bytes_t **v, UNUSED void *udata)
{
    if (*v != NULL) {
        D16((*v)->data, MIN(64, (*v)->sz));
    } else {
        TRACE("bytes null");
    }
    return 0;
}

void
svnproto_init_bytes_array(array_t *ar)
{
    if (array_init(ar, sizeof(svnproto_bytes_t *), 0,
                   (array_initializer_t)init_bytes,
                   (array_finalizer_t)fini_bytes) != 0) {
        FAIL("array_init");
    }
}

void
svnproto_fini_bytes_array(array_t *ar)
{
    if (array_fini(ar) != 0) {
        FAIL("array_fini");
    }
}

void
svnproto_dump_bytes_array(array_t *ar)
{
    array_traverse(ar, (array_traverser_t)dump_bytes, NULL);
}


