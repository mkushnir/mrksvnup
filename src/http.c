#include <errno.h>
#include <stdlib.h>
#include <string.h>

//#define TRRET_DEBUG
#include "mrkcommon/dumpm.h"
#include "mrkcommon/bytestream.h"
#include "mrkcommon/util.h"

#include "diag.h"

#include "mrksvnup/http.h"

#define RFC3986_RESEVED 0x01
#define RFC3986_OTHERS  0x02
#define RFC3986_UNRESERVED  0x04

#define ISSPACE(c) ((c) == ' ' || (c) == '\t')

static char charflags[256] = {
/*  0 1 2 3 4 5 6 7 8 9 a b c d e f 0 1 2 3 4 5 6 7 8 9 a b c d e f */
    2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,
    2,1,2,1,1,2,1,1,1,1,1,1,1,4,4,1,2,2,2,2,2,2,2,2,2,2,1,1,2,1,2,1,
    1,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,1,2,1,2,4,
    2,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,2,2,2,4,2,
    2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,
    2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,
    2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,
    2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,
};

char *
http_urlencode_reserved(const char *s, size_t sz)
{
    char *res;
    unsigned char c;
    unsigned int i, j;

    if ((res = malloc(sz * 3 + 1)) == NULL) {
        FAIL("malloc");
    }

    for (i = 0, j = 0; i < sz; ++i, ++j) {
        c = (unsigned char)(s[i]);

        if (!(charflags[c] & RFC3986_UNRESERVED)) {
            unsigned char cc = c >> 4;

            *(res + j++) = '%';
            if (cc < 10) {
                *(res + j++) = '0' + cc;
            } else {
                *(res + j++) = 'A' + cc - 10;
            }
            cc = (c & 0x0f);
            if (cc < 10) {
                *(res + j) = '0' + cc;
            } else {
                *(res + j) = 'A' + cc - 10;
            }

        } else {
            *(res + j) = (char)c;
        }
    }

    *(res + j) = '\0';

    return res;
}

char *
http_urldecode(char *s)
{
    char *src, *dst;
    for (src = dst = s; *src!= '\0'; ++src, ++dst)
    {
        if (*src == '%') {
            ++src;
            if (*src >= '0' && *src <= '9') {
                *dst = (*src - '0') << 4;
            } else if (*src >= 'a' && *src <= 'f') {
                *dst = (*src - 'a' + 10) << 4;
            } else if (*src >= 'A' && *src <= 'F') {
                *dst = (*src - 'A' + 10) << 4;
            } else {
                return NULL;
            }
            ++src;
            if (*src >= '0' && *src <= '9') {
                *dst |= (*src - '0');
            } else if (*src >= 'a' && *src <= 'f') {
                *dst |= (*src - 'a' + 10);
            } else if (*src >= 'A' && *src <= 'F') {
                *dst |= (*src - 'A' + 10);
            } else {
                return NULL;
            }
        } else {
            *dst = *src;
        }
    }

    return dst;
}

void
http_ctx_init(http_ctx_t *ctx)
{
    ctx->parser_state = PS_START;
    ctx->chunk_parser_state = 0;
    ctx->status_line.start = 0;
    ctx->status_line.end = 0;
    ctx->version_major = 0;
    ctx->version_minor = 0;
    ctx->status = 0;
    ctx->bodysz = -1;
    ctx->current_header_name.start = 0;
    ctx->current_header_name.end = 0;
    ctx->current_header_value.start = 0;
    ctx->current_header_value.end = 0;
    ctx->flags = 0;
    ctx->body.start = 0;
    ctx->body.end = 0;
    ctx->current_chunk_size = 0;
    ctx->current_chunk.start = 0;
    ctx->current_chunk.end = 0;
}

void
http_ctx_fini(http_ctx_t *ctx)
{
    http_ctx_init(ctx);
}

http_ctx_t *
http_ctx_new(void)
{
    http_ctx_t *ctx;

    if ((ctx = malloc(sizeof(http_ctx_t))) == NULL) {
        FAIL("malloc");
    }
    http_ctx_init(ctx);
    return ctx;
}

void
http_ctx_destroy(http_ctx_t *ctx)
{
    free(ctx);
}


int
http_start_request(bytestream_t *out,
                   const char *method,
                   const char *uri)
{
    int res = 0;
    size_t sz;

    /* strlen("  HTTP/1.1\r\n") = 12 */
    sz = strlen(method) + strlen(uri) + 12;
    /*
     * Reserve space in out at 4096 byte boundary.
     *
     * Do we really need it? bytestream_nprintf() grows its internal
     * buffer by BLOCKSZ chunks.
     */
    sz += (4096 - (sz % 4096));

    if ((res = bytestream_nprintf(out, sz,
                                  "%s %s HTTP/1.1\r\n",
                                  method, uri)) != 0) {
        res = HTTP_START_REQUEST + 1;
    }
    TRRET(res);
}

int
http_add_header_field(bytestream_t *out,
                const char *name,
                const char *value)
{
    int res = 0;
    size_t sz;

    /* strlen(": \r\n") = 4 */
    sz = strlen(name) + strlen(value) + 4;
    /*
     * Reserve space in out at 4096 byte boundary.
     *
     * Do we really need it? bytestream_nprintf() grows its internal
     * buffer by BLOCKSZ chunks.
     */
    sz += (4096 - (sz % 4096));

    if ((res = bytestream_nprintf(out, sz,
                                  "%s: %s\r\n",
                                  name, value)) != 0) {
        res = HTTP_ADD_HEADER_FIELD + 1;
    }
    TRRET(res);
}

int
http_end_of_header(bytestream_t *out)
{
    int res = 0;

    if ((res = bytestream_cat(out, 2, "\r\n")) != 0) {
        res = HTTP_END_OF_HEADER + 1;
    }
    TRRET(res);
}

int
http_add_body(bytestream_t *out, const char *body, size_t sz)
{
    int res = 0;

    if ((res = bytestream_cat(out, sz, body)) != 0) {
        res = HTTP_ADD_BODY + 1;
    }
    TRRET(res);
}




/*
 * Find the CR and LF pair in s, up to sz bytes long.
 */
char *
findcrlf(char *s, int sz)
{
    char *ss;

    for (ss = s + sz; s < ss; ++s) {
        if (*s == '\r') {
            if ((s + 1) < ss && *(s + 1) == '\n') {
                return s;
            }
        }
    }
    return NULL;
}

UNUSED static char *
findsp(char *s, int sz)
{
    char *ss;

    for (ss = s + sz; s < ss; ++s) {
        if (ISSPACE(*s)) {
            return s;
        }
    }
    return NULL;
}

static char *
findnosp(char *s, int sz)
{
    char *ss;

    for (ss = s + sz; s < ss; ++s) {
        if (!ISSPACE(*s)) {
            return s;
        }
    }
    return NULL;
}

static int
process_header(http_ctx_t *ctx, bytestream_t *in)
{
    //TRACE("current_header_name=%s",
    //      SDATA(in, ctx->current_header_name.start));
    //TRACE("current_header_value=%s",
    //      SDATA(in, ctx->current_header_value.start));

    if (strcasecmp(SDATA(in, ctx->current_header_name.start),
                   "content-length") == 0) {

        ctx->bodysz = strtol(SDATA(in, ctx->current_header_value.start),
                            NULL, 10);

    } else if (strcasecmp(SDATA(in, ctx->current_header_name.start),
                          "transfer-encoding") == 0) {

        if (strcmp(SDATA(in, ctx->current_header_value.start),
                   "chunked") == 0) {

            ctx->flags |= PS_FLAG_CHUNKED;
            ctx->chunk_parser_state = PS_CHUNK_SIZE;
        }
    }

    return PARSE_NEED_MORE;
}

static int
process_body(http_ctx_t *ctx,
             bytestream_t *in,
             http_cb_t body_cb,
             void *udata)
{
    //TRACE("flags=%d body sz=%d start=%ld end=%ld",
    //      ctx->flags, ctx->bodysz, ctx->body.start, ctx->body.end);
    //D16(SDATA(in, ctx->body.start), ctx->body.end - ctx->body.start);

    if (ctx->flags & PS_FLAG_CHUNKED) {
        if (ctx->chunk_parser_state == PS_CHUNK_SIZE) {
            char *end, *tmp = NULL;

            if ((end = findcrlf(SDATA(in, ctx->current_chunk.start),
                                SEOD(in) - ctx->current_chunk.start))
                == NULL) {

                //D8(SDATA(in, ctx->current_chunk.start),
                //   MIN(128, SEOD(in) - ctx->current_chunk.start));

                SADVANCEPOS(in, SEOD(in) - SPOS(in));
                TRRET(PARSE_NEED_MORE);
            }
            ctx->current_chunk_size = strtol(SDATA(in,
                        ctx->current_chunk.start), &tmp, 16);

            if (tmp != end || ctx->current_chunk_size  < 0) {
                D32(SDATA(in, ctx->current_chunk.start), 32);
                TRRET(PROCESS_BODY + 1);
            }

            //TRACE("tmp=%p end=%p", tmp, end);
            //TRACE("found chunk %d", ctx->current_chunk_size);

            SADVANCEPOS(in, end - SPDATA(in) + 2);
            ctx->current_chunk.start = SPOS(in);
            ctx->current_chunk.end = ctx->current_chunk.start;
            ctx->chunk_parser_state = PS_CHUNK_DATA;

            TRRET(PARSE_NEED_MORE);

        } else if (ctx->chunk_parser_state == PS_CHUNK_DATA) {
            int needed, navail;

            needed = ctx->current_chunk_size -
                     (ctx->current_chunk.end - ctx->current_chunk.start);

            navail = SEOD(in) - SPOS(in);

            if (needed > 0) {
                int incr = MIN(needed, navail);

                //TRACE("chunk incomplete: sz=%d/%ld",
                //      ctx->current_chunk_size,
                //      ctx->current_chunk.end - ctx->current_chunk.start);

                ctx->current_chunk.end += incr;
                SADVANCEPOS(in, incr);

                TRRET(PARSE_NEED_MORE);

            } else {
                //TRACE("chunk complete: sz=%d/%ld",
                //      ctx->current_chunk_size,
                //      ctx->current_chunk.end - ctx->current_chunk.start);
                //D32(SDATA(in, ctx->current_chunk.start),
                //    ctx->current_chunk.end - ctx->current_chunk.start);

                if (body_cb != NULL) {
                    if (body_cb(ctx, in, udata) != 0) {
                        TRRET(PROCESS_BODY + 2);
                    }
                }

                /* prepare to the next chunk */
                ctx->chunk_parser_state = PS_CHUNK_SIZE;
                /* account for \r\n */
                SADVANCEPOS(in, 2);
                ctx->current_chunk.start = SPOS(in);
                ctx->current_chunk.end = ctx->current_chunk.start;

                if (ctx->current_chunk_size > 0) {
                    TRRET(PARSE_NEED_MORE);
                } else {
                    TRRET(0);
                }
            }
        }
    } else {
        ssize_t navail, accumulated, needed, incr;

        navail = SEOD(in) - SPOS(in);
        accumulated = ctx->body.end - ctx->body.start;
        needed = ctx->bodysz - accumulated;
        incr = MIN(navail, needed);

        ctx->body.end += incr;
        ctx->current_chunk.end = ctx->body.end;
        SADVANCEPOS(in, incr);
        accumulated = ctx->body.end - ctx->body.start;


#ifdef TRRET_DEBUG
        TRACE("bodysz=%d navail=%ld accumulated=%ld incr=%ld "
              "SPOS=%ld SEOD=%ld",
              ctx->bodysz,
              navail,
              accumulated,
              incr,
              SPOS(in),
              SEOD(in));
#endif
        //D16(SPDATA(in), navail);

        if (accumulated < ctx->bodysz) {
            TRRET(PARSE_NEED_MORE);
        } else {
            if (body_cb != NULL) {
                if (body_cb(ctx, in, udata) != 0) {
                    TRRET(PROCESS_BODY + 3);
                }
            }
            TRRET(0);
        }
    }
    
    TRRET(PROCESS_BODY + 4);
}


int
http_parse_response(int fd,
                    bytestream_t *in,
                    http_cb_t header_cb,
                    http_cb_t body_cb,
                    void *udata)
{
    int res = PARSE_NEED_MORE;
    http_ctx_t *ctx = in->udata;

    http_ctx_init(ctx);

    while (res == PARSE_NEED_MORE) {

        if (SNEEDMORE(in)) {
            if (bytestream_consume_data(in, fd) != 0) {
                /* this must be treated as EOF condition */
                res = 0;
                break;
            }
        }

        if (ctx->parser_state == PS_START) {
            ctx->status_line.start = SPOS(in);
            ctx->parser_state = PS_STATUS_LINE;

        } else if (ctx->parser_state == PS_STATUS_LINE) {
            char *end, *tmp, *tmp1;

            if ((end = findcrlf(SDATA(in, ctx->status_line.start),
                                SEOD(in) - ctx->status_line.start)) == NULL) {

                /* PARSE_NEED_MORE */
                SADVANCEPOS(in, SEOD(in) - SPOS(in));
                continue;
            }

            ctx->status_line.end = SDPOS(in, end);

            if ((tmp1 = findnosp(SDATA(in, ctx->status_line.start),
                                 ctx->status_line.end -
                                 ctx->status_line.start)) == NULL) {

                TRRET(HTTP_PARSE_RESPONSE + 1);
            }

            tmp = tmp1;

            /* strlen("HTTP/") = 5 */
            if (strncasecmp(tmp, "HTTP/", 5) != 0) {
                TRRET(HTTP_PARSE_RESPONSE + 2);
            }

            tmp += 5;

            ctx->version_major = strtol((const char *)tmp, &tmp1, 10);

            if (ctx->version_major == 0 &&
                (errno == EINVAL || errno == ERANGE)) {

                TRRET(HTTP_PARSE_RESPONSE + 3);
            }

            if (*tmp1 != '.') {
                TRRET(HTTP_PARSE_RESPONSE + 4);
            }

            /* advance by a single dot */
            tmp = tmp1 + 1;

            ctx->version_minor = strtol(tmp, &tmp1, 10);

            if (ctx->version_minor == 0 &&
                (errno == EINVAL || errno == ERANGE)) {

                TRRET(HTTP_PARSE_RESPONSE + 5);
            }

            tmp = tmp1;

            /*
             * Status code
             */
            if ((tmp1 = findnosp(tmp, end - tmp)) == NULL) {
                TRRET(HTTP_PARSE_RESPONSE + 6);
            }

            tmp = tmp1;

            ctx->status = strtol(tmp, &tmp1, 10);

            if (ctx->status == 0 && (errno == EINVAL || errno == ERANGE)) {
                TRRET(HTTP_PARSE_RESPONSE + 7);
            }

            /*
             * Reason phrase (ignore)
             */

            ctx->parser_state = PS_HEADER_FIELD_IN;
            SADVANCEPOS(in, end - SPDATA(in) + 2);

        } else if (ctx->parser_state == PS_HEADER_FIELD_IN) {
            ctx->current_header_name.start = SPOS(in);
            ctx->parser_state = PS_HEADER_FIELD_OUT;
            //TRACE("SPOS=%ld SEOD=%ld", SPOS(in), SEOD(in));

        } else if (ctx->parser_state == PS_HEADER_FIELD_OUT) {
            char *end, *tmp;

            /*
             * XXX Header line continuations are not handled ATM.
             */
            if ((end = findcrlf(SDATA(in, ctx->current_header_name.start),
                                SEOD(in) -
                                ctx->current_header_name.start)) == NULL) {

                /* PARSE_NEED_MORE */
                //TRACE("SEOD=%ld SPOS=%ld", SEOD(in), SPOS(in));
                SADVANCEPOS(in, SEOD(in) - SPOS(in));
                continue;
            }

            //TRACE("current_header_name=%p end=%p",
            //      ctx->current_header_name, end);
            //D8(ctx->current_header_name, 8);
            //D8(end, 8);

            if (end == SDATA(in, ctx->current_header_name.start)) {
                /* empty line */
                SPOS(in) += 2;
                ctx->body.start = SPOS(in);
                ctx->body.end = ctx->body.start;
                /* in case the body is chunked */
                ctx->current_chunk.start = SPOS(in);
                ctx->current_chunk.end = ctx->current_chunk.start;
                ctx->parser_state = PS_BODY_IN;

            } else {
                /* next header */
                ctx->current_header_value.end = SDPOS(in, end);

                if ((tmp = memchr(SDATA(in, ctx->current_header_name.start),
                                  ':',
                                  ctx->current_header_value.end -
                                  ctx->current_header_name.start)) == NULL) {

                    TRRET(HTTP_PARSE_RESPONSE + 8);
                }

                ctx->current_header_name.end = SDPOS(in, tmp);
                *tmp++ = '\0';
                tmp = findnosp(tmp, end - tmp);
                ctx->current_header_value.start = SDPOS(in, tmp);
                *end = '\0';

                if ((res = process_header(ctx, in)) != 0) {
                    if (res != PARSE_NEED_MORE) {
                        TRRET(HTTP_PARSE_RESPONSE + 9);
                    }
                }

                if (header_cb != NULL) {
                    if (header_cb(ctx, in, udata) != 0) {
                        TRRET(HTTP_PARSE_RESPONSE + 10);
                    }
                }

                ctx->parser_state = PS_HEADER_FIELD_IN;
            }

            SADVANCEPOS(in, end - SPDATA(in) + 2);

        } else if (ctx->parser_state == PS_BODY_IN) {

            if ((res = process_body(ctx, in, body_cb, udata)) != 0) {
                if (res != PARSE_NEED_MORE) {
                    TRRET(HTTP_PARSE_RESPONSE + 11);
                }
            }

            //D32(SDATA(in, ctx->current_chunk.start),
            //          ctx->current_chunk.end - ctx->current_chunk.start);
            ctx->parser_state = PS_BODY;

        } else if (ctx->parser_state == PS_BODY) {

            if ((res = process_body(ctx, in, body_cb, udata)) != 0) {
                if (res != PARSE_NEED_MORE) {
                    TRRET(HTTP_PARSE_RESPONSE + 12);
                }
            }

            //D32(SDATA(in, ctx->current_chunk.start),
            //          ctx->current_chunk.end - ctx->current_chunk.start);

        } else {
            //TRACE("state=%s", PSSTR(ctx->parser_state));
            TRRET(HTTP_PARSE_RESPONSE + 13);
        }
    }

    http_ctx_fini(ctx);

    TRRET(res);
}

