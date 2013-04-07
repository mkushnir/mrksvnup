#ifndef SVNDIFF_H
#define SVNDIFF_H

#include <sys/types.h>
#include <sys/stat.h>

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

typedef struct _svndiff_wnd {
    ssize_t sview_offset;
    ssize_t sview_len;
    ssize_t tview_len;
    ssize_t inslen;
    ssize_t newlen;
    long orig_inslen;
    ssize_t inslen_check;
    array_t insns;
    bytes_t *bytes;
    char *tview;
} svndiff_wnd_t;

typedef struct _svndiff_doc {
    char version;
#define SD_STATE_VERSION 0
#define SD_STATE_NWND 1
#define SD_STATE_SVO 2
#define SD_STATE_SVL 3
#define SD_STATE_TVL 4
#define SD_STATE_INSL 5
#define SD_STATE_NEWL 6
#define SD_STATE_INSNS 7
#define SD_STATE_BYTES 8
    int parse_state;
    svndiff_wnd_t *current_wnd;
    bytes_t *base_checksum;
    /*
     * The currently opened file under edit (remote path, rev, mode, token, local path,
     * descriptor)
     */
    bytes_t *rp;
    long rev;
    unsigned int mod;
    bytes_t *ft;
    char *lp;
    int fd;
#define SD_FLAG_MOD_SET 0x01
#define SD_FLAG_SYMLINK_SET 0x02
    unsigned flags;
    struct stat sb;
    /* svndiff windows */
    array_t wnd;
} svndiff_doc_t;

int svndiff_parse_doc(const char *, const char *, svndiff_doc_t *);

int svndiff_doc_init(svndiff_doc_t *);
int svndiff_doc_fini(svndiff_doc_t *);
int svndiff_doc_dump(svndiff_doc_t *);

int svndiff_build_tview(svndiff_wnd_t *, svndiff_doc_t *);

#endif

