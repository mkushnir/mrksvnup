#include <err.h>
#include <libgen.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>

#include "mrkcommon/dumpm.h"
#include "mrkcommon/util.h"
#include "mrkcommon/traversedir.h"

#include "mrksvnup/svnc.h"
#include "mrksvnup/svnproto.h"

const char *_malloc_options = "J";
#define DOTFILE ".svnup"

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

    TRACE("set-path OK");

    if (svnproto_finish_report(ctx) != 0) {
        errx(1, "svnproto_finish_report");
    }

    TRACE("finish-report OK");

    if (svnproto_editor(ctx) != 0) {
        errx(1, "svnproto_editor");
    }

    return 0;
}

static void
run(const char *url,
    long target_rev,
    const char *localroot)
{
    long source_rev = -1;
    svnc_ctx_t *ctx;
    int kind = -1;
    struct {
        const char *path;
        long source_rev;
        long set_path_flags;
    } update_params = {"", -1, 0};
    char *dotfile = NULL;
    struct stat sb;
    int fd;

    /* we save source revision in a dotfile */
    if ((dotfile = path_join(localroot, DOTFILE)) == NULL) {
        errx(1, "path_join() issue");
    }
    if (lstat(dotfile, &sb) == 0 && S_ISREG(sb.st_mode)) {
        if ((fd = open(dotfile, O_RDONLY)) >= 0) {
            char buf[64];
            ssize_t nread;
            if ((nread = read(fd, buf, 64)) > 0) {
                buf[nread] = '\0';
                source_rev = strtol(buf, NULL, 10);
            }
            close(fd);
            TRACE("Found saved source revision: %ld", source_rev);
        }
    }

    /* set up local root */

    if (lstat(localroot, &sb) == 0) {
        if (!S_ISDIR(sb.st_mode)) {
            errx(1, "Not a directory: %s", localroot);
        }
    }

    if ((ctx = svnc_new(url)) == NULL) {
        errx(1, "svnc_new");
    }

    ctx->localroot = strdup(localroot);

    if (svnc_connect(ctx) != 0) {
        errx(1, "svnc_connect");
    }

    if (source_rev <= 0) {
        /* check out */
        if (target_rev <= 0) {
            if (svnproto_get_latest_rev(ctx, &target_rev) != 0) {
                errx(1, "svnproto_get_latest_rev 1");
            }
        }
        source_rev = target_rev;

        update_params.set_path_flags |= SETPFLAG_START_EMPTY;
    } else {
        /* update */
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
    TRACE("check_path kind=%s", svnproto_kind2str(kind));

    update_params.source_rev = source_rev;

    if (target_rev > 0) {
        if (source_rev >target_rev) {
            errx(1, "Source revision found greater that the target one: "
                "%ld > %ld", source_rev, target_rev);
        }
    }

    if (svnproto_update(ctx, target_rev, update_params.path, 0,
                        UPFLAG_RECURSE, update_cb, &update_params) != 0) {
        errx(1, "svnproto_update");
    }

    TRACE("source %ld target %ld", source_rev, target_rev);
    if ((fd = open(dotfile, O_RDWR|O_CREAT|O_TRUNC, 0644)) >= 0) {
        char buf[64];
        int sz = snprintf(buf, sizeof(buf), "%ld", target_rev);
        if (write(fd, buf, sz) < 0) {
            err(1, "write");
        }
        close(fd);
        TRACE("Saved revision: %ld", target_rev);
    } else {
        err(1, "open");
    }
    TRACE("OK");

    svnc_close(ctx);
    svnc_destroy(ctx);
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

    while ((ch = getopt(argc, argv, "u:r:l:hvV")) != -1) {
        switch (ch) {
        case 'u':
            url = optarg;
            break;

        case 'r':
            target_rev = strtol(optarg, NULL, 10);
            break;

        case 'l':
            localroot = optarg;
            break;

        case 'h':
            usage(argv[0]);
            exit(0);
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

    run(url, target_rev, localroot);

    return res;
}
