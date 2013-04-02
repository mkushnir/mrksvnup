#include <errno.h>
#include <stdlib.h>
#include <string.h>

#define TRRET_DEBUG
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
static char *
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

int
http_parse_response(int fd,
                    UNUSED bytestream_t *in,
                    UNUSED http_cb_t cb,
                    UNUSED void *udata)
{
    int res = PARSE_NEED_MORE;
    http_state_t *st = in->udata;

    while (res == PARSE_NEED_MORE) {
        if (SNEEDMORE(in)) {
            if ((res = bytestream_consume_data(in, fd)) != 0) {
                /* this must be treated as EOF condition */
                res = PARSE_EOD;
                break;
            }
        }

        if (st->parser_state == PS_START) {

            st->status_line = SDATA(in, SPOS(in));
            st->parser_state = PS_STATUS_LINE;

        } else if (st->parser_state == PS_STATUS_LINE) {
            char *end, *tmp, *tmp1;

            if ((end = findcrlf(SPDATA(in), SEOD(in) - SPOS(in))) == NULL) {

                /* PARSE_NEED_MORE */
                SADVANCEPOS(in, SEOD(in) - SPOS(in));
                continue;
            }

            if ((tmp1 = findnosp(st->status_line,
                                 end - st->status_line)) == NULL) {

                TRRET(HTTP_PARSE_RESPONSE + 1);
            }

            tmp = tmp1;

            /* stlen("HTTP/") = 5 */
            if (strncasecmp(tmp, "HTTP/", 5) != 0) {
                TRRET(HTTP_PARSE_RESPONSE + 1);
            }

            tmp += 5;

            st->version_major = strtol((const char *)tmp, &tmp1, 10);

            if (st->version_major == 0 &&
                (errno == EINVAL || errno == ERANGE)) {

                TRRET(HTTP_PARSE_RESPONSE + 1);
            }

            if (*tmp1 != '.') {
                TRRET(HTTP_PARSE_RESPONSE + 1);
            }

            /* advance by a single dot */
            tmp = tmp1 + 1;

            st->version_minor = strtol(tmp, &tmp1, 10);

            if (st->version_minor == 0 &&
                (errno == EINVAL || errno == ERANGE)) {

                TRRET(HTTP_PARSE_RESPONSE + 1);
            }

            tmp = tmp1;

            /*
             * Status code
             */
            if ((tmp1 = findnosp(tmp, end - tmp)) == NULL) {
                TRRET(HTTP_PARSE_RESPONSE + 1);
            }

            tmp = tmp1;

            st->status = strtol(tmp, &tmp1, 10);

            if (st->status == 0 && (errno == EINVAL || errno == ERANGE)) {
                TRRET(HTTP_PARSE_RESPONSE + 1);
            }

            /*
             * Reason phrase (ignore)
             */

            SADVANCEPOS(in, end - SPDATA(in) + 2);
            st->parser_state = PS_HEADER_FIELD_START;

        } else if (st->parser_state == PS_HEADER_FIELD_START) {
            st->current_header_name = SDATA(in, SPOS(in));
            st->parser_state = PS_HEADER_FIELD;

        } else if (st->parser_state == PS_HEADER_FIELD) {
            char *end, *tmp;

            /*
             * XXX Header line continuations are not handled ATM.
             */
            if ((end = findcrlf(SDATA(in, SPOS(in)),
                                SEOD(in) - SPOS(in))) == NULL) {

                /* PARSE_NEED_MORE */
                continue;
            }

            if (end == st->current_header_name) {
                /* empty line */
                SPOS(in) += 2;
                st->parser_state = PS_BODY;

            } else {
                /* next header */

                if ((tmp = memchr(st->current_header_name, ':',
                                  end - st->current_header_name)) == NULL) {
                    TRRET(HTTP_PARSE_RESPONSE + 1);
                }

                *tmp++ = '\0';
                st->current_header_value = findnosp(tmp, end - tmp);
                *end = '\0';


                if (cb != NULL) {
                    if ((res = cb(in, st, udata)) != 0) {
                        if (res == PARSE_NEED_MORE) {
                            continue;
                        }
                        TRRET(HTTP_PARSE_RESPONSE + 1);
                    }
                }
                SADVANCEPOS(in, end - SPDATA(in) + 2);
            }

        } else if (st->parser_state == PS_BODY) {

        } else {
            TRRET(HTTP_PARSE_RESPONSE + 1);
        }
    }

    TRRET(res);
}

