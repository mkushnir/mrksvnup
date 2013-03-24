#ifndef SVNDIFF_H
#define SVNDIFF_H

#include "mrkcommon/array.h"
#include "mrkcommon/array.h"

#include "mrksvnup/svnproto.h"

/* include/svn_delta.h */
#define SVN_TXDELTA_SOURCE 0
#define SVN_TXDELTA_TARGET 1
#define SVN_TXDELTA_NEW 2

typedef struct _svndiff_insn {
    char code;
    ssize_t offset;
    ssize_t len;
} svndiff_insn_t;

/*
 * SVN\1
 * sview_offset
 * sview_len
 * tview_len
 * inslen
 * newlen
 *
 * 
 */

typedef struct _svndiff_doc {
#define SDFL_INITED 0x01
    int flags;
    char version;
    ssize_t sview_offset;
    ssize_t sview_len;
    ssize_t tview_len;
    ssize_t inslen;
    ssize_t newlen;
    long orig_inslen;
    array_t insns;
    array_t bytes;
#define SD_STATE_START 0
#define SD_STATE_VERSION 1
#define SD_STATE_SVO 2
#define SD_STATE_SVL 3
#define SD_STATE_TVL 4
#define SD_STATE_INSL 5
#define SD_STATE_NEWL 6
#define SD_STATE_INSNS 7
#define SD_STATE_BYTES 8
    int parse_state;
    ssize_t inslen_check;
    svnproto_bytes_t *base_checksum;
    /*
     * The currently opened file under edit (remote path, rev, local path,
     * token and descriptor)
     */
    svnproto_bytes_t *rp;
    long rev;
    char *lp;
    svnproto_bytes_t *ft;
    int fd;
} svndiff_doc_t;
#endif

int svndiff_parse_doc(const char *, const char *, svndiff_doc_t *);

void svndiff_init_insn_array(array_t *);
void svndiff_fini_insn_array(array_t *);
void svndiff_dump_insn_array(array_t *);

void svndiff_clear_current_file(svndiff_doc_t *);

int svndiff_doc_init(svndiff_doc_t *);
int svndiff_doc_fini(svndiff_doc_t *);
int svndiff_doc_apply(svndiff_doc_t *);
int svndiff_doc_dump(svndiff_doc_t *);
