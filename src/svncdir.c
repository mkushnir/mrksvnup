#include <string.h>

#include "diag.h"
#include "mrkcommon/util.h"
#include "mrkcommon/dumpm.h"
#include "mrkcommon/traversedir.h"

#include "mrksvnup/svnc.h"
#include "mrksvnup/svnproto.h"
#include "mrksvnup/svncdir.h"

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
    svnproto_dirent_t *de;
    svnproto_fileent_t fe;

    svnproto_init_dirent_array(&dirents);

    if (svnproto_get_latest_rev(ctx, &rev) != 0) {
        res = SVNCDIR_WALK + 1;
        goto END;
    }

    //TRACE("rev=%ld", rev);

    if (svnproto_check_path(ctx, path, rev, &kind) != 0) {
        res = SVNCDIR_WALK + 1;
        goto END;
    }

    if (kind != SVNP_KIND_DIR) {
        res = SVNCDIR_WALK + 1;
        goto END;
    }

    if (svnproto_get_dir(ctx, path, rev, &dirents) != 0) {
        res = SVNCDIR_WALK + 1;
        goto END;
    }

    //svnproto_dump_dirent_array(&dirents);

    for (de = array_first(&dirents, &it);
         de != NULL;
         de = array_next(&dirents, &it)) {

        char *newpath = path_join(path, BDATA(de->name));

        if (de->kind == SVNP_KIND_FILE) {

            //TRACE(FGREEN("FILE %s"), newpath);

            svnproto_fileent_init(&fe);

            if (svnproto_get_file(ctx, newpath, de->rev,
                                  GETFLAG_WANT_PROPS,
                                  &fe) != 0) {

                free(newpath);
                newpath = NULL;
                svnproto_fileent_fini(&fe);
                res = SVNCDIR_WALK + 1;
                goto END;
            }

            //svnproto_fileent_dump(&fe);
            //
            if (cb != NULL) {
                if (cb(ctx, path, de, newpath, &fe, udata) != 0) {
                    free(newpath);
                    newpath = NULL;
                    svnproto_fileent_fini(&fe);
                    res = SVNCDIR_WALK + 1;
                    goto END;
                }
            }

            svnproto_fileent_fini(&fe);

        } else if (de->kind == SVNP_KIND_DIR) {

            if (cb != NULL) {
                if (cb(ctx, path, de, newpath, NULL, udata) != 0) {
                    free(newpath);
                    newpath = NULL;
                    res = SVNCDIR_WALK + 1;
                    goto END;
                }
            }

            if (svncdir_walk(ctx, newpath, cb, udata) != 0) {

                free(newpath);
                newpath = NULL;
                res = SVNCDIR_WALK + 1;
                goto END;
            }

        } else {
            TRACE("name=%s kind=%s ???",
                  de->name->data, svnproto_kind2str(de->kind));
        }

        free(newpath);
        newpath = NULL;
    }



END:
    if (res != 0) {
        //bytestream_dump(&ctx->in);
    }

    array_fini(&dirents);
    return res;
}


