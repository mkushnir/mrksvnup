#include <err.h>
#include <libgen.h>
#include <stdlib.h>
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

const char *_malloc_options = "J";

static void
check_integrity(svnc_ctx_t *ctx, long target_rev)
{
    char *rp = NULL;
    svnproto_bytes_t *cs = NULL;
    int i;
    size_t counter = 0;

    for (i = svnc_first_checksum(ctx, &rp, &cs);
         i == 0;
         i = svnc_next_checksum(ctx, &rp, &cs)) {

        char *lp;
        int fd = -1;
        svnproto_fileent_t fe;
        char *dirname_terminator;

        if ((lp = path_join(ctx->localroot, rp)) == NULL) {
            errx(1, "path_join");
        }

        /* # mkdir -p `basename $lp` */
        if ((dirname_terminator = strrchr(lp, '/')) != NULL) {
            *dirname_terminator = '\0';
            if (svncdir_mkdirs(lp) != 0) {
                errx(1, "svncdir_mkdirs");
            }
            *dirname_terminator = '/';
        }

        if ((fd = open(lp, O_RDWR|O_CREAT, 0644)) < 0) {
            errx(1, "open");
        }

        if (svnproto_editor_verify_checksum(fd, cs) != 0) {

            svnproto_fileent_init(&fe);

            if (svnproto_get_file(ctx,
                                  rp,
                                  target_rev,
                                  GETFLAG_WANT_CONTENTS|GETFLAG_WANT_PROPS,
                                  &fe) != 0) {
                //LTRACE(1, FRED("Failed to get remote file: %s (ignoring)"),
                //       rp);
                svnc_delete_checksum(ctx, rp);
                close(fd);
                fd = -1;
                if (unlink(lp) != 0) {
                    ;
                }

            } else {
                array_iter_t it;
                svnproto_bytes_t **s;
                svnproto_prop_t *prop;
                ssize_t total_len = 0;

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
                    }
                }
                //LTRACE(1, FGREEN("+ %s -> %s"), rp, lp);
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

static int
update_cb(svnc_ctx_t *ctx,
          UNUSED bytestream_t *stream,
          UNUSED svnproto_state_t *st,
          void *udata)
{
    struct {
        const char *path;
        long source_rev;
        long set_path_flags;
    } *params = udata;

    if (svnproto_set_path(ctx, params->path, params->source_rev,
                          NULL, SVN_DEPTH_INFINITY,
                          params->set_path_flags) != 0) {
        errx(1, "svnproto_set_path");
    }

    LTRACE(0, "set-path OK");

    if (svnproto_finish_report(ctx) != 0) {
        errx(1, "svnproto_finish_report");
    }

    LTRACE(0, "finish-report OK");

    if (svnproto_editor(ctx) != 0) {
        errx(1, "svnproto_editor");
    }

    return 0;
}

static char *
abspath(const char *path)
{
    char *cwd;
    char *res = NULL;

    if (*path == '/') {
        return strdup(path);
    }

    if ((cwd = getcwd(NULL, 0)) == NULL) {
        errx(1, "getcwd");
    }

    res = path_join(cwd, path);

    free(cwd);

    return res;
}

static void
run(const char *url,
    long target_rev,
    const char *localroot,
    unsigned int flags)
{
    char *absroot;
    char *dotfile = NULL;
    long source_rev = -1;
    svnc_ctx_t *ctx;
    struct stat sb;
    int fd;
    int kind = -1;
    char buf[64];
    struct {
        const char *path;
        long source_rev;
        long set_path_flags;
    } update_params = {"", -1, 0};

    /* set up local root */
    if ((absroot = abspath(localroot)) == NULL) {
        errx(1, "abspath");
    }

    if (lstat(absroot, &sb) == 0) {
        if (!S_ISDIR(sb.st_mode)) {
            errx(1, "Not a directory: %s", absroot);
        }
    }

    /* source revision is in a dotfile (previously saved target revision) */
    if ((dotfile = path_join(absroot, DOTFILE)) == NULL) {
        errx(1, "path_join() issue");
    }

    if (flags & SVNC_NFLUSHCACHE) {
        /* forget about source revision */
        if (unlink(dotfile) != 0) {
            ;
        }
    } else {
        if (lstat(dotfile, &sb) == 0 && S_ISREG(sb.st_mode)) {
            if ((fd = open(dotfile, O_RDONLY)) >= 0) {
                char buf[64];
                ssize_t nread;
                if ((nread = read(fd, buf, 64)) > 0) {
                    buf[nread] = '\0';
                    source_rev = strtol(buf, NULL, 10);
                }
                close(fd);
                LTRACE(0, "Found saved source revision: %ld", source_rev);
            }
        }
    }

    if ((ctx = svnc_new(url, absroot, 0)) == NULL) {
        errx(1, "svnc_new");
    }

    if (svnc_connect(ctx) != 0) {
        errx(1, "svnc_connect");
    }

    if (source_rev <= 0) {
        /* for check out */
        if (target_rev <= 0) {
            if (svnproto_get_latest_rev(ctx, &target_rev) != 0) {
                errx(1, "svnproto_get_latest_rev 1");
            }
        }
        source_rev = target_rev;

        update_params.set_path_flags |= SETPFLAG_START_EMPTY;
    } else {
        /* for update */
        if (target_rev <= 0) {
            if (svnproto_get_latest_rev(ctx, &target_rev) != 0) {
                errx(1, "svnproto_get_latest_rev 2");
            }
        }
    }

    if (svnproto_check_path(ctx, update_params.path,
                            target_rev, &kind) != 0) {
        errx(1, "svnproto_check_path");
    }

    //TRACE("check_path kind=%s", svnproto_kind2str(kind));
    
    if (kind != SVNP_KIND_DIR) {
        errx(1, "Not a path: %s", update_params.path);
    }

    /*
     * 1. Update from remote repo.
     */
    update_params.source_rev = source_rev;

    if (svnproto_update(ctx, target_rev, update_params.path, 0,
                        UPFLAG_RECURSE, update_cb, &update_params) != 0) {
        errx(1, "svnproto_update");
    }

    /*
     * 2. Check local integrity and possibly check out corrupt files.
     */

    LTRACE(0, "Checking integrity ...");

    check_integrity(ctx, target_rev);

    /*
     * 3. Save target revsion as a current revision.
     */

    LTRACE(0, "Update path: source %ld target %ld", source_rev, target_rev);
    if ((fd = open(dotfile, O_RDWR|O_CREAT|O_TRUNC, 0644)) < 0) {
        err(1, "open");
    }

    int sz = snprintf(buf, sizeof(buf), "%ld", target_rev);

    if (write(fd, buf, sz) < 0) {
        err(1, "write");
    }

    close(fd);
    LTRACE(0, "Saved revision: %ld", target_rev);

    LTRACE(0, "OK");

    svnc_close(ctx);
    svnc_destroy(ctx);
    free(ctx);
    free(absroot);
}

static void
usage(const char *progname)
{
    printf("Usage: %s -u URL [ -r REV ] -l PATH\n\n", basename(progname));
}

int
main(int argc, char *argv[])
{
    int res = 0;
    char ch;
    char *url = NULL;
    long target_rev = -1;
    char *localroot = NULL;
    unsigned int flags = 0;

    while ((ch = getopt(argc, argv, "fhl:r:u:vV")) != -1) {
        switch (ch) {
        case 'f':
            flags |= SVNC_NFLUSHCACHE;
            break;

        case 'h':
            usage(argv[0]);
            exit(0);
            break;

        case 'l':
            localroot = optarg;
            break;

        case 'r':
            target_rev = strtol(optarg, NULL, 10);
            break;

        case 'u':
            url = optarg;
            break;

        case 'V':
            usage(argv[0]);
            break;

        default:
            usage(argv[0]);
            exit(1);

        }
    }

    if (url == NULL) {
        usage(argv[0]);
        TRACE("Missing URL");
        exit(1);
    }
    if (localroot == NULL) {
        usage(argv[0]);
        TRACE("Missing local path");
        exit(1);
    }

    //TRACE("command-line arguments: %s %ld %s", url, target_rev, localroot);

    run(url, target_rev, localroot, flags);

    return res;
}
