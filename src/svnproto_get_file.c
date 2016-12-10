#include "diag.h"
#include "mrkcommon/array.h"
#include "mrkcommon/util.h"
//#define TRRET_DEBUG
#include "mrkcommon/dumpm.h"

#include "mrksvnup/svnc.h"
#include "mrksvnup/svnproto.h"
#include "mrkcommon/bytestream.h"

/*
 * command:
 *  ( command-name:word params:list )
 */

/*
 *  get-file
 *      params:     ( path:string [ rev:number ] want-props:bool
 *                    want-contents:bool [ want-iprops:bool ]
 *                  )
 *      response:   ( [ checksum:string ] rev:number props:proplist
 *                    [ inherited-props:iproplist ]
 *                  )
 */

static int
pack3(UNUSED svnc_ctx_t *ctx,
      mnbytestream_t *out,
      UNUSED void *st,
      UNUSED void *udata)
{
    struct {
        const char *path;
        long rev;
        int flags;
    } *params = udata;

    if (pack_number(out, params->rev) != 0) {
        TRRET(SVNPROTO_GET_FILE + 1);
    }
    return 0;
}

static int
pack2(UNUSED svnc_ctx_t *ctx,
      mnbytestream_t *out,
      UNUSED void *st,
      UNUSED void *udata)
{
    struct {
        const char *path;
        long rev;
        int flags;
    } *params = udata;
    const char *bool_str[] = { "false", "true" };
    int idx;

    if (pack_string(out, strlen(params->path), params->path) != 0) {
        TRRET(SVNPROTO_GET_FILE + 2);
    }

    if (pack_list(out, pack3, NULL, udata) != 0) {
        TRRET(SVNPROTO_GET_FILE + 3);
    }

    idx = (params->flags & GETFLAG_WANT_PROPS) ? 1 : 0;
    if (pack_word(out, strlen(bool_str[idx]), bool_str[idx]) != 0) {
        TRRET(SVNPROTO_GET_FILE + 4);
    }

    idx = (params->flags & GETFLAG_WANT_CONTENTS) ? 1 : 0;
    if (pack_word(out, strlen(bool_str[idx]), bool_str[idx]) != 0) {
        TRRET(SVNPROTO_GET_FILE + 5);
    }

    if (pack_list(out, NULL, NULL, NULL) != 0) {
        TRRET(SVNPROTO_GET_FILE + 6);
    }

    return 0;
}

static int
pack1(UNUSED svnc_ctx_t *ctx,
      mnbytestream_t *out,
      UNUSED void *st,
      UNUSED void *udata)
{
    if (pack_word(out, strlen("get-file"), "get-file") != 0) {
        TRRET(SVNPROTO_GET_FILE + 7);
    }

    if (pack_list(out, pack2, NULL, udata) != 0) {
        TRRET(SVNPROTO_GET_FILE + 8);
    }

    return 0;
}

static int
unpack3(UNUSED svnc_ctx_t *ctx,
              UNUSED mnbytestream_t *in,
              svnproto_state_t *st,
              UNUSED void *udata)
{
    int res = 0;
    mnbytes_t **b;
    mnarray_t *ar = udata;
    ssize_t sz;

    sz = st->r.end - st->r.start;

#ifdef TRRET_DEBUG
    TRACE(FRED("sz=%ld"), sz);
#endif

    if (sz <= 0) {
        res = SVNPROTO_UNPACK_NOMATCH_GOAHEAD;

    } else {

        if ((b = array_incr(ar)) == NULL) {
            FAIL("array_incr");
        }

        if ((*b = malloc(sizeof(mnbytes_t) + sz)) == NULL) {
            FAIL("malloc");
        }
        (*b)->sz = sz;
        memcpy((*b)->data, SDATA(in, st->r.start), (*b)->sz);
    }
    TRRET(res);
}

static int
unpack2(svnc_ctx_t *ctx,
        mnbytestream_t *in,
        UNUSED svnproto_state_t *st,
        UNUSED void *udata)
{
    int res;
    res = svnproto_unpack(ctx, in, "()");
    if (res != 0) {
        TRRET(res);
    }

    TRRET(res);
}

static int
unpack1(svnc_ctx_t *ctx,
        mnbytestream_t *in,
        UNUSED svnproto_state_t *st,
        UNUSED void *udata)
{
    int res;
    mnbytes_t *name = NULL, *value = NULL;
    svnc_fileent_t *e = udata;
    svnc_prop_t *p;


    res = svnproto_unpack(ctx, in, "(ss)", &name, &value);
    if (name != NULL) {
        if ((p = array_incr(&e->props)) == NULL) {
            FAIL("array_incr");
        }
        p->name = name;
        p->value = value;
    }

    TRRET(res);
}

int
svnproto_get_file(svnc_ctx_t *ctx,
                  const char *path,
                  long rev,
                  int flags,
                  svnc_fileent_t *e)
{
    int res = 0;
    struct {
        const char *path;
        long rev;
        int flags;
    } params = {path, rev, flags};
    int reconnect_attempt = 0;

DO_REQUEST:
    if (pack_list(&ctx->out, pack1, NULL, &params) != 0) {
        TRRET(SVNPROTO_GET_FILE + 9);
    }

    if (bytestream_produce_data(&ctx->out, ctx->fd) != 0) {
        TRRET(SVNPROTO_GET_FILE + 10);
    }

    if (svnproto_check_auth(ctx) != 0) {
        if (!reconnect_attempt) {
            ++reconnect_attempt;
            goto TRY_RECONNECT;
        } else {
            TRRET(SVNPROTO_GET_FILE + 11);
        }
    }

    if (svnproto_command_response(ctx, "((s?)n(r*)r?)",
                                  &e->checksum,
                                  &e->rev,
                                  unpack1, e,
                                  unpack2, NULL) != 0) {
        TRRET(SVNPROTO_GET_FILE + 12);
    }

    if (flags & GETFLAG_WANT_CONTENTS) {
        res = svnproto_unpack(ctx, &ctx->in, "S*", unpack3, &e->contents);
    }

    if (svnproto_command_response(ctx, "()") != 0) {
        TRRET(SVNPROTO_GET_FILE + 13);
    }

    bytestream_rewind(&ctx->out);

    TRRET(res);

TRY_RECONNECT:
    if ((res = svnc_socket_reconnect(ctx)) != 0) {
        TRRET(SVNPROTO_GET_FILE + 14);
    }
    if (ctx->setup(ctx) != 0) {
        TRRET(SVNPROTO_GET_FILE + 15);
    }
    goto DO_REQUEST;
}


