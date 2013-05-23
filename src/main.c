#include <assert.h>
#include <err.h>
#include <libgen.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <openssl/evp.h>

//#define TRRET_DEBUG
#include "diag.h"
#include "mrkcommon/array.h"
#include "mrkcommon/dumpm.h"
#include "mrkcommon/util.h"
#include "mrkcommon/traversedir.h"

#include "mrksvnup/svnc.h"
#include "mrksvnup/svncdir.h"

#ifndef NDEBUG
const char *_malloc_options = "J";
#endif

static svnc_ctx_t *ctx = NULL;
static char *absroot = NULL;

static void
atexit_cb(void)
{
    char *lockfile;

    if ((lockfile = path_join(absroot, LOCKFILE)) == NULL) {
        FAIL("path_join");
    }
    if (unlink(lockfile) != 0) {
        perror("[ignoring] unlink");
    }
    if (ctx != NULL) {
        svnc_close(ctx);
        svnc_destroy(ctx);
        free(ctx);
        ctx = NULL;
    }
    free(absroot);
    free(lockfile);
}

static void
sigterm_handler(UNUSED int sig)
{
    atexit_cb();
    LTRACE(0, "Interrupted.");
    _exit(1);
}


static int
update_cb(svnc_ctx_t *ctx,
          UNUSED bytestream_t *stream,
          UNUSED void *st,
          void *udata)
{
    int res;
    struct {
        const char *path;
        long source_rev;
        long set_path_flags;
    } *params = udata;

    assert(ctx->set_path != NULL);
    if ((res = ctx->set_path(ctx, params->path, params->source_rev,
                      NULL, SVN_DEPTH_INFINITY,
                      params->set_path_flags)) != 0) {
        errx(1, "set_path [%s]", diag_str(res));
    }

    if (ctx->debug_level > 0) {
        LTRACE(0, "set-path OK");
    }

    assert(ctx->finish_report != NULL);
    if ((res = ctx->finish_report(ctx)) != 0) {
        errx(1, "finish_report [%s]", diag_str(res));
    }

    if (ctx->debug_level > 0) {
        LTRACE(0, "finish-report OK");
    }

    assert(ctx->editor != NULL);
    if ((res = ctx->editor(ctx)) != 0) {
        svnc_print_last_error(ctx);
        errx(1, "editor [%s]", diag_str(res));
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
run(const char *cmdline_url,
    long target_rev,
    const char *localroot,
    unsigned int flags,
    int debug_level)
{
    int res;
    char *revfile = NULL;
    char *repofile = NULL;
    long source_rev = -1;
    struct stat sb;
    int fd;
    int kind = -1;
    char buf[64];
    char *url = NULL;
    struct {
        const char *path;
        long source_rev;
        long set_path_flags;
    } update_params = {"", -1, 0};

    if ((repofile = path_join(localroot, REPOFILE)) == NULL) {
        errx(1, "path_join() REPOFILE");
    }
    if (cmdline_url == NULL) {
        if (lstat(repofile, &sb) == 0 && S_ISREG(sb.st_mode)) {
            if ((fd = open(repofile, O_RDONLY)) >= 0) {
                if ((url = malloc(sb.st_size + 1)) == NULL) {
                    FAIL("malloc");
                }
                ssize_t nread;
                if ((nread = read(fd, url, sb.st_size)) > 0) {
                    url[sb.st_size] = '\0';
                }
                close(fd);
                if (debug_level > 0) {
                    LTRACE(0, "Found saved URL: %s", url);
                }
            } else {
                perror("open");
                errx(1, "Cannot find URL.");
            }
        } else {
            errx(1, "Cannot find URL.");
        }
    } else {
        url = strdup(cmdline_url);
    }

    /* source revision is in a revfile (previously saved target revision) */
    if ((revfile = path_join(localroot, REVFILE)) == NULL) {
        errx(1, "path_join() REVFILE");
    }

    if (flags & SVNC_FLUSHCACHE) {
        /* forget about source revision */
        if (unlink(revfile) != 0) {
            ;
        }
    } else {
        if (lstat(revfile, &sb) == 0 && S_ISREG(sb.st_mode)) {
            if ((fd = open(revfile, O_RDONLY)) >= 0) {
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
            flags |= SVNC_FLUSHCACHE;
        }
    }

    if ((ctx = svnc_new(url, localroot, flags, debug_level)) == NULL) {
        errx(1, "svnc_new");
    }

    if (svnc_connect(ctx) != 0) {
        errx(1, "svnc_connect: could not connect to %s:%d",
             ctx->host, ctx->port);
    }

    if (target_rev <= 0) {
        assert(ctx->get_latest_rev != NULL);
        if ((res = ctx->get_latest_rev(ctx, &target_rev)) != 0) {
            errx(1, "get_latest_rev [%s]", diag_str(res));
        }
    }

    if (source_rev <= 0) {
        /* for check out */
        source_rev = target_rev;

        update_params.set_path_flags |= SETPFLAG_START_EMPTY;

    } else {
        /* for update */
    }

    assert(ctx->check_path != NULL);
    if (ctx->check_path(ctx, update_params.path,
                            target_rev, &kind) != 0) {
        svnc_print_last_error(ctx);
        errx(1, "Remote path is not a directory: %s", ctx->path);
    }

    //TRACE("check_path kind=%s", svnc_kind2str(kind));

    if (kind != SVNC_KIND_DIR) {
        errx(1, "Remote path is not a directory: %s", ctx->path);
    }

    /*
     * 1. Update from remote repo.
     */
    update_params.source_rev = source_rev;

    assert(ctx->update != NULL);
    if ((res = ctx->update(ctx, target_rev, update_params.path, 0,
                        UPFLAG_RECURSE, update_cb, &update_params)) != 0) {
        errx(1, "update [%s]", diag_str(res));
    }

    /*
     * 2. Check local integrity and possibly check out corrupt files.
     */
    if (debug_level > 1) {
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
    }

    /*
     * 4. Save URL.
     */

    if ((fd = open(repofile, O_RDWR|O_CREAT|O_TRUNC, 0644)) < 0) {
        err(1, "open");
    }

    if (write(fd, url, strlen(url)) < 0) {
        err(1, "write");
    }

    close(fd);

    if (debug_level > 0) {
        LTRACE(0, "Saved URL: %s", url);
        LTRACE(0, "OK");
    }

    svnc_close(ctx);
    svnc_destroy(ctx);
    free(ctx);
    ctx = NULL;
    free(revfile);
    free(repofile);
    free(url);
}

static void
usage(const char *progname)
{
    printf("Usage:\n");
    printf("   %s [-r REV] [-v LEVEL] [-R] [-C] [-f] [-t] "
           "[URL] [DIR]\n", progname);
    printf("   %s [-h]\n", progname);
    printf("   %s [-V]\n", progname);
}

static void
version(UNUSED const char *progname)
{
    printf("%s\n", RA_CLIENT);
}

int
main(int argc, char *argv[])
{
    const char *progname;
    int res = 0;
    char ch;
    char *url = NULL;
    long target_rev = -1;
    char *localroot = NULL;
    char *lockfile = NULL;
    unsigned int flags = 0;
    int debug_level = 1;
    struct sigaction act = {
        .sa_handler = sigterm_handler,
        .sa_flags = 0,
    };
    struct stat sb;
    int lockfd;

    progname = argv[0];

    while ((ch = getopt(argc, argv, "Cfhr:Rtv:V")) != -1) {
        switch (ch) {
        case 'C':
            /* clear cache */
            flags |= SVNC_FLUSHCACHE;
            break;

        case 'f':
            /* clear lock file */
            flags |= SVNC_CLEAR_LOCKFILE;
            break;

        case 'h':
            usage(progname);
            exit(0);
            break;

        case 'r':
            target_rev = strtol(optarg, NULL, 10);
            break;

        case 'R':
            /* repair mode */
            flags |= SVNC_REPAIR;
            break;

        case 't':
            /* be tolerant */
            flags |= SVNC_TOLERANT;
            break;

        case 'v':
            /* partially implemented */
            debug_level = strtol(optarg, NULL, 10);
            break;

        case 'V':
            version(progname);
            exit(0);

        default:
            usage(progname);
            exit(1);

        }
    }

    argc -= optind;
    argv += optind;

    if ((flags & SVNC_REPAIR) &&
        (flags & (SVNC_FLUSHCACHE | SVNC_TOLERANT))) {

        errx(1, "-R can be combined with neither -C nor -t.");
    }

    if (argc > 0) {
        url = argv[0];
    }

    if (argc > 1) {
        localroot = argv[1];
    } else {
        localroot = ".";
    }

    if (*localroot == '-') {
        errx(1, "Invalid local root: %s.", localroot);
    }

    /* set up local root */
    if ((absroot = abspath(localroot)) == NULL) {
        errx(1, "abspath");
    }

    if (lstat(absroot, &sb) == 0) {
        if (!S_ISDIR(sb.st_mode)) {
            errx(1, "Not a directory: %s", absroot);
        }
    } else {
        if (svncdir_mkdirs(absroot) != 0) {
            perror("svnc_mkdirs");
            errx(1, "Could not make directory: %s", absroot);
        }
    }

    //TRACE("command-line arguments: %s %ld %s", url, target_rev, localroot);

    if (atexit(atexit_cb) != 0) {
        errx(1, "atexit");
    }

    /* check and obtain lock */
    if ((lockfile = path_join(absroot, LOCKFILE)) == NULL) {
        FAIL("path_join LOCKFILE");
    }
    if (lstat(lockfile, &sb) == 0) {
        if (flags & SVNC_CLEAR_LOCKFILE) {
            if (unlink(lockfile) != 0) {
                /* ignore */
                ;
            }
        } else {
            LTRACE(0, "Cannot run: another instance is running on %s", localroot);
            exit(1);
        }
    }
    if ((lockfd = open(lockfile, O_WRONLY|O_CREAT|O_TRUNC, 0600)) < 0) {
        FAIL("open");
    }
    close(lockfd);

    sigemptyset(&act.sa_mask);
    sigaction(SIGTERM, &act, NULL);
    sigaction(SIGINT, &act, NULL);

    SSL_load_error_strings();
    SSL_library_init();
    OpenSSL_add_all_algorithms();

    run(url, target_rev, absroot, flags, debug_level);

    ERR_free_strings();

    free(lockfile);
    lockfile = NULL;

    return res;
}
