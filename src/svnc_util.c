#include <assert.h>
#include <err.h>
#include <libgen.h>
#include <stdlib.h>
#include <limits.h>
#include <inttypes.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>

#include "diag.h"
#include "mrkcommon/array.h"
#include "mrkcommon/dumpm.h"
#include "mrkcommon/util.h"
#include "mrkcommon/traversedir.h"

#include "mrksvnup/svnc.h"
#include "mrksvnup/svnedit.h"
#include "mrksvnup/svncdir.h"
#include "mrksvnup/svnproto.h"
#include "mrksvnup/http.h"

static const char *kinds[] = {
    "none",
    "file",
    "dir",
    "unknown",
};

int
svnc_kind2int(const char *kind)
{
    unsigned i;

    if (kind != NULL) {
        /* mix of dav and svn ra */
        if ((strcmp(kind, "collection")) == 0) {
            return SVNC_KIND_DIR;
        }
        for (i = 0; i < countof(kinds); ++i) {
            if (strcmp(kind, kinds[i]) == 0) {
                return i;
            }
        }
    }

    return SVNC_KIND_UNKNOWN;
}

const char *
svnc_kind2str(int kind)
{
    if (kind < 0 || kind > SVNC_KIND_UNKNOWN) {
        kind = SVNC_KIND_UNKNOWN;
    }
    return kinds[kind];
}

int
svn_url_parse(const char *url, int *scheme, char **host, int *port, char **path)
{
    char *p0, *p1, *p2;
    size_t sz;

    assert(*host == NULL && *path == NULL);

    sz = strlen(url);

    if ((p0 = strnstr(url, "svn://", sz)) == url) {
        *scheme = SVNC_SCHEME_SVN;
        p0 += 6;
        sz -= 6;

    } else if ((p0 = strnstr(url, "http://", sz)) == url) {
        *scheme = SVNC_SCHEME_HTTP;
        p0 += 7;
        sz -= 7;

    } else if ((p0 = strnstr(url, "https://", sz)) == url) {
        *scheme = SVNC_SCHEME_HTTPS;
        p0 += 8;
        sz -= 8;

    } else {
        TRRET(SVN_URL_PARSE + 1);
    }

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
        if (*scheme == SVNC_SCHEME_SVN) {
            *port = SVN_DEFAULT_PORT;
        } else if (*scheme == SVNC_SCHEME_HTTP) {
            *port = HTTP_DEFAULT_PORT;
        } else if (*scheme == SVNC_SCHEME_HTTPS) {
            *port = HTTPS_DEFAULT_PORT;
        }
    }

    if ((*path = malloc(sz + 1)) == NULL) {
        TRRET(SVN_URL_PARSE + 5);
    }

    memcpy(*path, p1, sz);
    (*path)[sz] = '\0';

    return (0);
}

void
svnc_check_integrity(svnc_ctx_t *ctx, long target_rev)
{
    char *rp = NULL;
    bytes_t *cs = NULL;
    int i;
    size_t counter = 0;

    if (ctx->flags & SVNC_NO_CHECK_INTEGRITY) {
        if (ctx->debug_level > 0) {
            LTRACE(1, FYELLOW("Will not check files against database"));
        }
        return;
    }

    for (i = svnc_first_checksum(ctx, &rp, &cs);
         i == 0;
         i = svnc_next_checksum(ctx, &rp, &cs)) {

        char *lp;
        int fd = -1;
        svnc_fileent_t fe;
        struct stat sb;
        int need_file = 0;

        if ((lp = path_join(ctx->localroot, rp)) == NULL) {
            errx(1, "path_join");
        }

        /*
         * XXX fix it to handle symlinks here (as it is in the editor)!
         */

        /* exists? */

        if (lstat(lp, &sb) == 0) {
            if (S_ISREG(sb.st_mode)) {
                if ((fd = open(lp, O_RDWR)) < 0) {
                    errx(1, "open");
                }
                if (svnedit_verify_checksum(fd, cs) != 0) {
                    need_file = 1;
                }
            } else {
                /* checksum for directory? weird */
            }

        } else {
            need_file = 1;
        }

        if (need_file) {
            svnc_fileent_init(&fe);

            assert(ctx->get_file != NULL);
            if (ctx->get_file(ctx,
                              rp,
                              target_rev,
                              GETFLAG_WANT_CONTENTS|GETFLAG_WANT_PROPS,
                              &fe) != 0) {

                if (ctx->debug_level > 2) {
                    LTRACE(1, FYELLOW("Failed to get remote file: %s "
                           "(forgetting)"), rp);
                }

                svnc_delete_checksum(ctx, rp);
                close(fd);
                fd = -1;
                if (unlink(lp) != 0) {
                    /* ignore */
                    ;
                }
            } else {
                array_iter_t it;
                bytes_t **s;
                svnc_prop_t *prop;
                ssize_t total_len = 0;

                if (fd == -1) {
                    char *dirname_terminator;

                    /* # mkdir -p `basename $lp` */
                    if ((dirname_terminator = strrchr(lp, '/')) != NULL) {
                        *dirname_terminator = '\0';
                        if (svncdir_mkdirs(lp) != 0) {
                            errx(1, "svncdir_mkdirs");
                        }
                        *dirname_terminator = '/';
                    }

                    if ((fd = open(lp, O_RDWR|O_CREAT|O_TRUNC, 0644)) < 0) {
                        errx(1, "open");
                    }
                }

                /* contents */
                if (lseek(fd, 0, SEEK_SET) != 0) {
                    errx(1, "lssek");
                }

                for (s = array_first(&fe.contents, &it);
                     s != NULL;
                     s = array_next(&fe.contents, &it)) {

                    if (write(fd, (*s)->data, (*s)->sz) < 0) {
                        FAIL("write");
                    }
                    total_len += (*s)->sz;
                }

                if (ftruncate(fd, total_len) != 0) {
                    FAIL("ftruncate");
                }

                /* props */
                for (prop = array_first(&fe.props, &it);
                     prop != NULL;
                     prop = array_next(&fe.props, &it)) {

                    if (strcmp(BDATA(prop->name), ctx->executable_prop_name) == 0) {
                        if (fchmod(fd, 0755) != 0) {
                            errx(1, "fchmod");
                        }
                        break;
                    }
                }
                if (ctx->debug_level > 1) {
                    LTRACE(1, FGREEN("+ %s -> %s"), rp, lp);
                }
            }
        }

        if (fd != -1) {
            close(fd);
        }
        if (rp != NULL) {
            free(rp);
            rp = NULL;
        }
        if (lp != NULL) {
            free(lp);
            lp = NULL;
        }
        if (cs != NULL) {
            free(lp);
            lp = NULL;
        }
        if ((counter % 1000) == 0) {
            fprintf(stderr, FGREEN("."));
            fflush(stderr);
        }
        ++counter;
    }
    fprintf(stderr, ("\n"));
    fflush(stderr);
}

/* data helpers */

static int
dump_long(long *v, UNUSED void *udata)
{
    TRACE("v=%ld", *v);
    return 0;
}

void
init_long_array(array_t *ar)
{
    if (array_init(ar, sizeof(long), 0,
                   NULL, NULL) != 0) {
        FAIL("array_init");
    }
}

void
dump_long_array(array_t *ar) {
    array_traverse(ar, (array_traverser_t)dump_long, NULL);
}

static int
init_string(const char **v)
{
    *v = NULL;
    return 0;
}

static int
fini_string(char **v)
{
    if (*v != NULL) {
        free(*v);
        *v = NULL;
    }
    return 0;
}

static int
dump_string(const char **v, UNUSED void *udata)
{
    TRACE("v=%s", *v);
    return 0;
}

void
init_string_array(array_t *ar)
{
    if (array_init(ar, sizeof(char *), 0,
                   (array_initializer_t)init_string,
                   (array_finalizer_t)fini_string) != 0) {
        FAIL("array_init");
    }
}

void
dump_string_array(array_t *ar) {
    array_traverse(ar, (array_traverser_t)dump_string, NULL);
}

static int
init_bytes(bytes_t **v)
{
    *v = NULL;
    return 0;
}

static int
fini_bytes(bytes_t **v)
{
    if (*v != NULL) {
        free(*v);
        *v = NULL;
    }
    return 0;
}

static int
dump_bytes(bytes_t **v, UNUSED void *udata)
{
    if (*v != NULL) {
        D16((*v)->data, MIN(64, (*v)->sz));
    } else {
        TRACE("bytes null");
    }
    return 0;
}

void
init_bytes_array(array_t *ar)
{
    if (array_init(ar, sizeof(bytes_t *), 0,
                   (array_initializer_t)init_bytes,
                   (array_finalizer_t)fini_bytes) != 0) {
        FAIL("array_init");
    }
}

void
fini_bytes_array(array_t *ar)
{
    if (array_fini(ar) != 0) {
        FAIL("array_fini");
    }
}

void
dump_bytes_array(array_t *ar)
{
    array_traverse(ar, (array_traverser_t)dump_bytes, NULL);
}

bytes_t *
bytes_from_str(const char *s)
{
    bytes_t *b = NULL;
    size_t sz;

    if (s == NULL) {
        return NULL;
    }

    sz = strlen(s);

    if ((b = malloc(sizeof(bytes_t) + sz + 1)) == NULL) {
        FAIL("malloc");
    }
    b->sz = sz;
    memcpy(b->data, s, sz);
    (b->data)[sz] = '\0';
    return b;
}

bytes_t *
bytes_from_strn(const char *s, size_t sz)
{
    bytes_t *b = NULL;

    if (s == NULL) {
        return NULL;
    }

    if ((b = malloc(sizeof(bytes_t) + sz + 1)) == NULL) {
        FAIL("malloc");
    }
    b->sz = sz;
    memcpy(b->data, s, sz);
    (b->data)[sz] = '\0';
    return b;
}

bytes_t *
bytes_from_mem(const char *s, size_t sz)
{
    bytes_t *b = NULL;

    if (s == NULL) {
        return NULL;
    }

    if ((b = malloc(sizeof(bytes_t) + sz)) == NULL) {
        FAIL("malloc");
    }
    b->sz = sz;
    memcpy(b->data, s, sz);
    return b;
}

static int
prop_init(svnc_prop_t *p)
{
    p->name = NULL;
    p->value = NULL;
    return 0;
}

static int
prop_fini(svnc_prop_t *p)
{
    if (p->name != NULL) {
        free(p->name);
        p->name = NULL;
    }
    if (p->value != NULL) {
        free(p->value);
        p->value = NULL;
    }
    return 0;
}

static int
prop_dump(svnc_prop_t *p, UNUSED void *udata)
{
    TRACE("prop: %s=%s", BDATA(p->name), BDATA(p->value));
    return 0;
}


int
svnc_fileent_init(svnc_fileent_t *e)
{
    e->checksum = NULL;
    e->rev = -1;
    if (array_init(&e->props, sizeof(svnc_prop_t), 0,
                   (array_initializer_t)prop_init,
                   (array_finalizer_t)prop_fini) != 0) {
        FAIL("array_init");
    }
    init_string_array(&e->contents);
    return 0;
}

int
svnc_fileent_fini(svnc_fileent_t *e)
{
    if (e->checksum != NULL) {
        free(e->checksum);
        e->checksum = NULL;
    }
    if (array_fini(&e->props) != 0) {
        FAIL("array_fini");
    }
    array_fini(&e->contents);
    return 0;
}

void
svnc_fileent_dump(svnc_fileent_t *e)
{
    TRACE("checksum=%s rev=%ld", BDATA(e->checksum), e->rev);
    array_traverse(&e->props,
                   (array_traverser_t)prop_dump, NULL);
    dump_bytes_array(&e->contents);
}

