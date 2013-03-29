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
 *  check-path
 *      params:   ( path:string [ rev:number ] )
 *      response: ( kind:node-kind )
 */
static int
pack3(UNUSED svnc_ctx_t *ctx,
      bytestream_t *out,
      UNUSED svnproto_state_t *st,
      UNUSED void *udata)
{
    struct {
        const char *path;
        long rev;
    } *params = udata;


    if (pack_number(out, params->rev) != 0) {
        TRRET(SVNPROTO_CHECK_PATH + 1);
    }

    return 0;
}

static int
pack2(UNUSED svnc_ctx_t *ctx,
      bytestream_t *out,
      UNUSED svnproto_state_t *st,
      void *udata)
{
    struct {
        const char *path;
        long rev;
    } *params = udata;

    if (pack_string(out, strlen(params->path), params->path) != 0) {
        TRRET(SVNPROTO_CHECK_PATH + 2);
    }

    if (pack_list(out, pack3, NULL, udata) != 0) {
        TRRET(SVNPROTO_CHECK_PATH + 3);
    }

    return 0;
}

static int
pack1(svnc_ctx_t *ctx,
      bytestream_t *out,
      UNUSED svnproto_state_t *st,
      void *udata)
{
    if (pack_word(out, strlen("check-path"), "check-path") != 0) {
        TRRET(SVNPROTO_CHECK_PATH + 4);
    }

    if (pack_list(out, pack2, ctx, udata) != 0) {
        TRRET(SVNPROTO_CHECK_PATH + 5);
    }

    return 0;
}

int
svnproto_check_path(svnc_ctx_t *ctx, const char *path, long rev, int *kind)
{
    int res = 0;
    char *kind_str = NULL;
    struct {
        const char *path;
        long rev;
    } params = {path, rev};

    if (pack_list(&ctx->out, pack1, ctx, &params) != 0) {
        TRRET(SVNPROTO_CHECK_PATH + 6);
    }

    if (bytestream_produce_data(&ctx->out, ctx->fd) != 0) {
        TRRET(SVNPROTO_CHECK_PATH + 7);
    }

    if (svnproto_check_auth(ctx) != 0) {
        TRRET(SVNPROTO_CHECK_PATH + 8);
    }

    if (svnproto_command_response(ctx, "(w)", &kind_str) != 0) {
        TRRET(SVNPROTO_CHECK_PATH + 9);
    }

    *kind = svnproto_kind2int(kind_str);

    bytestream_rewind(&ctx->out);


    return res;
}

