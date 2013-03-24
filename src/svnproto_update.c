#include <string.h>

#include "diag.h"
#include "mrkcommon/util.h"
//#define TRRET_DEBUG
#include "mrkcommon/dumpm.h"

#include "mrksvnup/svnproto.h"
#include "mrksvnup/bytestream.h"

/*
 * command:
 *  ( command-name:word params:list )
 */

/*
 *  update
 *      params:   ( [ rev:number ] target:string recurse:bool
 *                  ? depth:word send_copyfrom_param:bool
 *                )
 *      response: ( )
 */

static int
pack4(svnc_ctx_t *ctx,
      bytestream_t *out,
      UNUSED svnproto_state_t *st,
      UNUSED void *udata)
{

    if (pack_word(out, strlen("success"), "success") != 0) {
        TRRET(SVNPROTO_UPDATE + 1);
    }

    if (pack_list(out, NULL, ctx, NULL) != 0) {
        TRRET(SVNPROTO_UPDATE + 2);
    }

    return 0;
}

static int
pack3(UNUSED svnc_ctx_t *ctx,
      bytestream_t *out,
      UNUSED svnproto_state_t *st,
      void *udata)
{
    struct {
        long rev;
        const char *target;
        svn_depth_t depth;
        long flags;
    } *params = udata;


    if (params->rev > 0) {
        if (pack_number(out, params->rev) != 0) {
            TRRET(SVNPROTO_UPDATE + 1);
        }
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
        long rev;
        const char *target;
        svn_depth_t depth;
        long flags;
    } *params = udata;
    const char *bool_str[] = { "false", "true" };
    int idx;

    if (pack_list(out, pack3, NULL, udata) != 0) {
        TRRET(SVNPROTO_UPDATE + 2);
    }

    if (pack_string(out, strlen(params->target), params->target) != 0) {
        TRRET(SVNPROTO_UPDATE + 3);
    }

    idx = (params->flags & UPFLAG_RECURSE) ? 1 : 0;
    if (pack_word(out, strlen(bool_str[idx]), bool_str[idx]) != 0) {
        TRRET(SVNPROTO_UPDATE + 4);
    }

    if (pack_word(out, strlen(SVN_DEPTH_STR(params->depth)),
                  SVN_DEPTH_STR(params->depth)) != 0) {
        TRRET(SVNPROTO_UPDATE + 5);
    }

    idx = (params->flags & UPFLAG_SEND_COPY_FREOM_PARAM) ? 1 : 0;
    if (pack_word(out, strlen(bool_str[idx]), bool_str[idx]) != 0) {
        TRRET(SVNPROTO_UPDATE + 6);
    }

    return 0;
}

static int
pack1(UNUSED svnc_ctx_t *ctx,
      bytestream_t *out,
      UNUSED svnproto_state_t *st,
      void *udata)
{
    if (pack_word(out, strlen("update"), "update") != 0) {
        TRRET(SVNPROTO_UPDATE + 7);
    }

    if (pack_list(out, pack2, NULL, udata) != 0) {
        TRRET(SVNPROTO_UPDATE + 8);
    }

    return 0;
}

int
svnproto_update(svnc_ctx_t *ctx,
                long rev,
                const char *target,
                svn_depth_t depth,
                long flags,
                svnproto_cb_t cb,
                void *udata)
{
    int res = 0;
    struct {
        long rev;
        const char *target;
        svn_depth_t depth;
        long flags;
    } params = {rev, target, depth, flags};

    if (pack_list(&ctx->out, pack1, NULL, &params) != 0) {
        TRRET(SVNPROTO_UPDATE + 9);
    }

    if (bytestream_produce_data(&ctx->out, ctx->fd) != 0) {
        TRRET(SVNPROTO_UPDATE + 10);
    }

    if (svnproto_check_auth(ctx) != 0) {
        TRRET(SVNPROTO_UPDATE + 11);
    }

    if (cb != NULL) {
        if (cb(ctx, NULL, NULL, udata) != 0) {
            TRRET(SVNPROTO_UPDATE + 12);
        }
    }

    /* say success */
    if (pack_list(&ctx->out, pack4, ctx, NULL) != 0) {
        TRRET(SVNPROTO_UPDATE + 13);
    }

    if (bytestream_produce_data(&ctx->out, ctx->fd) != 0) {
        TRRET(SVNPROTO_UPDATE + 10);
    }

    /* check acknowledgement success */
    if (svnproto_command_response(ctx, "()") != 0) {
        TRRET(SVNPROTO_UPDATE + 14);
    }

    bytestream_rewind(&ctx->in);
    bytestream_rewind(&ctx->out);

    return res;
}

