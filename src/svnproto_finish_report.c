#include <string.h>

#include "diag.h"
#include "mrkcommon/util.h"
//#define TRRET_DEBUG
#include "mrkcommon/dumpm.h"

#include "mrksvnup/svnproto.h"
#include "mrkcommon/bytestream.h"

/*
 * command:
 *  ( command-name:word params:list )
 */

/*
 *  finish-report
 *      params:   ( )
 *      response: none
 */


static int
pack1(UNUSED svnc_ctx_t *ctx,
      mnbytestream_t *out,
      UNUSED void *st,
      UNUSED void *udata)
{
    if (pack_word(out,
                  strlen("finish-report"), "finish-report") != 0) {
        TRRET(SVNPROTO_FINISH_REPORT + 1);
    }

    if (pack_list(out, NULL, NULL, NULL) != 0) {
        TRRET(SVNPROTO_FINISH_REPORT + 2);
    }

    return 0;
}

int
svnproto_finish_report(svnc_ctx_t * ctx)
{
    int res = 0;

    if (pack_list(&ctx->out, pack1, NULL, NULL) != 0) {
        TRRET(SVNPROTO_FINISH_REPORT + 3);
    }

    if (bytestream_produce_data(&ctx->out, ctx->fd) != 0) {
        TRRET(SVNPROTO_FINISH_REPORT + 4);
    }

    bytestream_rewind(&ctx->out);

    return res;
}

