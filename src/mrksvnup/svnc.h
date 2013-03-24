#ifndef SVNC_H
#define SVNC_H

#include <netdb.h>
#include <sys/socket.h>
#include <sys/types.h>

#include "mrksvnup/bytestream.h"
#include "mrksvnup/svnproto_bytes.h"

#define SVN_DEFAULT_PORT 3690

/* pretend to be official svn */
#define RA_CLIENT "SVN/1.7.7"

typedef struct _svnc_ctx {
    char *url;
    char *host;
    int port;
    char *path;
    struct addrinfo *ai;
    /*
     * Client socket descriptor
     */
    int fd;
    /* input buffer */
    bytestream_t in;
    /* output buffer */
    bytestream_t out;
#define SVNC_AUTH_MECH_OK 0x01
    uint64_t flags;

    /*
     * Local directory that corresponds to the remote root
     */
    char *localroot;

    struct _error_info {
        long apr_error;
        svnproto_bytes_t *message;
        svnproto_bytes_t *file;
        long line;
    } last_error;

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

typedef enum _svn_depth {
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

int svn_url_parse(const char *, char **, int *, char **);
svnc_ctx_t *svnc_new(const char *, const char *);
int svnc_connect(svnc_ctx_t *);
void svnc_clear_last_error(svnc_ctx_t *);
int svnc_close(svnc_ctx_t *);
int svnc_destroy(svnc_ctx_t *);
int svnc_debug_open(svnc_ctx_t *, const char *);

#endif
