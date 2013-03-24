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
#include "mrkcommon/dumpm.h"

#include "mrksvnup/svnproto.h"
#include "mrksvnup/svnc.h"

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

svnc_ctx_t *
svnc_new(const char *url,
         const char *localroot)
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
        TRRETNULL(SVNC_NEW + 5);
    }

    memset(&hints, '\0', sizeof(hints));
    hints.ai_family = PF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;
    hints.ai_flags = AI_NUMERICSERV;

    if (getaddrinfo(ctx->host, portstr, &hints, &ctx->ai) != 0) {
        svnc_destroy(ctx);
        TRRETNULL(SVNC_NEW + 6);
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
        TRRET(SVNC_DESTROY + 1);
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

