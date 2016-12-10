#ifndef SVNPROTO_H
#define SVNPROTO_H

#include "mrkcommon/array.h"
#include "mrkcommon/bytestream.h"

#include "mrksvnup/svnc.h"

#include "diag.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Parser.
 */

/*
 * n    read long
 * n?   read long
 * n*   read mnarray_t of long
 * w    read char *
 * w?   read char *
 * w*   read mnarray_t of char *
 * s    read mnbytes_t *
 * s?   read mnbytes_t *
 * s*   read mnarray_t of mnbytes_t *
 * S    cb, udata (read mnbytes_t *)
 * S?   cb, udata (read mnbytes_t *)
 * S*   cb, udata (read mnbytes_t *)
 * r    cb, udata (no read)
 * r?   cb, udata (no read)
 * r*   cb, udata (no read)
 */

typedef struct _svnproto_state {
    byterange_t r;
    long i;
#   define TS_START 0x01
#   define TS_TOK_IN 0x02
#   define TS_TOK_OUT 0x04
#   define TS_LIST_START 0x08
#   define TS_LIST_END 0x10
#   define TS_STRING_IN 0x20
#   define TS_STRING_CHECK 0x40
#   define TS_STRING_OUT 0x80
#   define TS_NUM_IN 0x100
#   define TS_NUM_OUT 0x200
#   define TS_END 0x400
#   define TS_OUT (TS_START | TS_TOK_OUT | TS_NUM_OUT | TS_STRING_OUT)
#   define TS_LIST (TS_LIST_START | TS_LIST_END)
#   define TS_DATA (TS_TOK_OUT | TS_NUM_OUT | TS_STRING_OUT)
#   define TSSTR(ts) ( \
        ts == TS_START ?  "TS_START" : \
        ts == TS_TOK_IN ?  "TS_TOK_IN" : \
        ts == TS_TOK_OUT ? "TS_TOK_OUT" : \
        ts == TS_LIST_START ?  "TS_LIST_START" : \
        ts == TS_LIST_END ? "TS_LIST_END" : \
        ts == TS_STRING_IN ?  "TS_STRING_IN" : \
        ts == TS_STRING_CHECK ? "TS_STRING_CHECK" : \
        ts == TS_STRING_OUT ? "TS_STRING_OUT" : \
        ts == TS_NUM_IN ?  "TS_NUM_IN" : \
        ts == TS_NUM_OUT ? "TS_NUM_OUT" : \
        ts == TS_END ? "TS_END" : \
        "<unknown>" \
    )
    int tokenizer_state;
    int backtrack:1;
} svnproto_state_t;
#define RLEN(v) ((v)->r.end - (v)->r.start)


#define ISSPACE(c) ((c) == ' ' || (c) == '\t' || (c) == '\n' || \
                    (c) == '\r' || (c) == '\0')
#define ISDIGIT(c) ((c) >= '0' && (c) <= '9')
#define ISALPHA(c) (((c) >= 'a' && (c) <= 'z') || ((c) >= 'A' && (c) <= 'Z'))
#define ISWORD(c) (ISALPHA(c) || ISDIGIT(c) || (c) == '-')

/* bytestream_consume */
#define PARSE_EOF (-1)
#define PARSE_NEED_MORE (-2)
/* end of data */
#define PARSE_EOD (-3)

/*
 * Parser
 */
int svnproto_unpack(svnc_ctx_t *, mnbytestream_t *, const char *, ...);
int svnproto_vunpack(svnc_ctx_t *, mnbytestream_t *, const char *, va_list);
int svnproto_command_response(svnc_ctx_t *, const char *, ...);

/* for user-defined calbacks to terminate r* r? S* S? */
#define SVNPROTO_UNPACK_NOMATCH_BACKTRACK (SVNPROTO_VUNPACK + 100)
#define SVNPROTO_UNPACK_NOMATCH_GOAHEAD (SVNPROTO_VUNPACK + 101)
#define SVNPROTO_IGNORE_VUNPACK(res) \
    ((((res) & DIAG_CLASS_MASK) == SVNPROTO_VUNPACK) ? 0 : (res))

/*
 * Serializer.
 */
int pack_word(mnbytestream_t *, size_t, const char *);
int pack_number(mnbytestream_t *, int);
int pack_string(mnbytestream_t *, size_t, const char *);
int pack_list(mnbytestream_t *, svnc_cb_t, svnc_ctx_t *, void *);

/*
 * Helpers
 */
void svnproto_init_dirent_array(mnarray_t *ar);
void svnproto_dump_dirent_array(mnarray_t *ar);

/*
 * Protocol.
 */
int svnproto_setup(svnc_ctx_t *);
int svnproto_check_auth(svnc_ctx_t *);
int svnproto_get_latest_rev(svnc_ctx_t *, long *);
int svnproto_check_path(svnc_ctx_t *, const char *, long, int *);
int svnproto_get_dir(svnc_ctx_t *, const char *, long, mnarray_t *);
int svnproto_get_file(svnc_ctx_t *, const char *, long, int,
                      svnc_fileent_t *);
int svnproto_reparent(svnc_ctx_t *, const char *);
int svnproto_update(svnc_ctx_t *, long, const char *, svn_depth_t, long,
                    svnc_cb_t, void *);
int svnproto_set_path(svnc_ctx_t *, const char *, long, const char *,
                      svn_depth_t, long);
int svnproto_finish_report(svnc_ctx_t *);
int svnproto_abort_report(svnc_ctx_t *);

/* Editor */
int svnproto_editor(svnc_ctx_t *);

#ifdef __cplusplus
}
#endif

#endif
