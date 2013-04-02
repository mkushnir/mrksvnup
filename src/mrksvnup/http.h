#ifndef HTTP_H
#define HTTP_H

#include "mrkcommon/bytestream.h"
#include "mrksvnup/svnc.h"

#define PARSE_NEED_MORE (-1)
/* end of data */
#define PARSE_EOD (-2)

typedef struct _http_state {
#define PS_START 0x01
#define PS_STATUS_LINE 0x02
#define PS_HEADER_FIELD_START 0x03
#define PS_HEADER_FIELD 0x03
#define PS_BODY 0x04
#define PSSTR(ps) ( \
        ps == PS_STATUS_LINE ? "STATUS_LINE" : \
        ps == PS_HEADER_FIELD ? "HEADER_FIELD" : \
        ps == PS_BODY ? "BODY" : \
        "<unknown>" \
        )
    int parser_state;

    char *status_line;
    int version_major;
    int version_minor;
    int status;
    int bodysz;
    char *current_header_name;
    char *current_header_value;
} http_state_t;

typedef int (*http_cb_t) (bytestream_t *, struct _http_state *, void *);

char *http_urlencode_reserved(const char *, size_t);
char *http_urldecode(char *);

int http_start_request(bytestream_t *, const char *, const char *);
int http_add_header_field(bytestream_t *, const char *, const char *);
int http_end_of_header(bytestream_t *);
int http_add_body(bytestream_t *, const char *, size_t);

int http_parse_response(int, bytestream_t *, http_cb_t, void *);
#endif
