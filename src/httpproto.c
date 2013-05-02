#include <assert.h>
#include <stdio.h>
#include <openssl/bio.h>
#include <openssl/evp.h>

//#define TRRET_DEBUG
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
    /* noop in http */
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
    int res = 0;
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

DO_REQUEST:
    if (dav_request(ctx, "OPTIONS", ctx->path, SVN_DEPTH_UNKNOWN,
                    body, strlen(body), NULL) != 0) {

        TRRET(HTTPPROTO_SETUP + 2);
    }

    if ((res = http_parse_response(ctx->fd, &ctx->in,
                                   NULL,
                                   setup_header_cb,
                                   setup_body_cb, ctx->udata)) != 0) {

        if (res == PARSE_EMPTY) {
            res = 0;
            goto TRY_RECONNECT;
        } else {
            TRRET(HTTPPROTO_SETUP + 3);
        }
    }

    TRRET(0);

TRY_RECONNECT:
    //TRACE("Reconnecting ...");
    //sleep(1);
    if ((res = svnc_socket_reconnect(ctx)) != 0) {
        TRRET(HTTPPROTO_SETUP + 4);
    }
    goto DO_REQUEST;
}

int
httpproto_get_latest_rev(svnc_ctx_t *ctx, long *rev)
{
    dav_ctx_t *davctx = ctx->udata;

    *rev = davctx->youngest_rev;
    TRRET(0);
}

static int
check_path_status_cb(http_ctx_t *ctx,
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

    if (ctx->status != 207) {
        svnc_clear_last_error(davctx->svnctx);
        davctx->svnctx->last_error.apr_error = ctx->status;
#ifdef TRRET_DEBUG
        TRACE("status=%d", ctx->status);
#endif
        //TRRET(CHECK_PATH_HEADER_CB + 1);
    }

    dav_setup_xml_parser(davctx, &cb, davctx,
                         "|DAV:multistatus|DAV:response"
                         "|DAV:propstat|DAV:prop|DAV:resourcetype"
                         "|DAV:collection|");


    TRRET(0);
}

void
check_path_error_chardata(void *udata,
                          const XML_Char *s,
                          int len)
{
    dav_ctx_t *davctx = udata;
    if (!all_spaces(s, len)) {
        davctx->svnctx->last_error.message = bytes_from_strn(s, len);
        //D32(s, len);
    }
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
        davctx->svnctx->last_error.message =
            bytes_from_str("Failed to check path");
        TRRET(CHECK_PATH_BODY_CB + 1);
    }
    if (davctx->match_result != 0) {
        dav_xml_cb_t cb = {
            NULL, NULL,
            NULL, NULL,
            check_path_error_chardata,
        };
        dav_setup_xml_parser(davctx, &cb, davctx, NULL);
        XML_Parse(davctx->p,
                  SDATA(in, ctx->body.start),
                  ctx->body.end - ctx->body.start,
                  1);
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
    char *fullpath = NULL;
    char *davpath = NULL;

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
#ifdef TRRET_DEBUG
        TRACE("revroot=%s reproot=%s", davctx->revroot, davctx->reproot);
#endif
        res = HTTPPROTO_CHECK_PATH + 1;
        goto END;
    }

    fullpath = path_join(ctx->path, path);
    davpath = dav_rvr_path(davctx, fullpath, rev);

    //TRACE("davpath=%s", davpath);

DO_REQUEST:
    if (dav_request(ctx, "PROPFIND", davpath, SVN_DEPTH_UNKNOWN,
                    body, strlen(body), NULL) != 0) {
        res = HTTPPROTO_CHECK_PATH + 2;
        goto END;
    }


    if ((res = http_parse_response(ctx->fd, &ctx->in,
                                   check_path_status_cb,
                                   NULL,
                                   check_path_body_cb, davctx)) != 0) {

        if (res == PARSE_EMPTY) {
            res = 0;
            goto TRY_RECONNECT;
        } else {
            res = HTTPPROTO_CHECK_PATH + 3;
        }
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

TRY_RECONNECT:
    //TRACE("Reconnecting ...");
    //sleep(1);
    if ((res = svnc_socket_reconnect(ctx)) != 0) {
        res = HTTPPROTO_CHECK_PATH + 4;
        goto END;
    }
    goto DO_REQUEST;
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

    if (svnedit_target_rev(ctx, davctx->target_rev) != 0) {
        TRRET(HTTPPROTO_UPDATE + 1);
    }
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
editor_el_start(void *udata,
                const XML_Char *name,
                const XML_Char **atts)
{
    dav_ctx_t *davctx = udata;

    //TRACE("cmd=%s", name);

    xmatch_push(&davctx->xmatch, name);

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
            if (davctx->svnctx->debug_level > 3) {
                LTRACE(1, "open-root rev=%ld", rev);
            }

            if (svnedit_open_root(davctx->svnctx, 0, NULL) != 0) {
                TRRETVOID(EDITOR_EL_START + 1);
            }

        } else {
            bytes_t *path = NULL;

            dav_cwp_enter(davctx, dname);

            if ((path = dav_cwp(davctx)) == NULL) {
                TRRETVOID(EDITOR_EL_START + 2);
            }

            if (svnedit_open_dir(davctx->svnctx, path) != 0) {
                TRRETVOID(EDITOR_EL_START + 3);
            }

            if (davctx->svnctx->debug_level > 3) {
                LTRACE(1, "open-dir path=%s", BDATA(path));
            }

            free(path);
            path = NULL;

        }

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
            bytes_t *path = NULL;

            dav_cwp_enter(davctx, dname);

            if ((path = dav_cwp(davctx)) == NULL) {
                TRRETVOID(EDITOR_EL_START + 4);
            }

            if (svnedit_add_dir(davctx->svnctx, path) != 0) {
                TRRETVOID(EDITOR_EL_START + 5);
            }

            if (davctx->svnctx->debug_level > 3) {
                LTRACE(1, "add-dir path=%s", BDATA(path));
            }

            free(path);
            path = NULL;
        }

    } else if (strcmp(name, "svn:open-file") == 0) {
        svndiff_doc_t *doc;
        const char *fname = NULL;
        const char *rev = NULL;

        while (*atts != NULL) {
            if (strcmp(*atts, "name") == 0) {
                ++atts;
                fname = *atts;

            } else if (strcmp(*atts, "rev") == 0) {
                ++atts;
                rev = *atts;

            } else {
                ++atts;
            }
            ++atts;
        }

        if (fname == NULL) {
            TRACE(FRED("invalid open-file, ignoring"));

        } else {
            if ((doc = svnedit_clear_doc()) == NULL) {
                TRRETVOID(EDITOR_EL_START + 6);
            }

            dav_cwp_enter(davctx, fname);

            /* doc->rp */

            if ((doc->rp = dav_cwp(davctx)) == NULL) {
                TRRETVOID(EDITOR_EL_START + 7);
            }

            doc->rev = strtol(rev, NULL, 10);

            if (davctx->svnctx->debug_level > 3) {
                LTRACE(1, "open-file path=%s", BDATA(doc->rp));
            }

            if (svnedit_open_file(davctx->svnctx) != 0) {
                TRRETVOID(EDITOR_EL_START + 8);
            }

        }

    } else if (strcmp(name, "svn:add-file") == 0) {
        svndiff_doc_t *doc;
        const char *fname = NULL;

        while (*atts != NULL) {
            if (strcmp(*atts, "name") == 0) {
                ++atts;
                fname = *atts;
                break;

            } else {
                ++atts;
            }
            ++atts;
        }

        if (fname == NULL) {
            TRACE(FRED("invalid add-file, ignoring"));

        } else {
            if ((doc = svnedit_clear_doc()) == NULL) {
                TRRETVOID(EDITOR_EL_START + 9);
            }

            dav_cwp_enter(davctx, fname);

            /* doc->rp */

            if ((doc->rp = dav_cwp(davctx)) == NULL) {
                TRRETVOID(EDITOR_EL_START + 10);
            }

            if (davctx->svnctx->debug_level > 3) {
                LTRACE(1, "add-file path=%s", BDATA(doc->rp));
            }

            if (svnedit_add_file(davctx->svnctx) != 0) {
                TRRETVOID(EDITOR_EL_START + 11);
            }

        }

    } else if (strcmp(name, "svn:set-prop") == 0 ||
               strcmp(name, "svn:remove-prop") == 0) {

        while (*atts != NULL) {
            if (strcmp(*atts, "name") == 0) {
                ++atts;
                davctx->set_prop_name = bytes_from_str(*atts);
                break;

            } else {
                ++atts;
            }
            ++atts;
        }

    } else if (strcmp(name, "svn:txdelta") == 0) {
        svndiff_doc_t *doc;

        doc = svnedit_get_doc();

        while (*atts != NULL) {
            if (strcmp(*atts, "base-checksum") == 0) {
                ++atts;
                doc->base_checksum = bytes_from_str(*atts);
                if (davctx->svnctx->debug_level > 3) {
                    LTRACE(1, "txdelta base-checksum=%s",
                           BDATA(doc->base_checksum));
                }
                break;

            } else {
                ++atts;
            }
            ++atts;
        }

    } else if (strcmp(name, "svn:delete-entry") == 0) {
        const char *ename = NULL;

        while (*atts != NULL) {
            if (strcmp(*atts, "name") == 0) {
                ++atts;
                ename = *atts;
                break;

            } else {
                ++atts;
            }
            ++atts;
        }

        if (ename == NULL) {
            TRACE(FRED("invalid delete-entry, ignoring"));

        } else {
            bytes_t *path;

            dav_cwp_enter(davctx, ename);

            if ((path = dav_cwp(davctx)) == NULL) {
                TRRETVOID(EDITOR_EL_START + 12);
            }

            if (davctx->svnctx->debug_level > 3) {
                LTRACE(1, "delete-entry path=%s", BDATA(path));
            }

            if (svnedit_delete_entry(davctx->svnctx, path, 0, NULL) != 0) {
                TRRETVOID(EDITOR_EL_START + 13);
            }

            free(path);
            path = NULL;
            dav_cwp_leave(davctx);
        }

    } else {
        if (davctx->svnctx->debug_level > 3) {
            LTRACE(1, FYELLOW("skipping %s"), name);
        }
    }
}

void
editor_el_end(UNUSED void *udata, UNUSED const XML_Char *name)
{
    dav_ctx_t *davctx = udata;
    //TRACE("cmd=%s", name);

    xmatch_pop(&davctx->xmatch);

    if (strcmp(name, "svn:open-directory") == 0) {
        dav_cwp_leave(davctx);

    } else if (strcmp(name, "svn:add-directory") == 0) {
        dav_cwp_leave(davctx);

    } else if (strcmp(name, "svn:open-file") == 0) {
        if (svnedit_close_file(davctx->svnctx, NULL,
                               davctx->text_checksum) != 0) {
            if (davctx->svnctx->debug_level > 3) {
                LTRACE(1, FYELLOW("skipping %s"), name);
            }
        }

        dav_cwp_leave(davctx);

        if (davctx->text_checksum != NULL) {
            free(davctx->text_checksum);
            davctx->text_checksum = NULL;
        }

    } else if (strcmp(name, "svn:add-file") == 0) {
        if (svnedit_close_file(davctx->svnctx, NULL,
                               davctx->text_checksum)!= 0) {
            if (davctx->svnctx->debug_level > 3) {
                LTRACE(1, FYELLOW("skipping %s"), name);
            }
        }

        dav_cwp_leave(davctx);

        if (davctx->text_checksum != NULL) {
            free(davctx->text_checksum);
            davctx->text_checksum = NULL;
        }


    } else if (strcmp(name, "svn:txdelta") == 0) {
        svndiff_doc_t *doc;
        BIO *b64, *bio;
        char *decoded;
        int nread;

        doc = svnedit_get_doc();

        b64 = BIO_new(BIO_f_base64());
        bio = BIO_new_mem_buf(doc->txdelta.data, doc->txdelta.elnum);
        bio = BIO_push(b64, bio);

        decoded = malloc(doc->txdelta.elnum);
        nread = BIO_read(bio, decoded, doc->txdelta.elnum);


        //D32(doc->txdelta.data, doc->txdelta.elnum);
        //TRACE("Parsing");
        //D32(decoded, nread);

        if (svndiff_parse_doc(decoded, decoded + nread, doc) != 0) {
            TRACE("Failed to parse");
            svndiff_doc_dump(doc);
            //TRACE("%ld/%d", doc->txdelta.elnum, nread);
        }

        BIO_free(b64);
        BIO_free(bio);
        free(decoded);

    } else {
        if (davctx->svnctx->debug_level > 3) {
            LTRACE(1, FYELLOW("skipping %s"), name);
        }
    }
}

void
editor_chardata(void *udata,
                const XML_Char *s,
                int len)
{
    dav_ctx_t *davctx = udata;
    const char *elname;

    elname = xmatch_top(&davctx->xmatch, 0);

    /* XXX handle multiline data */
    if (elname == NULL) {
        return;

    } else if (strcmp(elname, "svn:set-prop") == 0 ||
               strcmp(elname, "svn:remove-prop") == 0) {
        bytes_t *v;

        v = bytes_from_strn(s, len);

        if (BDATA(davctx->set_prop_name) == NULL) {
            //TRACE("elname=%s %s=%s", elname,
            //      BDATA(davctx->set_prop_name), BDATA(v));
            if (v != NULL) {
                free(v);
                v = NULL;
            }

            return;
        }

        if (svnedit_change_file_prop(davctx->svnctx,
                                     davctx->set_prop_name, v) != 0) {

            if (davctx->svnctx->debug_level > 3) {
                LTRACE(1, FYELLOW("skipping %s=%s"),
                      BDATA(davctx->set_prop_name), BDATA(v));
            }
        } else {
            if (davctx->svnctx->debug_level > 3) {
                LTRACE(1, "set-prop %s=%s",
                       BDATA(davctx->set_prop_name), BDATA(v));
            }
        }

        if (davctx->set_prop_name != NULL) {
            free(davctx->set_prop_name);
            davctx->set_prop_name = NULL;
        }

        if (v != NULL) {
            free(v);
            v = NULL;
        }

    } else if (strcmp(elname,
               "http://subversion.tigris.org/xmlns/dav/md5-checksum") == 0) {

        davctx->text_checksum = bytes_from_strn(s, len);

    } else if (strcmp(elname, "svn:txdelta") == 0) {
        svndiff_doc_t *doc;

        doc = svnedit_get_doc();

        array_ensure_len(&doc->txdelta,
                         doc->txdelta.elnum + len,
                         ARRAY_FLAG_SAVE);

        memcpy((char *)array_get(&doc->txdelta, doc->txdelta.elnum - len),
               s, len);

    } else {
        if (davctx->svnctx->debug_level > 4) {
            TRACE(" unhandled char data of %s", elname);
        }
    }

}

static int
editor_status_cb(http_ctx_t *ctx,
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

    if (svnedit_init_shadow_ctx(ctx)) {
        res = HTTPPROTO_EDITOR + 1;
        goto END;
    }

    if (davctx->svnctx->debug_level > 4) {
        TRACE("source_rev=%ld target_rev=%ld depth=%s target=%s flags=%08lx",
              davctx->source_rev,
              davctx->target_rev,
              SVN_DEPTH_STR(davctx->depth),
              davctx->target,
              davctx->flags);
    }

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
                            editor_status_cb,
                            NULL,
                            editor_body_cb, davctx) != 0) {

        res = HTTPPROTO_EDITOR + 4;
        goto END;
    }

END:
    svnedit_close_shadow_ctx();

    if (buf != NULL) {
        free(buf);
        buf = NULL;
    }
    TRRET(res);
}

//UNUSED static const char *get_locations_req =
//    "<?xml version=\"1.0\" encoding=\"utf-8\"?>"
//    "<S:get-locations xmlns:S=\"svn:\" xmlns:D=\"DAV:\">"
//        "<S:path></S:path>"
//        "<S:peg-revision>%ld</S:peg-revision>"
//        "<S:location-revision>%ld</S:location-revision>"
//    "</S:get-locations>";
//
//UNUSED static const char *get_locations_resp =
//    "<?xml version=\"1.0\" encoding=\"utf-8\"?>"
//    "<S:get-locations-report xmlns:S=\"svn:\" xmlns:D=\"DAV:\">"
//        "<S:location rev=\"248000\" path=\"/user/des/svnsup/bin/apply\"/>"
//    "</S:get-locations-report>";



