#ifndef SVNC_H
#define SVNC_H

#include <netdb.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <db.h>
#include <fcntl.h>
#include <limits.h>
#include <openssl/ssl.h>

#include "mrkcommon/array.h"
#include "mrkcommon/bytestream.h"

#include "mrksvnup/version.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SVN_DEFAULT_PORT 3690
#define HTTP_DEFAULT_PORT 80
#define HTTPS_DEFAULT_PORT 443

#define RA_CLIENT (PACKAGE_NAME "/" PACKAGE_VERSION)

#define REVFILE ".svnup.rev"
#define REPOFILE ".svnup.repo"
#define CACHEFILE ".svnup.cache"
#define LOCKFILE ".svnup.lock"

typedef enum _svn_depth {
    SVN_DEPTH_NONE = -1,
    SVN_DEPTH_UNKNOWN,
    SVN_DEPTH_EXCLUDE,
    SVN_DEPTH_EMPTY,
    SVN_DEPTH_FILES,
    SVN_DEPTH_IMMEDIATES,
    SVN_DEPTH_INFINITY,
} svn_depth_t;
#define SVN_DEPTH_STR(d) \
    ((d) == SVN_DEPTH_UNKNOWN ? "unknown" : \
     (d) == SVN_DEPTH_EXCLUDE ? "exclude" : \
     (d) == SVN_DEPTH_EMPTY ? "empty" : \
     (d) == SVN_DEPTH_FILES ? "files" : \
     (d) == SVN_DEPTH_IMMEDIATES ? "immediates" : \
     (d) == SVN_DEPTH_INFINITY ? "infinity" : \
     "unknown")

typedef struct _bytes {
    size_t sz;
    char data[];
} mnbytes_t;
/*
 * This macro is also defined in bytestream.h for a different structure.
 * Surprisingly, it appears to be exactly the same.
 *
 */
#ifndef BDATA
#   define BDATA(b) ((b) != NULL ? (b)->data : NULL)
#endif

typedef struct _svnc_prop {
    mnbytes_t *name;
    mnbytes_t *value;
} svnc_prop_t;

typedef struct _svnc_fileent {
    mnbytes_t *checksum;
    long rev;
    mnarray_t props;
    mnarray_t contents;
} svnc_fileent_t;

typedef struct _svnc_dirent {
    mnbytes_t *name;
    long rev; /* copied from get-dir response */
    int kind;
    ssize_t size;
} svnc_dirent_t;

struct _svnc_ctx;

typedef int (*svnc_cb_t)(struct _svnc_ctx *, mnbytestream_t *, void *, void *);

typedef struct _svnc_ctx {
#define SVNC_SCHEME_SVN 1
#define SVNC_SCHEME_HTTP 2
#define SVNC_SCHEME_HTTPS 3
    int scheme;
    char *url;
    char *host;
    int port;
    char *path;
    struct addrinfo *ai;
    /*
     * Client socket descriptor
     */
    int fd;
    SSL_CTX *sslctx;
    SSL *ssl;
    /* input buffer */
    mnbytestream_t in;
    /* output buffer */
    mnbytestream_t out;
#define SVNC_AUTH_MECH_OK 0x01
#define SVNC_NOCACHE 0x02
#define SVNC_FLUSHCACHE 0x04
#define SVNC_REPAIR 0x08
#define SVNC_CLEAR_LOCKFILE 0x10
#define SVNC_TOLERANT 0x20
#define SVNC_PRINT_REVISION 0x40

    unsigned int flags;
    int debug_level;

    /*
     * Local directory that corresponds to the remote root
     */
    char *localroot;

    /* file cache */
    char *cacheroot;
    char *cachepath;
    DB *cachedb;

    struct _error_info {
        long apr_error;
        mnbytes_t *message;
        mnbytes_t *file;
        long line;
    } last_error;

    int (*get_latest_rev)(struct _svnc_ctx *, long *);
    int (*check_path)(struct _svnc_ctx *, const char *, long, int *);
    int (*setup)(struct _svnc_ctx *);
    int (*set_path)(struct _svnc_ctx *, const char *, long, const char *,
                    svn_depth_t, long);
    int (*finish_report)(struct _svnc_ctx *);
    int (*get_file)(struct _svnc_ctx *, const char *,long, int,
                    svnc_fileent_t *);
    int (*update)(struct _svnc_ctx *, long, const char *,svn_depth_t,
                  long, svnc_cb_t, void *);
    int (*editor)(struct _svnc_ctx *);

    const char *executable_prop_name;
    const char *special_prop_name;

    /* this is not controlled by svnc module */
    void *udata;

    /* not used ATM */
#define SVNCAP_EDIT_PIPELINE    0x01
#define SVNCAP_SVNDIFF1         0x02
#define SVNCAP_ABSENT_ENTRIES   0x04
#define SVNCAP_COMMIT_REVPROPS  0x08
#define SVNCAP_MERGEINFO        0x10
#define SVNCAP_DEPTH            0x20
#define SVNCAP_ATOMIC_REVPROPS  0x40
    uint64_t server_caps;
    uint64_t client_caps;
} svnc_ctx_t;

#define SVNC_KIND_NONE 0
#define SVNC_KIND_FILE 1
#define SVNC_KIND_DIR 2
#define SVNC_KIND_UNKNOWN 3

int all_spaces(const char *, size_t);
void bin2hex(char *, const unsigned char *, size_t);
const char *svnc_kind2str(int);
int svnc_kind2int(const char *);
svnc_ctx_t *svnc_new(const char *, const char *, unsigned int, int);
int svnc_connect(svnc_ctx_t *);
int svnc_socket_reconnect(svnc_ctx_t *);
void svnc_clear_last_error(svnc_ctx_t *);
void svnc_print_last_error(svnc_ctx_t *);
int svnc_close(svnc_ctx_t *);
int svnc_destroy(svnc_ctx_t *);
int svnc_save_checksum(svnc_ctx_t *, const char *, mnbytes_t *);
int svnc_delete_checksum(svnc_ctx_t *, const char *);
int svnc_first_checksum(svnc_ctx_t *, char **, mnbytes_t **);
int svnc_next_checksum(svnc_ctx_t *, char **, mnbytes_t **);
int svnc_debug_open(svnc_ctx_t *, const char *);

/* Utilities */
int svn_url_parse(const char *, int *, char **, int *, char **);
void svnc_check_integrity(svnc_ctx_t *, long);
int svnc_fileent_init(svnc_fileent_t *);
int svnc_fileent_fini(svnc_fileent_t *);
void svnc_fileent_dump(svnc_fileent_t *);
void init_long_array(mnarray_t *);
void dump_long_array(mnarray_t *);
void init_string_array(mnarray_t *);
void dump_string_array(mnarray_t *);
void fini_string_array(mnarray_t *);
void init_bytes_array(mnarray_t *ar);
void fini_bytes_array(mnarray_t *ar);
void dump_bytes_array(mnarray_t *ar);
mnbytes_t *bytes_from_str(const char *);
mnbytes_t *bytes_from_strn(const char *, size_t);
mnbytes_t *bytes_from_mem(const char *, size_t);

/* Protocol API flags */

/* get-file */
#define GETFLAG_WANT_PROPS      0x01
#define GETFLAG_WANT_CONTENTS   0x02
#define GETFLAG_WANT_IPROPS     0x04

/* update */
#define UPFLAG_RECURSE 0x08
#define UPFLAG_SEND_COPY_FREOM_PARAM 0x10

/* set-path */
#define SETPFLAG_START_EMPTY 0x20

#ifdef __cplusplus
}
#endif

#endif
