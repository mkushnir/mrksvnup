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
#include "mrksvnup/svncdir.h"
#include "mrksvnup/svnproto.h"

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

void
svnc_check_integrity(svnc_ctx_t *ctx, long target_rev)
{
    char *rp = NULL;
    svnproto_bytes_t *cs = NULL;
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
        svnproto_fileent_t fe;
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
                if (svnproto_editor_verify_checksum(fd, cs) != 0) {
                    need_file = 1;
                }
            } else {
                /* checksum for directory? weird */
            }

        } else {
            need_file = 1;
        }

        if (need_file) {
            svnproto_fileent_init(&fe);

            if (svnproto_get_file(ctx,
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
                svnproto_bytes_t **s;
                svnproto_prop_t *prop;
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

                    if (strcmp(BDATA(prop->name), "svn:executable") == 0) {
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

