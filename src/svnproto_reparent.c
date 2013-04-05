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
 *  reparent
 *      params:   ( path:string )
 *      response: ( )
 */

static int
pack2(UNUSED svnc_ctx_t *ctx,
      bytestream_t *out,
      UNUSED void *st,
      void *udata)
{
    struct {
        const char *path;
    } *params = udata;

    if (pack_string(out, strlen(params->path), params->path) != 0) {
        TRRET(SVNPROTO_REPARENT + 1);
    }

    return 0;
}

static int
pack1(UNUSED svnc_ctx_t *ctx,
      bytestream_t *out,
      UNUSED void *st,
      void *udata)
{
    if (pack_word(out, strlen("reparent"), "reparent") != 0) {
        TRRET(SVNPROTO_REPARENT + 2);
    }

    if (pack_list(out, pack2, NULL, udata) != 0) {
        TRRET(SVNPROTO_REPARENT + 3);
    }

    return 0;
}

int
svnproto_reparent(svnc_ctx_t *ctx, const char *path)
{
    int res = 0;
    struct {
        const char *path;
    } params = {path,};

    if (pack_list(&ctx->out, pack1, NULL, &params) != 0) {
        TRRET(SVNPROTO_REPARENT + 4);
    }

    if (bytestream_produce_data(&ctx->out, ctx->fd) != 0) {
        TRRET(SVNPROTO_REPARENT + 5);
    }

    if (svnproto_check_auth(ctx) != 0) {
        TRRET(SVNPROTO_REPARENT + 6);
    }

    if (svnproto_command_response(ctx, "()") != 0) {
        TRRET(SVNPROTO_REPARENT + 7);
    }

    bytestream_rewind(&ctx->out);

    return res;
}

