//#define TRRET_DEBUG
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

#pragma GCC diagnostic ignored "-Wnonnull"

void
get_file_props_el_start(void *udata,
                        const XML_Char *name,
                        UNUSED const XML_Char **atts)
{
    dav_ctx_t *davctx = udata;
    const char *known_prop_names[] = {
        "http://subversion.tigris.org/xmlns/svn/executable",
        "http://subversion.tigris.org/xmlns/svn/special",
        "http://subversion.tigris.org/xmlns/dav/md5-checksum",
        "DAV:version-name",
    };
    unsigned i;

    for (i = 0; i < countof(known_prop_names); ++i) {
        if (strcmp(name, known_prop_names[i]) == 0) {
            davctx->set_prop_name = bytes_from_str(name);
            break;
        }
    }
}

void
get_file_props_chardata(void *udata,
                        const XML_Char *s,
                        int len)
{
    dav_ctx_t *davctx = udata;
    svnc_fileent_t *fe = davctx->fe;
    svnc_prop_t *p;

    /* XXX handle multiline data */

    if (davctx->set_prop_name != NULL) {
        if (strcmp(BDATA(davctx->set_prop_name),
                   "http://subversion.tigris.org/"
                   "xmlns/dav/md5-checksum") == 0) {
            fe->checksum = bytes_from_strn(s, len);
            free(davctx->set_prop_name);

        } else if (strcmp(BDATA(davctx->set_prop_name), "DAV:version-name") == 0) {
            fe->rev = strtol(s, NULL, 10);
            free(davctx->set_prop_name);

        } else {
            if ((p = array_incr(&fe->props)) == NULL) {
                FAIL("array_incr");
            }
            p->value = bytes_from_strn(s, len);
            p->name = davctx->set_prop_name;
        }
        davctx->set_prop_name = NULL;
    }
}


static int
get_file_props_body_cb(mnhttp_ctx_t *ctx,
                       mnbytestream_t *in,
                       void *udata)
{
    dav_ctx_t *davctx = udata;
    enum XML_Status res;

    //D16(SDATA(in, ctx->body.start), ctx->body.end - ctx->body.start);

    res = XML_Parse(davctx->p,
                    SDATA(in, ctx->body.start),
                    ctx->body.end - ctx->body.start,
                    0);

    if (res != 1) {
        TRRET(GET_FILE_PROPS_BODY_CB + 1);
    }

    TRRET(0);
}

static int
get_file_props_status_cb(mnhttp_ctx_t *ctx,
                         UNUSED mnbytestream_t *in,
                         void *udata)
{
    dav_ctx_t *davctx = udata;
    dav_xml_cb_t cb = {
        NULL,
        NULL,
        get_file_props_el_start,
        NULL,
        get_file_props_chardata,
    };

    //TRACE("status=%d", ctx->status);
    if (ctx->status != 207) {
        TRRET(GET_FILE_PROPS_HEADER_CB + 1);
    }

    dav_setup_xml_parser(davctx, &cb, davctx, NULL);

    TRRET(0);
}

static int
get_file_contents_status_cb(mnhttp_ctx_t *ctx,
                            UNUSED mnbytestream_t *in,
                            UNUSED void *udata)
{
    if (ctx->status != 200) {
        TRRET(GET_FILE_CONTENTS_HEADER_CB + 1);
    }
    TRRET(0);
}

static int
get_file_contents_body_cb(mnhttp_ctx_t *ctx, mnbytestream_t *in, void *udata)
{
    dav_ctx_t *davctx = udata;
    svnc_fileent_t *fe = davctx->fe;
    mnbytes_t **b;

    //D32(SDATA(in, ctx->current_chunk.start), ctx->current_chunk.end - ctx->current_chunk.start);

    if ((b = array_incr(&fe->contents)) == NULL) {
        FAIL("array_incr");
    }

    if ((*b = bytes_from_mem(SDATA(in, ctx->current_chunk.start),
                             ctx->current_chunk.end -
                             ctx->current_chunk.start)) == NULL) {

        FAIL("bytes_from_mem");
    }

    TRRET(0);
}

int
httpproto_get_file(svnc_ctx_t *ctx,
                   const char *path,
                   long rev,
                   int flags,
                   svnc_fileent_t *e)
{
    int res = 0;
    char *fullpath = NULL;
    char *davpath = NULL;
    dav_ctx_t *davctx = ctx->udata;
    const char *body =
        "<?xml version=\"1.0\" encoding=\"utf-8\"?>"
        "<propfind xmlns=\"DAV:\">"
            "<allprop/>"
        "</propfind>";


    fullpath = path_join(ctx->path, path);
    davpath = dav_rvr_path(davctx, fullpath, rev);
    davctx->fe = e;

DO_REQUEST:
    /* props */
    if (flags & GETFLAG_WANT_PROPS) {
        if (dav_request(ctx, "PROPFIND", davpath, SVN_DEPTH_UNKNOWN,
                        body, strlen(body), NULL) != 0) {
            res = HTTPPROTO_GET_FILE + 1;
            goto END;
        }


        if ((res = http_parse_response(ctx->fd, &ctx->in,
                                       get_file_props_status_cb,
                                       NULL,
                                       get_file_props_body_cb, davctx)) != 0) {

            if (res == PARSE_EMPTY) {
                res = 0;
                goto TRY_RECONNECT;
            } else {
                res = HTTPPROTO_GET_FILE + 2;
            }
            goto END;
        }
    }

    /* contents */
    if (flags & GETFLAG_WANT_CONTENTS) {
        if (dav_request(ctx, "GET", davpath, -1, NULL, 0, NULL) != 0) {
            res = HTTPPROTO_GET_FILE + 3;
            goto END;
        }

        if (http_parse_response(ctx->fd, &ctx->in,
                                get_file_contents_status_cb,
                                NULL,
                                get_file_contents_body_cb, davctx) != 0) {

            res = HTTPPROTO_GET_FILE + 4;
            goto END;
        }
    }

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

TRY_RECONNECT:
    //TRACE("Reconnecting ...");
    //sleep(1);
    if ((res = svnc_socket_reconnect(ctx)) != 0) {
        res = HTTPPROTO_GET_FILE + 5;
        goto END;
    }
    goto DO_REQUEST;
}


