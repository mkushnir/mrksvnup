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

    if (ctx->debug_level > 0) {
        LTRACE(0, "set-path OK");
    }

    if (svnproto_finish_report(ctx) != 0) {
        errx(1, "svnproto_finish_report");
    }

    if (ctx->debug_level > 0) {
        LTRACE(0, "finish-report OK");
    }

    if (svnproto_editor(ctx) != 0) {
        svnc_print_last_error(ctx);
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
    unsigned int flags,
    int debug_level)
{
    char *absroot;
    char *revfile = NULL;
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

    /* source revision is in a revfile (previously saved target revision) */
    if ((revfile = path_join(absroot, REVFILE)) == NULL) {
        errx(1, "path_join() issue");
    }

    if (flags & SVNC_NFLUSHCACHE) {
        /* forget about source revision */
        if (unlink(revfile) != 0) {
            ;
        }
    } else {
        if (lstat(revfile, &sb) == 0 && S_ISREG(sb.st_mode)) {
            if ((fd = open(revfile, O_RDONLY)) >= 0) {
                char buf[64];
                ssize_t nread;
                if ((nread = read(fd, buf, 64)) > 0) {
                    buf[nread] = '\0';
                    source_rev = strtol(buf, NULL, 10);
                }
                close(fd);
                if (debug_level > 0) {
                    LTRACE(0, "Found saved source revision: %ld", source_rev);
                }
            }
        } else {
            /* forget about old db, and don't do integrity check this time */
            flags |= SVNC_NFLUSHCACHE;
        }
    }

    if ((ctx = svnc_new(url, absroot, flags, debug_level)) == NULL) {
        errx(1, "svnc_new");
    }

    if (svnc_connect(ctx) != 0) {
        errx(1, "svnc_connect");
    }

    if (target_rev <= 0) {
        if (svnproto_get_latest_rev(ctx, &target_rev) != 0) {
            errx(1, "svnproto_get_latest_rev");
        }
    }

    if (source_rev <= 0) {
        /* for check out */
        source_rev = target_rev;

        update_params.set_path_flags |= SETPFLAG_START_EMPTY;

    } else {
        /* for update */
    }

    if (svnproto_check_path(ctx, update_params.path,
                            target_rev, &kind) != 0) {
        errx(1, "svnproto_check_path");
    }

    //TRACE("check_path kind=%s", svnproto_kind2str(kind));
    
    if (kind != SVNP_KIND_DIR) {
        errx(1, "Remote path is not a directory: %s", update_params.path);
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
    if (debug_level > 0) {
        LTRACE(0, "Checking integrity (lengthy) ...");
    }

    svnc_check_integrity(ctx, target_rev);

    /*
     * 3. Save target revsion as a current revision.
     */

    if (debug_level > 0) {
        LTRACE(0, "Update path: source %ld target %ld", source_rev, target_rev);
    }
    if ((fd = open(revfile, O_RDWR|O_CREAT|O_TRUNC, 0644)) < 0) {
        err(1, "open");
    }

    int sz = snprintf(buf, sizeof(buf), "%ld", target_rev);

    if (write(fd, buf, sz) < 0) {
        err(1, "write");
    }

    close(fd);

    if (debug_level > 0) {
        LTRACE(0, "Saved revision: %ld", target_rev);
        LTRACE(0, "OK");
    }


    svnc_close(ctx);
    svnc_destroy(ctx);
    free(ctx);
    free(absroot);
}

static void
usage(const char *progname)
{
    printf("Usage: %s -u URL -l PATH "
           "[ -r REV ] [ -f ] [ -L ] [ -v LEVEL ]\n\n", basename(progname));
    printf("Usage: %s -h\n\n", basename(progname));
    printf("Usage: %s -V\n\n", basename(progname));
}

static void
version(UNUSED const char *progname)
{
    printf("%s\n", RA_CLIENT);
}

int
main(int argc, char *argv[])
{
    int res = 0;
    char ch;
    char *url = NULL;
    long target_rev = -1;
    char *localroot = NULL;
    unsigned int flags = SVNC_NNOCHECK;
    int debug_level = 1;

    while ((ch = getopt(argc, argv, "fhl:r:Ru:v:V")) != -1) {
        switch (ch) {
        case 'f':
            /* flush cache */
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

        case 'R':
            /* repair mode */
            flags &= ~SVNC_NNOCHECK;
            break;

        case 'u':
            url = optarg;
            break;

        case 'v':
            /* not yet implemented */
            debug_level = strtol(optarg, NULL, 10);
            break;

        case 'V':
            version(argv[0]);
            break;

        default:
            usage(argv[0]);
            exit(1);

        }
    }

    if (url == NULL) {
        usage(argv[0]);
        errx(1, "Missing URL");
    }
    if (localroot == NULL) {
        usage(argv[0]);
        errx(1, "Missing local path");
    }

    //TRACE("command-line arguments: %s %ld %s", url, target_rev, localroot);

    run(url, target_rev, localroot, flags, debug_level);

    return res;
}
