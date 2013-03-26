#ifndef SVNPROTO_H
#define SVNPROTO_H

#include "mrkcommon/array.h"

#include "mrksvnup/svnc.h"
#include "mrksvnup/bytestream.h"
#include "mrksvnup/svnproto_bytes.h"

/*
 * Parser.
 */

typedef struct _svnrange {
    off_t start;
    off_t end;
} svnrange_t;

/*
 * n    long
 * n?   long
 * n*   array_t of long
 * s    char *
 * s?   char *
 * s*   array_t of svnproto_bytes_t *
 * w    char *
 * w?   char *
 * w*   array_t of char *
 * r    cb, udata
 * r?   cb, udata
 * r*   cb, udata
 */
struct _svnproto_state;

typedef int (*svnproto_cb_t) (svnc_ctx_t *, bytestream_t *, struct _svnproto_state *, void *);

typedef struct _svnproto_state {
    svnrange_t r;
    long i;
    int next_read:1;
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
#   define TS_IGNORE 0x400
#   define TS_OUT (TS_START | TS_TOK_OUT | TS_NUM_OUT | TS_STRING_OUT | TS_IGNORE)
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
        ts == TS_IGNORE ? "TS_IGNORE" : \
        "<unknown>" \
    )
    int tokenizer_state;
} svnproto_state_t;
#define RLEN(v) ((v)->r.end - (v)->r.start)


#define ISSPACE(c) ((c) == ' ' || (c) == '\t' || (c) == '\n' || \
                    (c) == '\r' || (c) == '\0')
#define ISDIGIT(c) ((c) >= '0' && (c) <= '9')
#define ISALPHA(c) (((c) >= 'a' && (c) <= 'z') || ((c) >= 'A' && (c) <= 'Z'))
#define ISWORD(c) (ISALPHA(c) || ISDIGIT(c) || (c) == '-')

#define PARSE_NEED_MORE (-1)
/* end of data */
#define PARSE_EOD (-2)

typedef struct _svnproto_prop {
    svnproto_bytes_t *name;
    svnproto_bytes_t *value;
} svnproto_prop_t;

typedef struct _svnproto_dirent {
    svnproto_bytes_t *name;
    long rev; /* copied from get-dir response */
#define SVNP_KIND_NONE 0
#define SVNP_KIND_FILE 1
#define SVNP_KIND_DIR 2
#define SVNP_KIND_UNKNOWN 3
    int kind;
    ssize_t size;
} svnproto_dirent_t;

typedef struct _svnproto_fileent {
    svnproto_bytes_t *checksum;
    long rev;
    array_t props;
    array_t contents;
} svnproto_fileent_t;

/*
 * Parser
 */
int svnproto_unpack(svnc_ctx_t *, bytestream_t *, const char *, ...);
int svnproto_vunpack(svnc_ctx_t *, bytestream_t *, const char *, va_list);
int svnproto_command_response(svnc_ctx_t *, const char *, ...);

/*
 * Serializer.
 */
int pack_word(bytestream_t *, size_t, const char *);
int pack_number(bytestream_t *, int);
int pack_string(bytestream_t *, size_t, const char *);
int pack_list(bytestream_t *, svnproto_cb_t, svnc_ctx_t *, void *);

/*
 * Helpers
 */
void svnproto_init_long_array(array_t *);
void svnproto_dump_long_array(array_t *);
void svnproto_init_string_array(array_t *);
void svnproto_dump_string_array(array_t *);
void svnproto_init_dirent_array(array_t *ar);
void svnproto_dump_dirent_array(array_t *ar);
const char *svnproto_kind2str(int);
int svnproto_kind2int(const char *);

int svnproto_fileent_init(svnproto_fileent_t *);
int svnproto_fileent_fini(svnproto_fileent_t *);
void svnproto_fileent_dump(svnproto_fileent_t *);
/*
 * Protocol.
 */
int svnproto_setup(svnc_ctx_t *);
int svnproto_check_auth(svnc_ctx_t *);
int svnproto_get_latest_rev(svnc_ctx_t *, long *);
int svnproto_check_path(svnc_ctx_t *, const char *, long, int *);
#define GETFLAG_WANT_PROPS      0x01
#define GETFLAG_WANT_CONTENTS   0x02
#define GETFLAG_WANT_IPROPS     0x04
int svnproto_get_dir(svnc_ctx_t *, const char *, long, array_t *);
int svnproto_get_file(svnc_ctx_t *, const char *, long, int,
                      svnproto_fileent_t *);
int svnproto_reparent(svnc_ctx_t *, const char *);
#define UPFLAG_RECURSE 0x01
#define UPFLAG_SEND_COPY_FREOM_PARAM 0x02
int svnproto_update(svnc_ctx_t *, long, const char *, svn_depth_t, long,
                    svnproto_cb_t, void *);
#define SETPFLAG_START_EMPTY 0x01
int svnproto_set_path(svnc_ctx_t *, const char *, long, const char *,
                      svn_depth_t, long);
int svnproto_finish_report(svnc_ctx_t *);
int svnproto_abort_report(svnc_ctx_t *);

/* Editor */
int svnproto_editor(svnc_ctx_t *);
int svnproto_editor_verify_checksum(int, const svnproto_bytes_t *);

#endif
