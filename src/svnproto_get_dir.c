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
 *  get-dir
 *      params:     ( path:string [ rev:number ]
 *                    want-props:bool want-contents:bool
 *                    ? ( field:dirent-field ... )
 *                    [ want-iprops:bool ]
 *                  )
 *
 *      dirent-field: kind | size | has-props |
 *                    created-rev | time | last-author
 *                    | word
 *
 *      response:   ( rev:number props:proplist
 *                    ( entry:dirent ... )
 *                    [ inherited-props:iproplist ]
 *                  )
 *
 *      proplist:   ( ( name:string value:string ) ... )
 *
 *      dirent:     ( name:string kind:node-kind
 *                    size:number has-props:bool
 *                    created-rev:number
 *                    [ created-date:string ]
 *                    [ last-author:string ]
 *                  ) 
 */

static int
pack4(UNUSED svnc_ctx_t *ctx,
      bytestream_t *out,
      UNUSED svnproto_state_t *st,
      UNUSED void *udata)
{
    const char *fields[] = {
        "kind",
        "size",
    };
    unsigned i;

    for (i = 0; i < countof(fields); ++i) {
        if (pack_word(out, strlen(fields[i]), fields[i]) != 0) {
            TRRET(SVNPROTO_GET_DIR + 1);
        }
    }
    return 0;
}

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
        TRRET(SVNPROTO_GET_DIR + 2);
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
    } *params = udata;

    //TRACE("path=%s rev=%ld", params->path, params->rev);

    if (pack_string(out, strlen(params->path), params->path) != 0) {
        TRRET(SVNPROTO_GET_DIR + 3);
    }
    if (pack_list(out, pack3, NULL, udata) != 0) {
        TRRET(SVNPROTO_GET_DIR + 4);
    }
    if (pack_word(out, strlen("false"), "false") != 0) {
        TRRET(SVNPROTO_GET_DIR + 5);
    }
    if (pack_word(out, strlen("true"), "true") != 0) {
        TRRET(SVNPROTO_GET_DIR + 6);
    }
    if (pack_list(out, pack4, NULL, NULL) != 0) {
        TRRET(SVNPROTO_GET_DIR + 7);
    }
    if (pack_list(out, NULL, NULL, NULL) != 0) {
        TRRET(SVNPROTO_GET_DIR + 8);
    }

    return 0;
}

static int
pack1(UNUSED svnc_ctx_t *ctx,
      bytestream_t *out,
      UNUSED svnproto_state_t *st,
      UNUSED void *udata)
{
    if (pack_word(out, strlen("get-dir"), "get-dir") != 0) {
        TRRET(SVNPROTO_GET_DIR + 9);
    }

    if (pack_list(out, pack2, NULL, udata) != 0) {
        TRRET(SVNPROTO_GET_DIR + 10);
    }

    return 0;
}

static int
unpack3(svnc_ctx_t *ctx,
        bytestream_t *in,
        UNUSED svnproto_state_t *st,
        UNUSED void *udata)
{
    int res;

    res = svnproto_unpack(ctx, in, "()");
    TRRET(PARSE_EOD);
}

static int
unpack2(svnc_ctx_t *ctx,
        bytestream_t *in,
        UNUSED svnproto_state_t *st,
        UNUSED void *udata)
{
    int res;
    svnproto_bytes_t *name = NULL;
    char *kind_str = NULL;
    long size;
    array_t *dirents = udata;
    svnproto_dirent_t *e;


    res = svnproto_unpack(ctx, in, "(swnwn(s?)(s?))", &name, &kind_str, &size,
                          NULL,
                          NULL,
                          NULL,
                          NULL);
    if (name != NULL) {
        if ((e = array_incr(dirents)) == NULL) {
            FAIL("array_incr");
        }
        e->name = name;
        e->kind = svnproto_kind2int(kind_str);
        e->size = size;
    }
    if (kind_str != NULL) {
        free(kind_str);
        kind_str = NULL;
    }
    if (res != 0) {
        res = PARSE_EOD;
    }
    TRRET(res);
}

static int
unpack1(svnc_ctx_t *ctx,
        bytestream_t *in,
        UNUSED svnproto_state_t *st,
        UNUSED void *udata)
{
    int res;
    svnproto_bytes_t *key = NULL, *value = NULL;

    res = svnproto_unpack(ctx, in, "(ss)", &key, &value);
    //TRACE("key=%s value=%s", BDATA(key), BDATA(value));
    if (key != NULL) {
        free(key);
    }
    if (value != NULL) {
        free(value);
    }
    if (res != 0) {
        res = PARSE_EOD;
    }
    TRRET(res);
}

static int
dirent_init(svnproto_dirent_t *e)
{
    e->name = NULL;
    e->kind = -1;
    e->size = -1;
    e->rev = -1;
    return 0;
}

static int
dirent_fini(svnproto_dirent_t *e)
{
    if (e->name != NULL) {
        free(e->name);
        e->name = NULL;
    }
    e->kind = -1;
    e->size = -1;
    e->rev = -1;
    return 0;
}

static int
dirent_dump(svnproto_dirent_t *e)
{
    TRACE("dirent name=%s rev=%ld kind=%s size=%zd",
           BDATA(e->name), e->rev, svnproto_kind2str(e->kind), e->size);
    return 0;
}

void
svnproto_init_dirent_array(array_t *ar)
{
    if (array_init(ar, sizeof(svnproto_dirent_t), 0,
                   (array_initializer_t)dirent_init,
                   (array_finalizer_t)dirent_fini) != 0) {
        FAIL("array_init");
    }
}

void
svnproto_dump_dirent_array(array_t *ar)
{
    array_traverse(ar, (array_traverser_t)dirent_dump, NULL);
}

int
svnproto_get_dir(svnc_ctx_t *ctx,
                 const char *path,
                 long rev,
                 /* TODO want-props want-contents fields want-iprops */
                 array_t *dirents)
{
    int res = 0;
    struct {
        const char *path;
        long rev;
    } params = {path, rev};
    long orev;
    svnproto_dirent_t *de;
    array_iter_t it;

    if (pack_list(&ctx->out, pack1, NULL, &params) != 0) {
        TRRET(SVNPROTO_GET_DIR + 11);
    }

    if (bytestream_produce_data(&ctx->out, ctx->fd) != 0) {
        TRRET(SVNPROTO_GET_DIR + 12);
    }

    if (svnproto_check_auth(ctx) != 0) {
        TRRET(SVNPROTO_GET_DIR + 13);
    }

    if (svnproto_command_response(ctx, "(n(r*)(r*)r?)",
                                  &orev,
                                  unpack1, NULL,
                                  unpack2, dirents,
                                  unpack3, NULL) != 0) {
        TRRET(SVNPROTO_GET_DIR + 14);
    }
    //TRACE("orev=%ld", orev);
    for (de = array_first(dirents, &it);
         de != NULL;
         de = array_next(dirents, &it)) {
        de->rev = orev;
    }


    bytestream_rewind(&ctx->in);
    bytestream_rewind(&ctx->out);

    return res;
}

