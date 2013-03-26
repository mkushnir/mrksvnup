#include "diag.h"
#include "mrkcommon/array.h"
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
      bytestream_t *out,
      UNUSED svnproto_state_t *st,
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
      bytestream_t *out,
      UNUSED svnproto_state_t *st,
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
      bytestream_t *out,
      UNUSED svnproto_state_t *st,
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
unpack2(svnc_ctx_t *ctx,
        bytestream_t *in,
        UNUSED svnproto_state_t *st,
        UNUSED void *udata)
{
    int res;

    res = svnproto_unpack(ctx, in, "()");
    TRRET(PARSE_EOD);
}

static int
unpack1(svnc_ctx_t *ctx,
        bytestream_t *in,
        UNUSED svnproto_state_t *st,
        UNUSED void *udata)
{
    int res;
    svnproto_bytes_t *name = NULL, *value = NULL;
    svnproto_fileent_t *e = udata;
    svnproto_prop_t *p;


    res = svnproto_unpack(ctx, in, "(ss)", &name, &value);
    if (name != NULL) {
        if ((p = array_incr(&e->props)) == NULL) {
            FAIL("array_incr");
        }
        p->name = name;
        p->value = value;
    }

    if (res != 0) {
        res = PARSE_EOD;
    }
    TRRET(res);
}

static int
prop_init(svnproto_prop_t *p)
{
    p->name = NULL;
    p->value = NULL;
    return 0;
}

static int
prop_fini(svnproto_prop_t *p)
{
    if (p->name != NULL) {
        free(p->name);
        p->name = NULL;
    }
    if (p->value != NULL) {
        free(p->value);
        p->value = NULL;
    }
    return 0;
}

static int
prop_dump(svnproto_prop_t *p, UNUSED void *udata)
{
    TRACE("prop: %s=%s", BDATA(p->name), BDATA(p->value));
    return 0;
}


int
svnproto_fileent_init(svnproto_fileent_t *e)
{
    e->checksum = NULL;
    e->rev = -1;
    if (array_init(&e->props, sizeof(svnproto_prop_t), 0,
                   (array_initializer_t)prop_init,
                   (array_finalizer_t)prop_fini) != 0) {
        FAIL("array_init");
    }
    svnproto_init_string_array(&e->contents);
    return 0;
}

int
svnproto_fileent_fini(svnproto_fileent_t *e)
{
    if (e->checksum != NULL) {
        free(e->checksum);
        e->checksum = NULL;
    }
    if (array_fini(&e->props) != 0) {
        FAIL("array_fini");
    }
    array_fini(&e->contents);
    return 0;
}

void
svnproto_fileent_dump(svnproto_fileent_t *e)
{
    TRACE("checksum=%s rev=%ld", BDATA(e->checksum), e->rev);
    array_traverse(&e->props,
                   (array_traverser_t)prop_dump, NULL);
    svnproto_dump_string_array(&e->contents);
}

int
svnproto_get_file(svnc_ctx_t *ctx,
                  const char *path,
                  long rev,
                  int flags,
                  svnproto_fileent_t *e)
{
    int res = 0;
    struct {
        const char *path;
        long rev;
        int flags;
    } params = {path, rev, flags};

    if (pack_list(&ctx->out, pack1, NULL, &params) != 0) {
        TRRET(SVNPROTO_GET_FILE + 9);
    }

    if (bytestream_produce_data(&ctx->out, ctx->fd) != 0) {
        TRRET(SVNPROTO_GET_FILE + 10);
    }

    if (svnproto_check_auth(ctx) != 0) {
        TRRET(SVNPROTO_GET_FILE + 11);
    }

    if (svnproto_command_response(ctx, "((s)n(r*)r?)",
                                  &e->checksum,
                                  &e->rev,
                                  unpack1, e,
                                  unpack2, NULL) != 0) {
        TRRET(SVNPROTO_GET_FILE + 12);
    }

    if (flags & GETFLAG_WANT_CONTENTS) {
        svnproto_unpack(ctx, &ctx->in, "s*", &e->contents);
    }

    bytestream_rewind(&ctx->in);
    bytestream_rewind(&ctx->out);

    return res;
}


