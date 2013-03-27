#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdlib.h>
#include <limits.h>
#include <inttypes.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>

#include "diag.h"
#include "mrkcommon/util.h"
//#define TRRET_DEBUG
#include "mrkcommon/dumpm.h"
#include "mrkcommon/traversedir.h"

#include "mrksvnup/svnc.h"
#include "mrksvnup/svncdir.h"
#include "mrksvnup/svnproto.h"

/* keep it in sync with enum _svn_depth */
UNUSED static const char *svn_depth_str[] = {
    "empty",
    "files",
    "immediates",
    "infinity",
};

int
svn_url_parse(const char *url, char **host, int *port, char **path)
{
    char *p0, *p1, *p2;
    size_t sz;

    assert(*host == NULL && *path == NULL);

    sz = strlen(url);

    if ((p0 = strnstr(url, "svn://", sz)) != url) {
        TRRET(SVN_URL_PARSE + 1);
    }

    p0 += 6;
    sz -= 6;

    if ((p1 = strchr(p0, '/')) == NULL) {
        TRRET(SVN_URL_PARSE + 2);
    }

    sz -= (p1 - p0);

    if ((*host = malloc(p1 - p0 + 1)) == NULL) {
        TRRET(SVN_URL_PARSE + 3);
    }

    memcpy(*host, p0, p1 - p0);
    (*host)[p1 - p0] = '\0';

    if ((p2 = strstr(*host, ":")) != NULL) {
        char *endptr = NULL;

        *p2 = '\0';
        ++p2;

        *port = strtoimax(p2, &endptr, 10);
        if ((*port) == 0) {
            if (p2 == endptr || errno == ERANGE || errno == EINVAL) {
                TRRET(SVN_URL_PARSE + 4);
            }
        }
    } else {
        *port = SVN_DEFAULT_PORT;
    }

    if ((*path = malloc(sz + 1)) == NULL) {
        TRRET(SVN_URL_PARSE + 5);
    }

    memcpy(*path, p1, sz);
    (*path)[sz] = '\0';

    return (0);
}

int
svnc_save_checksum(svnc_ctx_t *ctx,
                   const char *path,
                   svnproto_bytes_t *checksum)
{
    int res = 0;
    DBT k, v;
    k.size = strlen(path);
    k.data = (void *)path;
    v.size = checksum->sz;
    v.data = checksum->data;

    if (ctx->cachedb->put(ctx->cachedb, &k, &v, 0) != 0) {
        res = SVNC_SAVE_CHECKSUM + 1;
    }
    TRRET(res);
}

int
svnc_delete_checksum(svnc_ctx_t *ctx,
                   const char *path)
{
    int res = 0;
    DBT k;
    k.size = strlen(path);
    k.data = (void *)path;

    if (ctx->cachedb->del(ctx->cachedb, &k, 0) != 0) {
        res = SVNC_DELETE_CHECKSUM + 1;
    }
    TRRET(res);
}

int
svnc_first_checksum(svnc_ctx_t *ctx,
                     char **path,
                     svnproto_bytes_t **checksum)
{
    int res = 0;
    DBT k, v;
    k.size = 0;
    k.data = NULL;
    v.size = 0;
    v.data = NULL;

    if (ctx->cachedb->seq(ctx->cachedb, &k, &v, R_FIRST) != 0) {
        res = SVNC_FIRST_CHECKSUM + 1;
    }

    if ((*path = malloc(k.size + 1)) == NULL) {
        FAIL("malloc");
    }
    memcpy(*path, k.data, k.size);
    *((*path) + k.size) = '\0';

    if ((*checksum = malloc(sizeof(svnproto_bytes_t) + v.size)) == NULL) {
        FAIL("malloc");
    }
    (*checksum)->sz = v.size;
    memcpy((*checksum)->data, v.data, v.size);

    TRRET(res);
}

int
svnc_next_checksum(svnc_ctx_t *ctx,
                   char **path,
                   svnproto_bytes_t **checksum)
{
    int res = 0;
    DBT k, v;
    k.size = 0;
    k.data = NULL;
    v.size = 0;
    v.data = NULL;

    if (ctx->cachedb->seq(ctx->cachedb, &k, &v, R_NEXT) != 0) {
        res = SVNC_NEXT_CHECKSUM + 1;
    }

    if ((*path = malloc(k.size + 1)) == NULL) {
        FAIL("malloc");
    }
    memcpy(*path, k.data, k.size);
    *((*path) + k.size) = '\0';

    if ((*checksum = malloc(sizeof(svnproto_bytes_t) + v.size)) == NULL) {
        FAIL("malloc");
    }
    (*checksum)->sz = v.size;
    memcpy((*checksum)->data, v.data, v.size);

    TRRET(res);
}

svnc_ctx_t *
svnc_new(const char *url,
         const char *localroot,
         unsigned flags)
{
    svnc_ctx_t *ctx;
    struct addrinfo hints;
    char portstr[32];

    if ((ctx = malloc(sizeof(svnc_ctx_t))) == NULL) {
        TRRETNULL(SVNC_NEW + 1);
    }

    /* XXX manually initialize it here */
    ctx->fd = -1;
    ctx->url = NULL;
    ctx->host = NULL;
    ctx->port = 0;
    ctx->path = NULL;
    ctx->localroot = NULL;
    ctx->last_error.apr_error = -1;
    ctx->last_error.message = NULL;
    ctx->last_error.file = NULL;
    ctx->last_error.line = -1;
    ctx->ai = NULL;
    ctx->cacheroot = NULL;
    ctx->cachepath = NULL;
    ctx->cachedb = NULL;

    if ((ctx->url = strdup(url)) == NULL) {
        TRRETNULL(SVNC_NEW + 2);
    }

    if ((ctx->localroot = strdup(localroot)) == NULL) {
        TRRETNULL(SVNC_NEW + 3);
    }

    if (svn_url_parse(ctx->url, &ctx->host, &ctx->port, &ctx->path)) {
        TRRETNULL(SVNC_NEW + 4);
    }

    if (snprintf(portstr ,sizeof(portstr), "%d", ctx->port) <= 0) {
        svnc_destroy(ctx);
        free(ctx);
        TRRETNULL(SVNC_NEW + 5);
    }

    memset(&hints, '\0', sizeof(hints));
    hints.ai_family = PF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;
    hints.ai_flags = AI_NUMERICSERV;

    if (getaddrinfo(ctx->host, portstr, &hints, &ctx->ai) != 0) {
        svnc_destroy(ctx);
        free(ctx);
        TRRETNULL(SVNC_NEW + 6);
    }

    if (!flags & SVNC_NNOCACHE) {
        int dbopen_flags = O_RDWR|O_CREAT|O_EXLOCK;

        if ((ctx->cacheroot = path_join(SVNC_CACHE_ROOT,
                                        ctx->localroot)) == NULL) {
            svnc_destroy(ctx);
            free(ctx);
            TRRETNULL(SVNC_NEW + 7);
        }

        if (svncdir_mkdirs(ctx->cacheroot) != 0) {
            svnc_destroy(ctx);
            free(ctx);
            TRRETNULL(SVNC_NEW + 8);
        }

        if ((ctx->cachepath = path_join(ctx->cacheroot, CACHEFILE)) == NULL) {
            svnc_destroy(ctx);
            free(ctx);
            TRRETNULL(SVNC_NEW + 7);
        }

        if (flags & SVNC_NFLUSHCACHE) {
            dbopen_flags |= O_TRUNC;
        }
        if ((ctx->cachedb = dbopen(ctx->cachepath,
                                   dbopen_flags,
                                   0600,
                                   DB_HASH,
                                   NULL)) == NULL) {
            svnc_destroy(ctx);
            free(ctx);
            TRRETNULL(SVNC_NEW + 9);
        }
    }

    return (ctx);
}

int
svnc_connect(svnc_ctx_t *ctx)
{
    struct addrinfo *ai;

    for (ai = ctx->ai; ai != NULL; ai = ai->ai_next) {
        if ((ctx->fd = socket(ai->ai_family, ai->ai_socktype,
                              ai->ai_protocol)) >= 0) {
            if (connect(ctx->fd, ai->ai_addr, ai->ai_addrlen) == 0) {
                break;
            } else {
                close(ctx->fd);
                ctx->fd = -1;
            }
        }
    }

    if (ctx->fd < 0) {
        TRRET(SVNC_CONNECT + 1);
    }

    if (bytestream_init(&ctx->in) != 0) {
        TRRET(SVNC_CONNECT + 2);
    }

    ctx->in.read_more = bytestream_recv_more;

    if (bytestream_init(&ctx->out) != 0) {
        TRRET(SVNC_CONNECT + 3);
    }

    ctx->out.write = bytestream_send;

    if (svnproto_setup(ctx) != 0) {
        TRRET(SVNC_CONNECT + 4);
    }

    return (0);
}

int
svnc_close(svnc_ctx_t *ctx)
{
    bytestream_fini(&ctx->in);
    bytestream_fini(&ctx->out);

    if (ctx->fd != -1) {
        if (close(ctx->fd) != 0) {

            TRRET(SVNC_CLOSE + 1);
        }

        ctx->fd = -1;
    }

    return (0);
}

void
svnc_clear_last_error(svnc_ctx_t *ctx)
{
    if (ctx->last_error.message != NULL) {
        free(ctx->last_error.message);
        ctx->last_error.message = NULL;
    }

    if (ctx->last_error.file != NULL) {
        free(ctx->last_error.file);
        ctx->last_error.file = NULL;
    }
    ctx->last_error.apr_error = -1;
    ctx->last_error.line = -1;
}

int
svnc_destroy(svnc_ctx_t *ctx)
{
    if (svnc_close(ctx) != 0) {
        TRACE("svnc_close issue");
    }

    if (ctx->ai != NULL) {
        freeaddrinfo(ctx->ai);
        ctx->ai = NULL;
    }

    if (ctx->host != NULL) {
        free(ctx->host);
        ctx->host = NULL;
    }

    if (ctx->path != NULL) {
        free(ctx->path);
        ctx->path = NULL;
    }

    if (ctx->url != NULL) {
        free(ctx->url);
        ctx->url = NULL;
    }

    if (ctx->localroot != NULL) {
        free(ctx->localroot);
        ctx->localroot = NULL;
    }

    if (ctx->cachedb != NULL) {
        if (ctx->cachedb->close(ctx->cachedb) != 0) {
            TRACE("cachedb->close issue");
        }
        ctx->cachedb = NULL;
    }
    if (ctx->cachepath != NULL) {
        free(ctx->cachepath);
        ctx->cachepath = NULL;
    }
    if (ctx->cacheroot != NULL) {
        free(ctx->cacheroot);
        ctx->cacheroot = NULL;
    }

    svnc_clear_last_error(ctx);

    return (0);
}


/*
 * For unit tests only.
 */
int
svnc_debug_open(svnc_ctx_t *ctx, const char *path)
{
    if ((ctx->fd = open(path, O_RDONLY)) == -1) {
        TRRET(SVNC_DEBUG_OPEN + 1);
    }
    if (bytestream_init(&ctx->in) != 0) {
        TRRET(SVNC_OPEN + 2);
    }

    ctx->in.read_more = bytestream_read_more;

    if (bytestream_init(&ctx->out) != 0) {
        TRRET(SVNC_OPEN + 3);
    }
    ctx->out.write = bytestream_stderr_write;

    return (0);
}

