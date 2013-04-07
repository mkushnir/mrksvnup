#include <assert.h>
#include <stdio.h>

#define TRRET_DEBUG
//#define TRRET_DEBUG_VERBOSE
#include "mrkcommon/dumpm.h"
#include "mrkcommon/bytestream.h"
#include "mrkcommon/traversedir.h"
#include "mrkcommon/util.h"

#include "mrksvnup/svnc.h"
#include "mrksvnup/svnedit.h"
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
setup_header_cb(http_ctx_t *ctx, bytestream_t *in, void *udata)
{
    dav_ctx_t *davctx = udata;
    UNUSED dav_xml_cb_t cb = {
        debug_ns_start,
        debug_ns_end,
        debug_el_start,
        debug_el_end,
        debug_chardata,
    };

    if (ctx->status != 200) {
        TRRET(SETUP_HEADER_CB + 1);
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

    dav_setup_xml_parser(davctx, NULL, NULL, NULL);

    TRRET(0);
}


static int
setup_body_cb(http_ctx_t *ctx, bytestream_t *in, void *udata)
{
    dav_ctx_t *davctx = udata;
    enum XML_Status res;

    res = XML_Parse(davctx->p,
                    SDATA(in, ctx->body.start),
                    ctx->body.end - ctx->body.start,
                    0);

    //TRACE("res=%d", res);
    if (res != 1) {
        TRRET(SETUP_BODY_CB + 1);
    }
    TRRET(0);
}

int
httpproto_setup(UNUSED svnc_ctx_t *ctx)
{
    dav_ctx_t *davctx = NULL;
    const char *body =
        "<?xml version=\"1.0\" encoding=\"utf-8\"?>"
        "<D:options xmlns:D=\"DAV:\">"
            "<D:activity-collection-set/>"
        "</D:options>";

    if ((davctx = dav_ctx_new()) == NULL) {
        TRRET(HTTPPROTO_SETUP + 1);
    }

    /* a weak ref */
    davctx->svnctx = ctx;

    assert(ctx->udata == NULL);
    ctx->udata = davctx;

    if (dav_request(ctx, "OPTIONS", ctx->path, SVN_DEPTH_UNKNOWN,
                    body, strlen(body), NULL) != 0) {

        TRRET(HTTPPROTO_SETUP + 2);
    }

    if (http_parse_response(ctx->fd, &ctx->in,
                            setup_header_cb,
                            setup_body_cb, ctx->udata) != 0) {

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
check_path_header_cb(http_ctx_t *ctx,
                   UNUSED bytestream_t *in,
                   void *udata)
{
    dav_ctx_t *davctx = udata;
    dav_xml_cb_t cb = {
        NULL,
        NULL,
        pattern_match_el_start,
        pattern_match_el_end,
        NULL,
    };

    //TRACE("status=%d", ctx->status);
    if (!(ctx->status != 200 || ctx->status != 207)) {
        TRRET(CHECK_PATH_HEADER_CB + 1);
    }

    dav_setup_xml_parser(davctx, &cb, davctx,
                         "|DAV:multistatus|DAV:response"
                         "|DAV:propstat|DAV:prop|DAV:resourcetype"
                         "|DAV:collection|");


    TRRET(0);
}

static int
check_path_body_cb(http_ctx_t *ctx, bytestream_t *in, void *udata)
{
    dav_ctx_t *davctx = udata;
    enum XML_Status res;

    //D16(SDATA(in, ctx->body.start), ctx->body.end - ctx->body.start);

    res = XML_Parse(davctx->p,
                    SDATA(in, ctx->body.start),
                    ctx->body.end - ctx->body.start,
                    0);

    if (res != 1) {
        TRRET(CHECK_PATH_BODY_CB + 1);
    }
    if (davctx->match_result != 0) {
        TRRET(CHECK_PATH_BODY_CB + 2);
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
                    body, strlen(body), NULL) != 0) {
        res = HTTPPROTO_CHECK_PATH + 2;
        goto END;
    }


    if (http_parse_response(ctx->fd, &ctx->in,
                            check_path_header_cb,
                            check_path_body_cb, davctx) != 0) {

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
    davctx->target = target;
    davctx->flags |= flags;

    if (cb != NULL) {
        if (cb(ctx, NULL, NULL, udata) != 0) {
            TRRET(HTTPPROTO_UPDATE + 1);
        }
    }
    TRRET(0);
}

int
httpproto_set_path(svnc_ctx_t *ctx,
                   const char *path,
                   long rev,
                   UNUSED const char *lock_token,
                   svn_depth_t depth,
                   long flags)
{
    dav_ctx_t *davctx = ctx->udata;
    davctx->source_rev = rev;
    davctx->target = path;
    davctx->depth = depth;
    davctx->flags |= flags;
    TRRET(0);
}

void
editor_el_start(UNUSED void *udata, const XML_Char *name, UNUSED const XML_Char **atts)
{
    dav_ctx_t *davctx = udata;

    //TRACE("cmd=%s", name);

    if (strcmp(name, "svn:open-directory") == 0) {
        const char *dname = NULL;
        long rev = 0;

        while (*atts != NULL) {
            if (strcmp(*atts, "name") == 0) {
                ++atts;
                dname = *atts;

            } else if (strcmp(*atts, "rev") == 0) {
                ++atts;
                rev = strtol(*atts, NULL, 10);

            } else {
                ++atts;
            }
            ++atts;
        }

        if (dname == NULL) {
            TRACE("open-root rev %ld", rev);
            dname = davctx->svnctx->localroot;

            if (svnedit_open_root(davctx->svnctx, 0, NULL) != 0) {
                TRRETVOID(EDITOR_EL_START + 1);
            }

        } else {
            TRACE("open-dir name %s rev %ld", dname, rev);
        }

        dav_dir_enter(davctx, dname);

    } else if (strcmp(name, "svn:add-directory") == 0) {
        const char *dname = NULL;

        while (*atts != NULL) {
            if (strcmp(*atts, "name") == 0) {
                ++atts;
                dname = *atts;
                break;

            } else {
                ++atts;
            }
            ++atts;
        }

        if (dname == NULL) {
            TRACE(FRED("invalid add-directory, ignoring"));

        } else {
            TRACE("add-dir name %s", dname);
            dav_dir_enter(davctx, dname);

        }

    } else if (strcmp(name, "svn:add-file") == 0) {

    } else {
        TRACE(FYELLOW("skipping %s"), name);
    }
}

void
editor_el_end(UNUSED void *udata, UNUSED const XML_Char *name)
{
    dav_ctx_t *davctx = udata;
    //TRACE("cmd=%s", name);

    if (strcmp(name, "svn:open-directory") == 0) {
        dav_dir_leave(davctx);

    } else if (strcmp(name, "svn:add-directory") == 0) {
        dav_dir_leave(davctx);

    } else if (strcmp(name, "svn:add-file") == 0) {
    } else {
        TRACE(FYELLOW("skipping %s"), name);
    }
}

void
editor_chardata(UNUSED void *udata, UNUSED const XML_Char *s, UNUSED int len)
{
    //TRACE("character data:");
    //D32(s, len);
}


static int
editor_header_cb(http_ctx_t *ctx,
                   UNUSED bytestream_t *in,
                   void *udata)
{
    dav_ctx_t *davctx = udata;
    dav_xml_cb_t cb = {
        NULL,
        NULL,
        editor_el_start,
        editor_el_end,
        editor_chardata,
    };

    //TRACE("status=%d", ctx->status);
    if (ctx->status != 200) {
        TRRET(EDITOR_HEADER_CB + 1);
    }

    dav_setup_xml_parser(davctx, &cb, davctx, NULL);

    TRRET(0);
}

static int
editor_body_cb(http_ctx_t *ctx, bytestream_t *in, void *udata)
{
    dav_ctx_t *davctx = udata;
    enum XML_Status res;

    //D32(SDATA(in, ctx->current_chunk.start), ctx->current_chunk.end - ctx->current_chunk.start);

    res = XML_Parse(davctx->p,
                    SDATA(in, ctx->current_chunk.start),
                    ctx->current_chunk.end - ctx->current_chunk.start,
                    ctx->current_chunk_size > 0 ? 0 : 1);

    if (res != 1) {
        TRRET(EDITOR_BODY_CB + 1);
    }
    //if (davctx->match_result != 0) {
    //    TRRET(EDITOR_BODY_CB + 2);
    //}
    TRRET(0);
}

int
httpproto_editor(svnc_ctx_t *ctx)
{
    int res = 0;
    dav_ctx_t *davctx = ctx->udata;
    char *buf = NULL;
    size_t sz, nwritten;
    const char *body = 
        "<?xml version=\"1.0\" encoding=\"utf-8\"?>"
        "<S:update-report send-all=\"true\" xmlns:S=\"svn:\">"
            "<S:src-path>%s</S:src-path>"
            "<S:entry rev=\"%ld\" depth=\"%s\"%s>"
            "</S:entry>"
            "<S:target-revision>%ld</S:target-revision>"
            "<S:depth>%s</S:depth>"
        "</S:update-report>";

    const struct _extra_header eh[] = {
        {"Accept-Encoding", "svndiff1;q=0.9,svndiff;q=0.8"},
        {NULL, NULL},
    };

        //"<?xml version=\"1.0\" encoding=\"utf-8\"?>"
        //"<S:update-report send-all=\"true\" xmlns:S=\"svn:\">"
        //    /* not stub, full path */
        //    "<S:src-path>%s</S:src-path>"
        //    "<S:depth>%s</S:depth>"
        //    "<S:entry "
        //        "rev=\"%ld\" "
        //        "depth=\"%s\" >"
        //    "</S:entry>"
        //"</S:update-report>"

    if (svnedit_init(ctx) != 0) {
        res = HTTPPROTO_EDITOR + 1;
        goto END;
    }

    TRACE("source_rev=%ld target_rev=%ld depth=%s target=%s flags=%08lx",
          davctx->source_rev,
          davctx->target_rev,
          SVN_DEPTH_STR(davctx->depth),
          davctx->target,
          davctx->flags);

    /* XXX consult svnc.h */
    sz = strlen(body) + strlen(ctx->path) +
         16 /* entry rev */ +
         10 /* entry depth longest immediates */ +
         19 /* start-empty SP start-empty="true" */ +
         16 /* target-revision */ +
         10 /* depth longest "immediates" */ +
          1 /* null terminator */;

    if ((buf = malloc(sz)) == NULL) {
        FAIL("malloc");
    }

    if ((nwritten = snprintf(buf, sz, body,
                             ctx->path,
                             davctx->source_rev,
                             SVN_DEPTH_STR(davctx->depth),
                             (davctx->flags & SETPFLAG_START_EMPTY) ?
                                " start-empty=\"true\"" : "",
                             davctx->target_rev,
                             "unknown")) >= sz) {
        res = HTTPPROTO_EDITOR + 2;
        goto END;

    }

    if (dav_request(ctx, "REPORT", davctx->me, -1,
                    buf, nwritten, eh) != 0) {
        res = HTTPPROTO_EDITOR + 3;
        goto END;
    }

    if (http_parse_response(ctx->fd, &ctx->in,
                            editor_header_cb,
                            editor_body_cb, davctx) != 0) {

        res = HTTPPROTO_EDITOR + 4;
        goto END;
    }

    if (ctx->udata != NULL) {
        dav_ctx_destroy((dav_ctx_t *)(ctx->udata));
        ctx->udata = NULL;
    }

END:
    svnedit_fini();

    if (buf != NULL) {
        free(buf);
        buf = NULL;
    }
    TRRET(res);
}

UNUSED static const char *get_locations_req =
    "<?xml version=\"1.0\" encoding=\"utf-8\"?>"
    "<S:get-locations xmlns:S=\"svn:\" xmlns:D=\"DAV:\">"
        "<S:path></S:path>"
        "<S:peg-revision>%ld</S:peg-revision>"
        "<S:location-revision>%ld</S:location-revision>"
    "</S:get-locations>";

UNUSED static const char *get_locations_resp =
    "<?xml version=\"1.0\" encoding=\"utf-8\"?>"
    "<S:get-locations-report xmlns:S=\"svn:\" xmlns:D=\"DAV:\">"
        "<S:location rev=\"248000\" path=\"/user/des/svnsup/bin/apply\"/>"
    "</S:get-locations-report>";



