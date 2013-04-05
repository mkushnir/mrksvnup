#include <assert.h>

#define TRRET_DEBUG
//#define TRRET_DEBUG_VERBOSE
#include "mrkcommon/dumpm.h"
#include "mrkcommon/bytestream.h"
#include "mrkcommon/traversedir.h"
#include "mrkcommon/util.h"

#include "mrksvnup/svnc.h"
#include "mrksvnup/http.h"
#include "mrksvnup/dav.h"

#include "httpproto_private.h"

#include "diag.h"

int
httpproto_finish_report(UNUSED svnc_ctx_t *ctx)
{
    TRRET(0);
}

int
httpproto_get_file(UNUSED svnc_ctx_t *ctx,
                   UNUSED const char *path,
                   UNUSED long rev,
                   UNUSED int flags,
                   UNUSED svnc_fileent_t *e)
{
    TRRET(0);
}

static int
options_header_cb(http_ctx_t *ctx, bytestream_t *in, void *udata)
{
    dav_ctx_t *davctx = udata;

    if (ctx->status != 200) {
        TRRET(OPTIONS_HEADER_CB + 1);
    }

    if ((strcasecmp(SDATA(in, ctx->current_header_name.start),
                    "SVN-Youngest-Rev")) == 0) {

        davctx->youngest_rev = strtol(SDATA(in,
                ctx->current_header_value.start), NULL, 10);

    } else if (strcasecmp(SDATA(in, ctx->current_header_name.start),
               "SVN-Me-Resource") == 0) {

        davctx->me = strdup(SDATA(in, ctx->current_header_value.start));

    } else if (strcasecmp(SDATA(in, ctx->current_header_name.start),
               "SVN-Repository-Root") == 0) {

        davctx->reproot = strdup(SDATA(in, ctx->current_header_value.start));

    } else if (strcasecmp(SDATA(in, ctx->current_header_name.start),
               "SVN-Rev-Root-Stub") == 0) {

        davctx->revroot = strdup(SDATA(in, ctx->current_header_value.start));
    }

    TRRET(0);
}


static int
options_body_cb(http_ctx_t *ctx, bytestream_t *in, void *udata)
{
    dav_ctx_t *davctx = udata;
    enum XML_Status res;
    UNUSED dav_xml_cb_t cb = {
        debug_ns_start,
        debug_ns_end,
        debug_el_start,
        debug_el_end,
        debug_chardata,
    };

    if (ctx->parser_state == PS_BODY_IN) {
        dav_setup_xml_parser(davctx, NULL, NULL, NULL);
    }

    res = XML_Parse(davctx->p,
                    SDATA(in, ctx->body.start),
                    ctx->body.end - ctx->body.start,
                    0);

    //TRACE("res=%d", res);
    if (res != 1) {
        TRRET(OPTIONS_BODY_CB + 1);
    }
    TRRET(0);
}

int
httpproto_setup(UNUSED svnc_ctx_t *ctx)
{
    const char *body =
        "<?xml version=\"1.0\" encoding=\"utf-8\"?>"
        "<D:options xmlns:D=\"DAV:\">"
            "<D:activity-collection-set/>"
        "</D:options>";

    assert(ctx->udata == NULL);
    if ((ctx->udata = dav_ctx_new()) == NULL) {
        TRRET(HTTPPROTO_SETUP + 1);
    }

    if (dav_request(ctx, "OPTIONS", ctx->path, SVN_DEPTH_UNKNOWN,
                    body, strlen(body)) != 0) {

        TRRET(HTTPPROTO_SETUP + 2);
    }

    if (http_parse_response(ctx->fd, &ctx->in,
                            options_header_cb,
                            options_body_cb, ctx->udata) != 0) {

        TRRET(HTTPPROTO_SETUP + 3);
    }

    TRRET(0);
}

int
httpproto_get_latest_rev(svnc_ctx_t *ctx, long *rev)
{
    dav_ctx_t *davctx = ctx->udata;

    *rev = davctx->youngest_rev;
    TRRET(0);
}

static int
propfind_header_cb(http_ctx_t *ctx,
                   UNUSED bytestream_t *in,
                   UNUSED void *udata)
{
    //TRACE("status=%d", ctx->status);
    if (!(ctx->status != 200 || ctx->status != 207)) {
        TRRET(PROPFIND_HEADER_CB + 1);
    }

    TRRET(0);
}


static int
propfind_body_cb(http_ctx_t *ctx, bytestream_t *in, void *udata)
{
    dav_ctx_t *davctx = udata;
    enum XML_Status res;
    dav_xml_cb_t cb = {
        NULL,
        NULL,
        pattern_match_el_start,
        pattern_match_el_end,
        NULL,
    };

    if (ctx->parser_state == PS_BODY_IN) {
        dav_setup_xml_parser(davctx, &cb, davctx,
                             "|DAV:multistatus|DAV:response"
                             "|DAV:propstat|DAV:prop|DAV:resourcetype"
                             "|DAV:collection|");

    }

    //D16(SDATA(in, ctx->body.start), ctx->body.end - ctx->body.start);

    res = XML_Parse(davctx->p,
                    SDATA(in, ctx->body.start),
                    ctx->body.end - ctx->body.start,
                    0);

    if (res != 1) {
        TRRET(PROPFIND_BODY_CB + 1);
    }
    if (davctx->match_result != 0) {
        TRRET(PROPFIND_BODY_CB + 2);
    }
    TRRET(0);
}

int
httpproto_check_path(svnc_ctx_t *ctx,
                     const char *path,
                     long rev,
                     int *kind)
{
    int res = 0;
    dav_ctx_t *davctx = ctx->udata;
    char *davpath = NULL;
    char *fullpath = NULL;
    size_t pathsz;
    size_t rootsz;
    char *p;

    const char *body =
        "<?xml version=\"1.0\" encoding=\"utf-8\"?>"
        "<propfind xmlns=\"DAV:\">"
            "<prop>"
                "<version-controlled-configuration xmlns=\"DAV:\"/>"
                "<resourcetype xmlns=\"DAV:\"/>"
                "<baseline-relative-path "
                    "xmlns=\"http://subversion.tigris.org/xmlns/dav/\"/>"
                "<repository-uuid "
                    "xmlns=\"http://subversion.tigris.org/xmlns/dav/\"/>"
            "</prop>"
        "</propfind>";

    if (davctx->revroot == NULL || davctx->reproot == NULL) {
        res = HTTPPROTO_CHECK_PATH + 1;
        goto END;
    }

    fullpath = path_join(ctx->path, path);

    pathsz = strlen(fullpath) + strlen(davctx->revroot) + 64;
    rootsz = strlen(davctx->reproot);

    if ((p = strstr(fullpath, davctx->reproot)) == NULL) {
        res = HTTPPROTO_CHECK_PATH + 2;
        goto END;
    }

    if (p != fullpath) {
        res = HTTPPROTO_CHECK_PATH + 2;
        goto END;
    }

    if ((davpath = malloc(pathsz)) == NULL) {
        FAIL("malloc");
    }

    snprintf(davpath, pathsz, "%s/%ld%s",
             davctx->revroot,
             rev,
             p + rootsz);

    //TRACE("davpath=%s", davpath);

    if (dav_request(ctx, "PROPFIND", davpath, SVN_DEPTH_UNKNOWN,
                    body, strlen(body)) != 0) {
        res = HTTPPROTO_CHECK_PATH + 2;
        goto END;
    }


    if (http_parse_response(ctx->fd, &ctx->in,
                            propfind_header_cb,
                            propfind_body_cb, davctx) != 0) {

        res = HTTPPROTO_CHECK_PATH + 3;
        goto END;
    }

    *kind = SVNC_KIND_DIR;

END:
    if (davpath != NULL) {
        free(davpath);
        davpath = NULL;
    }

    if (fullpath != NULL) {
        free(fullpath);
        fullpath = NULL;
    }

    TRRET(res);
}

int
httpproto_update(svnc_ctx_t *ctx,
                 long rev,
                 const char *target,
                 svn_depth_t depth,
                 long flags,
                 svnc_cb_t cb,
                 void *udata)
{
    dav_ctx_t *davctx = ctx->udata;
    davctx->target_rev = rev;
    davctx->depth = depth;
    davctx->path = target;
    davctx->flags |= flags;

    if (cb != NULL) {
        if (cb(ctx, NULL, NULL, udata) != 0) {
            TRRET(HTTPPROTO_UPDATE + 1);
        }
    }
    TRRET(0);
}

int
httpproto_set_path(UNUSED svnc_ctx_t *ctx,
                   UNUSED const char *path,
                   UNUSED long rev,
                   UNUSED const char *lock_token,
                   UNUSED svn_depth_t depth,
                   UNUSED long flags)
{
    dav_ctx_t *davctx = ctx->udata;
    davctx->source_rev = rev;
    davctx->path = path;
    davctx->depth = depth;
    davctx->flags |= flags;
    TRRET(0);
}

int
httpproto_editor(UNUSED svnc_ctx_t *ctx)
{
    dav_ctx_t *davctx = ctx->udata;
    UNUSED const char *body = 
        "<?xml version=\"1.0\" encoding=\"utf-8\"?>"
        "<S:update-report send-all=\"true\" xmlns:S=\"svn:\">"
            "<S:src-path>%s</S:src-path>"
            "<S:entry "
                "rev=\"249103\" "
                "depth=\"infinity\" "
                "start-empty=\"true\">"
            "</S:entry>"
            "<S:target-revision>249103</S:target-revision>"
            "<S:depth>unknown</S:depth>"
        "</S:update-report>";

    TRACE("source_rev=%ld target_rev=%ld depth=%s path=%s flags=%08lx",
          davctx->source_rev,
          davctx->target_rev,
          SVN_DEPTH_STR(davctx->depth),
          davctx->path,
          davctx->flags);

    if (ctx->udata != NULL) {
        dav_ctx_destroy((dav_ctx_t *)(ctx->udata));
        ctx->udata = NULL;
    }
    TRRET(0);
}

