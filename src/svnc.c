#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>

#include "diag.h"
#include "mrkcommon/util.h"
//#define TRRET_DEBUG
//#define TRRET_DEBUG_VERBOSE
#include "mrkcommon/dumpm.h"
#include "mrkcommon/traversedir.h"

#include "mrksvnup/svncdir.h"
#include "mrksvnup/svnproto.h"
#include "mrksvnup/http.h"
#include "mrksvnup/dav.h"
#include "mrksvnup/httpproto.h"

#include "svnc_private.h"

int
svnc_save_checksum(svnc_ctx_t *ctx,
                   const char *path,
                   bytes_t *checksum)
{
    int res = 0;
    DBT k, v;
    k.size = strlen(path);
    k.data = (void *)path;
    v.size = checksum->sz;
    v.data = checksum->data;

#ifdef TRRET_DEBUG_VERBOSE
    TRACE("saving checksum path=%p data=%p", path, v.data);
    TRACE("saving checksum path=%s", path);
    D32(v.data, v.size);
    TRACE();
#endif
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
                     bytes_t **checksum)
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

    if ((*checksum = malloc(sizeof(bytes_t) + v.size)) == NULL) {
        FAIL("malloc");
    }
    (*checksum)->sz = v.size;
    memcpy((*checksum)->data, v.data, v.size);

    TRRET(res);
}

int
svnc_next_checksum(svnc_ctx_t *ctx,
                   char **path,
                   bytes_t **checksum)
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

    if ((*checksum = malloc(sizeof(bytes_t) + v.size)) == NULL) {
        FAIL("malloc");
    }
    (*checksum)->sz = v.size;
    memcpy((*checksum)->data, v.data, v.size);

    TRRET(res);
}

svnc_ctx_t *
svnc_new(const char *url,
         const char *localroot,
         unsigned flags,
         int debug_level)
{
    svnc_ctx_t *ctx;
    struct addrinfo hints;
    char portstr[32];

    if ((ctx = malloc(sizeof(svnc_ctx_t))) == NULL) {
        FAIL("malloc");
    }

    /* XXX manually initialize it here */
    ctx->fd = -1;
    ctx->scheme = 0;
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
    ctx->debug_level = debug_level;
    ctx->get_latest_rev = NULL;
    ctx->check_path = NULL;
    ctx->setup = NULL;
    ctx->set_path = NULL;
    ctx->finish_report = NULL;
    ctx->get_file = NULL;
    ctx->update = NULL;
    ctx->editor = NULL;
    ctx->udata = NULL;

    if (bytestream_init(&ctx->in) != 0) {
        FAIL("bytestream_init");
    }

    if (bytestream_init(&ctx->out) != 0) {
        FAIL("bytestream_init");
    }

    if ((ctx->url = strdup(url)) == NULL) {
        FAIL("strdup");
    }

    if ((ctx->localroot = strdup(localroot)) == NULL) {
        FAIL("strdup");
    }

    if (svn_url_parse(ctx->url, &ctx->scheme, &ctx->host, &ctx->port, &ctx->path)) {
        LTRACE(1, "URL could not be accepted: %s", ctx->url);
        TRRETNULL(SVNC_NEW + 1);
    }

    if (snprintf(portstr, sizeof(portstr), "%d", ctx->port) <= 0) {
        svnc_destroy(ctx);
        free(ctx);
        TRRETNULL(SVNC_NEW + 2);
    }

    memset(&hints, '\0', sizeof(hints));
    hints.ai_family = PF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;
    hints.ai_flags = AI_NUMERICSERV;

    if (getaddrinfo(ctx->host, portstr, &hints, &ctx->ai) != 0) {
        LTRACE(1, "Invalid parameters to getaddrinfo: host '%s' port '%s'", ctx->host, portstr);
        svnc_destroy(ctx);
        free(ctx);
        TRRETNULL(SVNC_NEW + 3);
    }

    if (!(flags & SVNC_NNOCACHE)) {
        int dbopen_flags = O_RDWR|O_CREAT|O_EXLOCK;

        if ((ctx->cacheroot = strdup(ctx->localroot)) == NULL) {
            FAIL("strdup");
        }

        if (svncdir_mkdirs(ctx->cacheroot) != 0) {
            svnc_destroy(ctx);
            free(ctx);
            TRRETNULL(SVNC_NEW + 4);
        }

        if ((ctx->cachepath = path_join(ctx->cacheroot, CACHEFILE)) == NULL) {
            svnc_destroy(ctx);
            free(ctx);
            TRRETNULL(SVNC_NEW + 5);
        }

        if (flags & SVNC_NFLUSHCACHE) {
            dbopen_flags |= O_TRUNC;
            ctx->flags |= SVNC_NO_CHECK_INTEGRITY;
        }

        if (flags & SVNC_NNOCHECK) {
            ctx->flags |= SVNC_NO_CHECK_INTEGRITY;
        }

        if ((ctx->cachedb = dbopen(ctx->cachepath,
                                   dbopen_flags,
                                   0600,
                                   DB_BTREE,
                                   NULL)) == NULL) {
            if (debug_level > 0) {
                LTRACE(0, FRED("Failed to open cache db: %s"), ctx->cachepath);
            }
            svnc_destroy(ctx);
            free(ctx);
            TRRETNULL(SVNC_NEW + 6);
        }
    }

    if (ctx->scheme == SVNC_SCHEME_SVN) {
        ctx->get_latest_rev = svnproto_get_latest_rev;
        ctx->check_path = svnproto_check_path;
        ctx->setup = svnproto_setup;
        ctx->set_path = svnproto_set_path;
        ctx->finish_report = svnproto_finish_report;
        ctx->get_file = svnproto_get_file;
        ctx->update = svnproto_update;
        ctx->editor = svnproto_editor;
        ctx->executable_prop_name = "svn:executable";
        ctx->special_prop_name = "svn:special";

    } else if (ctx->scheme == SVNC_SCHEME_HTTP) {
        ctx->get_latest_rev = httpproto_get_latest_rev;
        ctx->check_path = httpproto_check_path;
        ctx->setup = httpproto_setup;
        ctx->set_path = httpproto_set_path;
        ctx->finish_report = httpproto_finish_report;
        ctx->get_file = httpproto_get_file;
        ctx->update = httpproto_update;
        ctx->editor = httpproto_editor;
        ctx->executable_prop_name = "http://subversion.tigris.org/xmlns/svn/executable";
        ctx->special_prop_name = "http://subversion.tigris.org/xmlns/svn/special";
    }

    return (ctx);
}

int
svnc_socket_reconnect(svnc_ctx_t *ctx)
{
    struct addrinfo *ai;

    if (ctx->fd != -1) {
        if (close(ctx->fd) != 0) {
            /* ignore */
            ;
        }
    }

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
        LTRACE(1, "Could not connect to %s:%d", ctx->host, ctx->port);
        TRRET(SVNC_SOCKET_RECONNECT + 1);
    }

    TRRET(0);
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

    ctx->in.read_more = bytestream_recv_more;

    if (ctx->scheme == SVNC_SCHEME_SVN) {
        ctx->in.udata = svnproto_state_new();
    } else if (ctx->scheme == SVNC_SCHEME_HTTP) {
        ctx->in.udata = http_ctx_new();
    } else {
        ctx->in.udata = NULL;
    }

    ctx->out.write = bytestream_send;

    assert(ctx->setup != NULL);
    if (ctx->setup(ctx) != 0) {
        TRRET(SVNC_CONNECT + 1);
    }

    return (0);
}

int
svnc_close(svnc_ctx_t *ctx)
{
    if (ctx->in.udata != NULL) {
        if (ctx->scheme == SVNC_SCHEME_SVN) {
            svnproto_state_destroy(ctx->in.udata);
        } else if (ctx->scheme == SVNC_SCHEME_HTTP) {
            http_ctx_destroy(ctx->in.udata);
        } else {
            ;
        }
        ctx->in.udata = NULL;
    }

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

void
svnc_print_last_error(svnc_ctx_t *ctx)
{
    if (BDATA(ctx->last_error.message) != NULL) {
        LTRACE(0, FRED("E %s (code %ld)"),
                       BDATA(ctx->last_error.message),
                       ctx->last_error.apr_error);
    }
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

    ctx->get_latest_rev = NULL;
    ctx->check_path = NULL;
    ctx->setup = NULL;
    ctx->set_path = NULL;
    ctx->finish_report = NULL;
    ctx->get_file = NULL;
    ctx->update = NULL;
    ctx->editor = NULL;

    if (ctx->udata != NULL) {
        if (ctx->scheme == SVNC_SCHEME_SVN) {
        } else if (ctx->scheme == SVNC_SCHEME_HTTP) {
            dav_ctx_destroy((dav_ctx_t *)(ctx->udata));
        } else {
            ;
        }
        ctx->udata = NULL;
    }

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

    if (ctx->scheme == SVNC_SCHEME_SVN) {
        ctx->in.udata = svnproto_state_new();
    } else if (ctx->scheme == SVNC_SCHEME_HTTP) {
        ctx->in.udata = http_ctx_new();
    } else {
        ctx->in.udata = NULL;
    }

    ctx->in.read_more = bytestream_read_more;

    if (bytestream_init(&ctx->out) != 0) {
        TRRET(SVNC_OPEN + 3);
    }
    ctx->out.write = bytestream_stderr_write;

    return (0);
}

