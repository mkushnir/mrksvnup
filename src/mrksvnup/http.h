#ifndef HTTP_H
#define HTTP_H

#include "mrkcommon/bytestream.h"

#ifdef __cplusplus
extern "C" {
#endif

/* bytestream_consume */
#define PARSE_EOF (-1)
#define PARSE_NEED_MORE (-2)
/* end of data */
#define PARSE_EOD (-3)
#define PARSE_EMPTY (-4)

typedef struct _http_ctx {
#define PS_START 1
#define PS_STATUS_LINE 2
#define PS_HEADER_FIELD_IN 3
#define PS_HEADER_FIELD_OUT 4
#define PS_BODY_IN 5
#define PS_BODY 6
#define PSSTR(ps) ( \
        ps == PS_START ? "START" : \
        ps == PS_STATUS_LINE ? "STATUS_LINE" : \
        ps == PS_HEADER_FIELD_IN ? "HEADER_FIELD_IN" : \
        ps == PS_HEADER_FIELD_OUT ? "HEADER_FIELD_OUT" : \
        ps == PS_BODY_IN ? "BODY_IN" : \
        ps == PS_BODY ? "BODY" : \
        "<unknown>" \
        )
    char parser_state;
#define PS_CHUNK_SIZE 1
#define PS_CHUNK_DATA 2
#define CPSSTR(ps) ( \
        ps == PS_CHUNK_SIZE ? "CHUNK_SIZE" : \
        ps == PS_CHUNK_DATA ? "CHUNK_DATA" : \
        "<unknown>" \
        )
    char chunk_parser_state;
    byterange_t status_line;
    int version_major;
    int version_minor;
    int status;
    int bodysz;
    byterange_t current_header_name;
    byterange_t current_header_value;
#define PS_FLAG_CHUNKED 0x01
    int flags;
    byterange_t body;
    int current_chunk_size;
    byterange_t current_chunk;
    void *udata;
} http_ctx_t;

char * findcrlf(char *, int);

void http_ctx_init(http_ctx_t *);
void http_ctx_fini(http_ctx_t *);
http_ctx_t * http_ctx_new(void);
void http_ctx_destroy(http_ctx_t *);

typedef int (*http_cb_t) (http_ctx_t *, bytestream_t *, void *);

char *http_urlencode_reserved(const char *, size_t);
char *http_urldecode(char *);

int http_start_request(bytestream_t *, const char *, const char *);
int http_add_header_field(bytestream_t *, const char *, const char *);
int http_end_of_header(bytestream_t *);
int http_add_body(bytestream_t *, const char *, size_t);

int http_parse_response(int,
                        bytestream_t *,
                        http_cb_t,
                        http_cb_t,
                        http_cb_t,
                        void *);

#ifdef __cplusplus
}
#endif

#endif
