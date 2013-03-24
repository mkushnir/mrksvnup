#include <assert.h>
#include <sys/types.h>

#include "diag.h"
#include "mrkcommon/util.h"
#include "mrkcommon/dumpm.h"

#include "mrksvnup/svnproto.h"
#include "mrksvnup/bytestream.h"

/*
 * Data packing.
 */
int
pack_word(bytestream_t *out, size_t sz, const char *word)
{
    if (bytestream_nprintf(out, sz + 2, "%s ", word) != 0) {
        TRRET(PACK_WORD + 1);
    }

    return (0);
}

int
pack_number(bytestream_t *out, int n)
{
    if (bytestream_nprintf(out, 32, "%d ", n) != 0) {
        TRRET(PACK_NUMBER + 1);
    }

    return (0);
}

int
pack_string(bytestream_t *out, size_t sz, const char *str)
{
    if (bytestream_nprintf(out, 32, "%d:", sz) != 0) {
        TRRET(PACK_STRING + 1);
    }

    if (bytestream_cat(out, sz, str) != 0) {
        TRRET(PACK_STRING + 2);
    }

    if (bytestream_cat(out, 1, " ") != 0) {
        TRRET(PACK_STRING + 3);
    }

    return (0);
}

int
pack_list(bytestream_t *out, svnproto_cb_t cb, svnc_ctx_t *ctx, void *udata)
{
    if (bytestream_nprintf(out, 3, "( ") != 0) {
        TRRET(PACK_LIST + 1);
    }

    if (cb != NULL && cb(ctx, out, NULL, udata) != 0) {
        TRRET(PACK_LIST + 2);
    }

    if (bytestream_nprintf(out, 3, ") ") != 0) {
        TRRET(PACK_LIST + 3);
    }

    return (0);
}

