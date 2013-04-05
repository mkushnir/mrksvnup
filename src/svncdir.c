#include <assert.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>

//#define TRRET_DEBUG
#include "diag.h"
#include "mrkcommon/util.h"
#include "mrkcommon/dumpm.h"
#include "mrkcommon/traversedir.h"

#include "mrksvnup/svnc.h"
#include "mrksvnup/svnproto.h"
#include "mrksvnup/svncdir.h"

/* another recursive mkdir */

static int
make_one_dir(const char *path, const char *last)
{
    struct stat sb;

    //TRACE("path '%s' last '%s'", path, last);

    if (*path == '\0') {
        return 0;
    }

    if (strcmp(last, "..") == 0) {
        TRRET(SVNCDIR_MKDIRS + 1);
    }

    if (lstat(path, &sb) != 0) {
        if (mkdir(path, 0755) != 0) {
            TRRET(SVNCDIR_MKDIRS + 2);
        }
    } else {
        if (!(S_ISDIR(sb.st_mode) || S_ISLNK(sb.st_mode))) {
            TRRET(SVNCDIR_MKDIRS + 3);
        }
    }
    return 0;
}

int
svncdir_mkdirs(const char *path)
{
    int res = 0;
    char *buf, *pbuf0, *pbuf1;

    if ((buf = strdup(path)) == NULL) {
        FAIL("strdup");
    }
    pbuf0 = buf;

    //TRACE("path=%s", path);

    for (pbuf1 = strchr(pbuf0, '/');
         pbuf1 != NULL;
         pbuf1 = strchr(pbuf0, '/')) {

        /* tempararily terminate */
        *pbuf1 = '\0';

        if (make_one_dir(buf, pbuf0)) {
            res = SVNCDIR_MKDIRS + 4;
            goto END;
        }

        /*restore separator */
        *pbuf1 = '/';

        ++pbuf1;
        pbuf0 = pbuf1;
    }

    if (make_one_dir(buf, pbuf0)) {
        res = SVNCDIR_MKDIRS + 5;
        goto END;
    }


END:
    free(buf);
    buf = NULL;
    TRRET(res);
}

int
svncdir_path_walk(UNUSED const char *path)
{
    return 0;
}


int
svncdir_walk(svnc_ctx_t *ctx, const char *path, svncdir_cb_t cb, void *udata)
{
    int res = 0;
    long rev = 0;
    int kind = -1;
    array_t dirents;
    array_iter_t it;
    svnc_dirent_t *de;
    svnc_fileent_t fe;

    svnproto_init_dirent_array(&dirents);

    assert(ctx->get_latest_rev != NULL);
    if (ctx->get_latest_rev(ctx, &rev) != 0) {
        res = SVNCDIR_WALK + 1;
        goto END;
    }

    //TRACE("rev=%ld", rev);

    assert(ctx->check_path != NULL);
    if (ctx->check_path(ctx, path, rev, &kind) != 0) {
        res = SVNCDIR_WALK + 2;
        goto END;
    }

    if (kind != SVNC_KIND_DIR) {
        res = SVNCDIR_WALK + 3;
        goto END;
    }

    if (svnproto_get_dir(ctx, path, rev, &dirents) != 0) {
        res = SVNCDIR_WALK + 4;
        goto END;
    }

    //svnproto_dump_dirent_array(&dirents);

    for (de = array_first(&dirents, &it);
         de != NULL;
         de = array_next(&dirents, &it)) {

        char *newpath = path_join(path, BDATA(de->name));

        if (de->kind == SVNC_KIND_FILE) {

            //TRACE(FGREEN("FILE %s"), newpath);

            svnc_fileent_init(&fe);

            assert(ctx->get_file != NULL);
            if (ctx->get_file(ctx, newpath, de->rev,
                              GETFLAG_WANT_PROPS,
                              &fe) != 0) {

                free(newpath);
                newpath = NULL;
                svnc_fileent_fini(&fe);
                res = SVNCDIR_WALK + 5;
                goto END;
            }

            //svnc_fileent_dump(&fe);
            //
            if (cb != NULL) {
                if (cb(ctx, path, de, newpath, &fe, udata) != 0) {
                    free(newpath);
                    newpath = NULL;
                    svnc_fileent_fini(&fe);
                    res = SVNCDIR_WALK + 6;
                    goto END;
                }
            }

            svnc_fileent_fini(&fe);

        } else if (de->kind == SVNC_KIND_DIR) {

            if (cb != NULL) {
                if (cb(ctx, path, de, newpath, NULL, udata) != 0) {
                    free(newpath);
                    newpath = NULL;
                    res = SVNCDIR_WALK + 7;
                    goto END;
                }
            }

            if (svncdir_walk(ctx, newpath, cb, udata) != 0) {

                free(newpath);
                newpath = NULL;
                res = SVNCDIR_WALK + 8;
                goto END;
            }

        } else {
            TRACE("name=%s kind=%s ???",
                  de->name->data, svnc_kind2str(de->kind));
        }

        free(newpath);
        newpath = NULL;
    }



END:
    if (res != 0) {
        //bytestream_dump(&ctx->in);
    }

    array_fini(&dirents);
    TRRET(res);
}


