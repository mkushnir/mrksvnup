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
 *  get-latest-rev
 *      params:   ( )
 *      response: ( rev:number )
 */
static int
pack1(UNUSED svnc_ctx_t *ctx,
      bytestream_t *out,
      UNUSED svnproto_state_t *v,
      UNUSED void *udata)
{
    if (pack_word(out,
                  strlen("get-latest-rev"), "get-latest-rev") != 0) {
        TRRET(SVNPROTO_GET_LATEST_REV + 1);
    }

    if (pack_list(out, NULL, NULL, NULL) != 0) {
        TRRET(SVNPROTO_GET_LATEST_REV + 2);
    }

    return 0;
}

int
svnproto_get_latest_rev(svnc_ctx_t *ctx, long *rev)
{
    int res = 0;

    if (pack_list(&ctx->out, pack1, NULL, NULL) != 0) {
        TRRET(SVNPROTO_GET_LATEST_REV + 3);
    }

    if (bytestream_produce_data(&ctx->out, ctx->fd) != 0) {
        TRRET(SVNPROTO_GET_LATEST_REV + 4);
    }

    if (svnproto_check_auth(ctx) != 0) {
        TRRET(SVNPROTO_GET_LATEST_REV + 5);
    }

    if (svnproto_command_response(ctx, "(n)", rev) != 0) {
        TRRET(SVNPROTO_GET_LATEST_REV + 6);
    }

    bytestream_rewind(&ctx->out);

    return res;
}

