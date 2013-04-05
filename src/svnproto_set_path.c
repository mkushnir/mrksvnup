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
 *  set-path
 *      params:   ( path:string rev:number start-empty:bool
 *                  ? [ lock-token:string ] ? depth:word
 *                )
 *      response: none
 */


static int
pack3(UNUSED svnc_ctx_t *ctx,
      bytestream_t *out,
      UNUSED void *st,
      UNUSED void *udata)
{
    struct {
        const char *path;
        long rev;
        const char *lock_token;
        svn_depth_t depth;
        long flags;
    } *params = udata;


    if (params->lock_token != NULL) {
        if (pack_string(out, strlen(params->lock_token),
                        params->lock_token) != 0) {
            TRRET(SVNPROTO_SET_PATH + 1);
        }
    }

    return 0;
}

static int
pack2(UNUSED svnc_ctx_t *ctx,
      bytestream_t *out,
      UNUSED void *st,
      void *udata)
{
    struct {
        const char *path;
        long rev;
        const char *lock_token;
        svn_depth_t depth;
        long flags;
    } *params = udata;
    const char *bool_str[] = { "false", "true" };
    int idx;

    if (pack_string(out, strlen(params->path), params->path) != 0) {
        TRRET(SVNPROTO_SET_PATH + 2);
    }

    if (pack_number(out, params->rev) != 0) {
        TRRET(SVNPROTO_SET_PATH + 3);
    }

    idx = (params->flags & SETPFLAG_START_EMPTY) ? 1 : 0;
    if (pack_word(out, strlen(bool_str[idx]), bool_str[idx]) != 0) {
        TRRET(SVNPROTO_SET_PATH + 4);
    }

    if (pack_list(out, pack3, NULL, udata) != 0) {
        TRRET(SVNPROTO_SET_PATH + 5);
    }

    if (pack_word(out, strlen(SVN_DEPTH_STR(params->depth)),
                  SVN_DEPTH_STR(params->depth)) != 0) {
        TRRET(SVNPROTO_SET_PATH + 6);
    }

    return 0;
}

static int
pack1(UNUSED svnc_ctx_t *ctx,
      bytestream_t *out,
      UNUSED void *st,
      void *udata)
{
    if (pack_word(out,
                  strlen("set-path"), "set-path") != 0) {
        TRRET(SVNPROTO_SET_PATH + 7);
    }

    if (pack_list(out, pack2, NULL, udata) != 0) {
        TRRET(SVNPROTO_SET_PATH + 8);
    }

    return 0;
}

int svnproto_set_path(svnc_ctx_t * ctx,
                      const char *path,
                      long rev,
                      const char * lock_token,
                      svn_depth_t depth,
                      long flags)
{
    int res = 0;
    struct {
        const char *path;
        long rev;
        const char *lock_token;
        svn_depth_t depth;
        long flags;
    } params = {path, rev, lock_token, depth, flags};

    if (pack_list(&ctx->out, pack1, NULL, &params) != 0) {
        TRRET(SVNPROTO_SET_PATH + 9);
    }

    if (bytestream_produce_data(&ctx->out, ctx->fd) != 0) {
        TRRET(SVNPROTO_SET_PATH + 10);
    }

    bytestream_rewind(&ctx->out);

    return res;
}

