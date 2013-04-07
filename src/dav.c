#include <bsdxml.h>

#include "mrkcommon/bytestream.h"
//#define TRRET_DEBUG
#include "mrkcommon/dumpm.h"
#include "mrkcommon/util.h"

#include "mrksvnup/svnc.h"
#include "mrksvnup/http.h"
#include "mrksvnup/dav.h"

#include "diag.h"

void
debug_ns_start(UNUSED void *udata, const XML_Char *prefix, const XML_Char *uri)
{
    TRACE("ns >>> prefix=%s uri=%s", prefix, uri);
}

void
debug_ns_end(UNUSED void *udata, const XML_Char *prefix)
{
    TRACE("ns <<< prefix=%s", prefix);
}

void
debug_el_start(UNUSED void *udata, const XML_Char *name, const XML_Char **atts)
{
    TRACE("el >>> name=%s", name);
    while (*atts != NULL) {
        TRACE("attr=%s", *atts);
        ++atts;
    }

}

void
debug_el_end(UNUSED void *udata, const XML_Char *name)
{
    TRACE("el <<< name=%s", name);
}

void
debug_chardata(UNUSED void *udata, const XML_Char *s, int len)
{
    TRACE("character data:");
    D32(s, len);
}

void
pattern_match_el_start(void *udata,
                       const XML_Char *name,
                       UNUSED const XML_Char **atts)
{
    dav_ctx_t *davctx = udata;
    int match;

    //TRACE("el >>> name=%s", name);
    //while (*atts != NULL) {
    //    TRACE("attr=%s", *atts);
    //    ++atts;
    //}
    xmatch_push(&davctx->xmatch, name);
    if ((match = xmatch_matches(&davctx->xmatch)) == 0) {
        davctx->match_result = 0;
    }
}

void
pattern_match_el_end(void *udata, UNUSED const XML_Char *name)
{
    dav_ctx_t *davctx = udata;
    //TRACE("el <<< name=%s", name);
    xmatch_pop(&davctx->xmatch);
}

void
dav_setup_xml_parser(dav_ctx_t *ctx,
                     dav_xml_cb_t *cb,
                     void *udata,
                     const char *pattern)
{
    XML_ParserReset(ctx->p, NULL);

    if (cb != NULL) {
        if (cb->ns_start != NULL) {
            XML_SetStartNamespaceDeclHandler(ctx->p, cb->ns_start);
        }
        if (cb->ns_end != NULL) {
            XML_SetEndNamespaceDeclHandler(ctx->p, cb->ns_end);
        }
        if (cb->el_start != NULL) {
            XML_SetStartElementHandler(ctx->p, cb->el_start);
        }
        if (cb->el_end != NULL) {
            XML_SetEndElementHandler(ctx->p, cb->el_end);
        }
        if (cb->chardata != NULL) {
            XML_SetCharacterDataHandler(ctx->p, cb->chardata);
        }
    }

    if (udata != NULL) {
        XML_SetUserData(ctx->p, udata);
    }

    xmatch_fini(&ctx->xmatch);
    xmatch_init(&ctx->xmatch, pattern);
}

void
dav_cwp_enter(dav_ctx_t *ctx, const char *dir)
{
    bytes_t **s;

    if ((s = array_incr(&ctx->cwp)) == NULL) {
        FAIL("array_incr");
    }
    *s = bytes_from_str(dir);
}

void
dav_cwp_leave(dav_ctx_t *ctx)
{
    if (ctx->cwp.elnum > 0) {
        if (array_decr(&ctx->cwp) != 0) {
            FAIL("array_decr");
        }
    }
}

bytes_t *
dav_cwp(dav_ctx_t *ctx)
{
    bytes_t *res;
    size_t sz = 0;
    bytes_t **e;
    array_iter_t it;
    char *d;

    for (e = array_first(&ctx->cwp, &it);
         e != NULL;
         e = array_next(&ctx->cwp, &it)) {
        sz += (*e)->sz;
    }
    sz += ctx->cwp.elnum;
    if ((res = malloc(sizeof(bytes_t) + sz + 1)) == NULL) {
        FAIL("malloc");
    }
    res->sz = sz;
    d = res->data;
    for (e = array_first(&ctx->cwp, &it);
         e != NULL;
         e = array_next(&ctx->cwp, &it)) {
        *d++ = '/';
        memcpy(d, (*e)->data, (*e)->sz);
        d += (*e)->sz;
    }
    *d = '\0';
    return res;
}


dav_ctx_t *
dav_ctx_new(void)
{
    dav_ctx_t *ctx = NULL;

    if ((ctx = malloc(sizeof(dav_ctx_t))) == NULL) {
        FAIL("malloc");
    }

    if ((ctx->p = XML_ParserCreateNS(NULL, '\0')) == NULL) {
        FAIL("XML_ParserCreateNS");
    }

    ctx->youngest_rev = -1;
    ctx->me = NULL;
    ctx->reproot = NULL;
    ctx->revroot = NULL;

    if (xmatch_init(&ctx->xmatch, NULL) != 0) {
        goto FAIL;
    }

    ctx->match_result = -1;

    ctx->source_rev = -1;
    ctx->target_rev = -1;
    ctx->depth = SVN_DEPTH_UNKNOWN;
    /* weak ref */
    ctx->target = NULL;
    ctx->flags = 0;
    ctx->svnctx = NULL;
    init_bytes_array(&ctx->cwp);
    ctx->set_prop_name = NULL;
    ctx->text_checksum = NULL;

END:
    return ctx;

FAIL:
    if (ctx != NULL) {
        free(ctx);
    }
    goto END;
}

void
dav_ctx_destroy(dav_ctx_t *ctx)
{
    XML_ParserFree(ctx->p);
    ctx->p = NULL;
    ctx->youngest_rev = -1;
    if (ctx->me != NULL) {
        free(ctx->me);
        ctx->me = NULL;
    }
    if (ctx->reproot != NULL) {
        free(ctx->reproot);
        ctx->reproot = NULL;
    }
    if (ctx->revroot != NULL) {
        free(ctx->revroot);
        ctx->revroot = NULL;
    }
    xmatch_fini(&ctx->xmatch);
    ctx->match_result = -1;
    ctx->source_rev = -1;
    ctx->target_rev = -1;
    ctx->depth = SVN_DEPTH_UNKNOWN;
    /* weak ref */
    ctx->target = NULL;
    ctx->flags = 0;
    ctx->svnctx = NULL;
    fini_bytes_array(&ctx->cwp);
    if (ctx->set_prop_name != NULL) {
        free(ctx->set_prop_name);
        ctx->set_prop_name = NULL;
    }
    if (ctx->text_checksum != NULL) {
        free(ctx->text_checksum);
        ctx->text_checksum = NULL;
    }
    free(ctx);
}

static int
dav_pack_header_fields(svnc_ctx_t *ctx, svn_depth_t depth, size_t bodylen)
{
    char buf[64];

    if (http_add_header_field(&ctx->out, "Host", ctx->host) != 0) {
        TRRET(DAV_PACK_HEADER_FIELDS + 1);
    }

    if (http_add_header_field(&ctx->out, "Keep-Alive", "") != 0) {
        TRRET(DAV_PACK_HEADER_FIELDS + 2);
    }

    if (http_add_header_field(&ctx->out, "Connection", "keep-alive") != 0) {
        TRRET(DAV_PACK_HEADER_FIELDS + 3);
    }

    if (http_add_header_field(&ctx->out, "DAV",
            "http://subversion.tigris.org/xmlns/dav/svn/depth") != 0) {
        TRRET(DAV_PACK_HEADER_FIELDS + 4);
    }

    if (http_add_header_field(&ctx->out, "DAV",
            "http://subversion.tigris.org/xmlns/dav/svn/mergeinfo") != 0) {
        TRRET(DAV_PACK_HEADER_FIELDS + 5);
    }

    if (http_add_header_field(&ctx->out, "DAV",
            "http://subversion.tigris.org/xmlns/dav/svn/log-revprops") != 0) {
        TRRET(DAV_PACK_HEADER_FIELDS + 6);
    }

    if (depth > SVN_DEPTH_NONE) {
        snprintf(buf, 64, "%d", depth);
        if (http_add_header_field(&ctx->out, "Depth", buf) != 0) {
            TRRET(DAV_PACK_HEADER_FIELDS + 7);
        }
    }

    if (http_add_header_field(&ctx->out, "User-Agent", RA_CLIENT) != 0) {
        TRRET(DAV_PACK_HEADER_FIELDS + 8);
    }

    if (bodylen > 0) {
        if (http_add_header_field(&ctx->out, "Content-Type", "text/xml") != 0) {
            TRRET(DAV_PACK_HEADER_FIELDS + 9);
        }
        snprintf(buf, 64, "%ld", bodylen);
        if (http_add_header_field(&ctx->out, "Content-Length", buf) != 0) {
            TRRET(DAV_PACK_HEADER_FIELDS + 10);
        }
    }

    return 0;
}

struct _extra_header {
    const char *name;
    const char *value;
};
int
dav_request(svnc_ctx_t *ctx,
            const char *method,
            const char *uri,
            svn_depth_t depth,
            const char *body,
            size_t bodylen,
            const struct _extra_header extra_headers[])
{
    const struct _extra_header *eh;

    if (http_start_request(&ctx->out, method, uri) != 0) {
        TRRET(DAV_REQUEST + 1);
    }

    if (body == NULL) {
        bodylen = -1;
    }

    if (dav_pack_header_fields(ctx, depth, bodylen) != 0) {
        TRRET(DAV_REQUEST + 2);
    }

    if (extra_headers != NULL) {
        for (eh = extra_headers; eh->name != NULL; ++eh) {
            if (http_add_header_field(&ctx->out, eh->name, eh->value) != 0) {
                TRRET(DAV_REQUEST + 3);
            }

        }
    }

    if (http_end_of_header(&ctx->out) != 0) {
        TRRET(DAV_REQUEST + 3);
    }

    if (body != NULL) {
        if (http_add_body(&ctx->out, body, bodylen) != 0) {
            TRRET(DAV_REQUEST + 4);
        }
    }

    if (bytestream_produce_data(&ctx->out, ctx->fd) != 0) {
        TRRET(DAV_REQUEST + 5);
    }

    bytestream_rewind(&ctx->out);

    TRRET(0);
}

