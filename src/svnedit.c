#include <assert.h>
#include <fcntl.h>
/* goes before md5.h */
#include <sys/types.h>
#include <md5.h>
#include <sys/param.h>
#include <sys/stat.h>

#include "mrkcommon/dumpm.h"
#include "mrkcommon/bytestream.h"
#include "mrkcommon/traversedir.h"
#include "mrkcommon/util.h"

#include "mrksvnup/svnc.h"
#include "mrksvnup/svndiff.h"
#include "mrksvnup/svnedit.h"

#define VCBUFSZ (PAGE_SIZE * 512)

static svnc_ctx_t *shadow_ctx = NULL;
static long target_rev;
static svndiff_doc_t doc;

int
svnedit_init_shadow_ctx(svnc_ctx_t *ctx)
{
    assert(shadow_ctx == NULL);

    if ((shadow_ctx = svnc_new(ctx->url,
                               ctx->localroot,
                               SVNC_NNOCACHE,
                               ctx->debug_level)) == NULL) {
        TRRET(SVNEDIT_INIT_SHADOW_CTX + 1);
    }

    if (svnc_connect(shadow_ctx) != 0) {
        TRRET(SVNEDIT_INIT_SHADOW_CTX + 2);
    }

    TRRET(0);
}

void
svnedit_close_shadow_ctx(void)
{
    if (shadow_ctx != NULL) {
        svnc_close(shadow_ctx);
        svnc_destroy(shadow_ctx);
        free(shadow_ctx);
        shadow_ctx = NULL;
    }
}

svndiff_doc_t *
svnedit_clear_doc(void)
{
    if (svndiff_doc_init(&doc) != 0) {
        TRRETNULL(SVNEDIT_CLEAR_DOC + 1);
    }
    return &doc;
}

svndiff_doc_t *
svnedit_get_doc(void)
{
    return &doc;
}


int
svnedit_verify_checksum(int fd, const bytes_t *cs)
{
    int res = 0;
    char buf[VCBUFSZ];
    ssize_t nread;
    MD5_CTX ctx;
    char *s = NULL;

    MD5Init(&ctx);

    if (lseek(fd, 0, SEEK_SET) != 0) {
        perror("lseek");
        res = SVNEDIT_VERIFY_CHECKSUM + 1;
        goto END;
    }

    while ((nread = read(fd, buf, VCBUFSZ)) > 0) {
        MD5Update(&ctx, buf, (unsigned int)nread);
    }

    if (nread < 0) {
        perror("read");
        res = SVNEDIT_VERIFY_CHECKSUM + 2;
        goto END;
    }

    if ((s = MD5End(&ctx, NULL)) == NULL) {
        perror("ND5End");
        res = SVNEDIT_VERIFY_CHECKSUM + 3;
        goto END;
    }

    //TRACE("s=%s cs=%s", s, cs);

    if (strncmp(s, cs->data, cs->sz) != 0) {
        res = SVNEDIT_VERIFY_CHECKSUM + 4;
        goto END;
    }

END:
    if (s != NULL) {
        free(s);
        s = NULL;
    }
    return res;
}

int
svnedit_target_rev(UNUSED svnc_ctx_t *ctx, long rev)
{
    target_rev = rev;
    TRRET(0);
}

int
svnedit_open_root(svnc_ctx_t *ctx,
                 UNUSED long rev,
                 UNUSED bytes_t *token)
{
    struct stat sb;

    if (lstat(ctx->localroot, &sb) != 0) {
        if (mkdir(ctx->localroot, 0755) != 0) {
            TRRET(SVNEDIT_OPENROOT + 1);
        }
    } else {
        if (!S_ISDIR(sb.st_mode)) {
            TRRET(SVNEDIT_OPENROOT + 2);
        }
    }
    TRRET(0);
}

static int
delete_entry_cb(const char *path,
                struct dirent *de,
                UNUSED void *udata)
{
    int res = 0;
    char *fname;

    /*
     * XXX Need support for checkng modified and untracked files/dirs.
     */
    if (de != NULL) {

        if ((fname = path_join(path, de->d_name)) == NULL) {
            return 1;
        }

        //TRACE("deleting file: %s", fname);

        if (unlink(fname) != 0) {
            perror("unlink");
            res = 1;
        }

        free(fname);

    } else {
        //TRACE("deleting dir: %s", path);

        if (rmdir(path) != 0) {
            perror("rmdir");
            res = 2;
        }
    }
    return res;
}

int
svnedit_delete_entry(svnc_ctx_t *ctx,
                     bytes_t *path,
                     UNUSED long rev,
                     UNUSED bytes_t *token)
{
    int res = 0;
    char *localpath = NULL;
    UNUSED struct stat sb;

    if ((localpath = path_join(ctx->localroot,
                               BDATA(path))) == NULL) {
        res = SVNEDIT_DELETE_ENTRY + 1;
        goto END;
    }

    if (traverse_dir(localpath, delete_entry_cb, NULL) != 0) {
        /* Is this a file in first place? */
        if (lstat(localpath, &sb) != 0) {
            if (ctx->debug_level > 1) {
                LTRACE(1, FGREEN("- %s -> %s"), BDATA(path), localpath);
            }

        } else {
            if (S_ISREG(sb.st_mode) || S_ISLNK(sb.st_mode)) {

                if (unlink(localpath) != 0) {
                    perror("unlink");
                    res = SVNEDIT_DELETE_ENTRY + 2;
                    goto END;
                }
                if (ctx->debug_level > 1) {
                    LTRACE(1, FGREEN("- %s -> %s"), BDATA(path), localpath);
                }

            } else {
                if (ctx->debug_level > 2) {
                    LTRACE(1, FYELLOW("Failed to fully delete %s"), localpath);
                }
            }
        }
    } else {
        if (ctx->debug_level > 1) {
            LTRACE(1, FGREEN("- %s -> %s"), BDATA(path), localpath);
        }
    }

    if (svnc_delete_checksum(ctx, BDATA(path)) != 0) {
        if (ctx->debug_level > 2) {
            LTRACE(1, FYELLOW("Failed to delete checksum for %s (ignoring)"),
                   BDATA(path));
        }
    }

END:
    if (localpath != NULL) {
        free(localpath);
    }
    TRRET(res);
}

int
svnedit_add_dir(svnc_ctx_t *ctx,
                bytes_t *path)
{
    int res = 0;
    char *localpath = NULL;
    struct stat sb;

    if ((localpath = path_join(ctx->localroot,
                               BDATA(path))) == NULL) {
        res = SVNEDIT_ADD_DIR + 1;
        goto END;
    }

    if (lstat(localpath, &sb) != 0) {
        if (mkdir(localpath, 0755) != 0) {
            res = SVNEDIT_ADD_DIR + 2;
            goto END;
        }
    } else {
        if (!S_ISDIR(sb.st_mode)) {
            res = SVNEDIT_ADD_DIR + 3;
            goto END;
        }
    }

    if (ctx->debug_level > 0) {
        LTRACE(1, FGREEN("+ %s -> %s"), BDATA(path), localpath);
    }

END:
    if (localpath != NULL) {
        free(localpath);
    }
    TRRET(res);
}

int
svnedit_open_dir(svnc_ctx_t *ctx,
                 bytes_t *path)
{
    int res = 0;
    char *localpath = NULL;
    struct stat sb;

    if ((localpath = path_join(ctx->localroot,
                               BDATA(path))) == NULL) {
        res = SVNEDIT_OPEN_DIR + 1;
        goto END;
    }

    if (lstat(localpath, &sb) != 0) {
        /*
         * we are being told to open dir, but it doesn't even
         * exist. Create it.
         */
        if (mkdir(localpath, 0755) != 0) {
            res = SVNEDIT_OPEN_DIR + 2;
            goto END;
        }
    } else {
        if (!S_ISDIR(sb.st_mode)) {
            res = SVNEDIT_OPEN_DIR + 3;
            goto END;
        }
    }

    if ((localpath = path_join(ctx->localroot,
                               BDATA(path))) == NULL) {
        res = SVNEDIT_OPEN_DIR + 4;
        goto END;
    }

    if (lstat(localpath, &sb) != 0) {
        /*
         * we are being told to open dir, but it doesn't even
         * exist. Create it.
         */
        if (mkdir(localpath, 0755) != 0) {
            res = SVNEDIT_OPEN_DIR + 5;
            goto END;
        }
    } else {
        if (!S_ISDIR(sb.st_mode)) {
            res = SVNEDIT_OPEN_DIR + 6;
            goto END;
        }
    }

END:
    if (localpath != NULL) {
        free(localpath);
    }
    TRRET(res);
}

int
svnedit_add_file(svnc_ctx_t *ctx)
{
    int res = 0;

    if ((doc.lp = path_join(ctx->localroot,
                               BDATA(doc.rp))) == NULL) {
        res = SVNEDIT_ADD_FILE + 1;
        goto END;
    }

    if (lstat(doc.lp, &doc.sb) == 0) {
        if (S_ISREG(doc.sb.st_mode) || S_ISLNK(doc.sb.st_mode)) {
            if (unlink(doc.lp) != 0) {
                FAIL("unlink");
            }
        } else {
            /* we are not yet handling it */
            res = SVNEDIT_ADD_FILE + 2;
            goto END;
        }
    }

END:
    TRRET(res);
}

static int
create_file(svndiff_doc_t *doc, svnc_fileent_t *fe)
{
    int res = 0;
    bytes_t **s;
    array_iter_t it;
    ssize_t total_len = 0;
    int fd = -1;

    if ((fd = open(doc->lp,
                        O_RDWR|O_CREAT|O_TRUNC|O_NOFOLLOW,
                        doc->mod)) < 0) {
        perror("open");
        TRACE(FRED("Failed to create %s"), doc->lp);
        res = CREATE_FILE + 1;
        goto END;
    }

    if (lseek(fd, 0, SEEK_SET) != 0) {
        FAIL("lseek");
    }

    for (s = array_first(&fe->contents, &it);
         s != NULL;
         s = array_next(&fe->contents, &it)) {

        if (write(fd, (*s)->data, (*s)->sz) < 0) {
            FAIL("write");
        }
        total_len += (*s)->sz;
    }

    if (ftruncate(fd, total_len) != 0) {
        FAIL("ftruncate");
    }

    if (lseek(fd, 0, SEEK_SET) != 0) {
        FAIL("lseek");
    }

    if (fchmod(fd, doc->mod) != 0) {
        FAIL("fchmod");
    }
END:
    if (fd != -1) {
        close(fd);
        fd = -1;
    }
    TRRET(res);
}

static int
create_symlink(svndiff_doc_t *doc, svnc_fileent_t *fe)
{
    int res = 0;
    bytes_t **s;
    array_iter_t it;
    array_t lnk;
    char *data;

    if (array_init(&lnk, sizeof(char), 0, NULL, NULL) != 0) {
        FAIL("array_init");
    }

    for (s = array_first(&fe->contents, &it);
         s != NULL;
         s = array_next(&fe->contents, &it)) {

        array_ensure_len(&lnk, lnk.elnum + (*s)->sz, ARRAY_FLAG_SAVE);
        memcpy((char *)array_get(&lnk, lnk.elnum), (*s)->data, (*s)->sz);
    }
    /* terminator */
    array_incr(&lnk);

    /* A weakref to internal buffer. Hackish. We just know that lnk.data is
     * of char * type.
     */
    data = (char *)lnk.data;
    *(data + lnk.elnum) = '\0';
    if (strstr(data, "link ") == NULL) {
        res = CREATE_SYMLINK + 1;
        goto END;
    }
    /* strlen("link") */
    *(data + 4) = '\0';
    data += 5;

    if (symlink(data, doc->lp) < 0) {
        perror("symlink");
        TRACE(FRED("Failed to symlink %s to %s"), data, doc->lp);
        res = CREATE_SYMLINK + 2;
        goto END;
    }

END:
    if (array_fini(&lnk) != 0) {
        FAIL("array_fini");
    }

    TRRET(res);
}

static int
checkout_file(svnc_ctx_t *ctx, svndiff_doc_t *doc, long rev)
{
    int res = 0;
    svnc_fileent_t fe;
    svnc_prop_t *prop;
    array_iter_t it;

    svnc_fileent_init(&fe);

    if (shadow_ctx->debug_level > 1) {
        LTRACE(1, FYELLOW("! %s,%ld ->%s"), BDATA(doc->rp), rev, doc->lp);
    }

    assert(shadow_ctx->get_file != NULL);
    if (shadow_ctx->get_file(shadow_ctx,
                             BDATA(doc->rp),
                             rev,
                             GETFLAG_WANT_CONTENTS|GETFLAG_WANT_PROPS,
                             &fe) != 0) {
        res = CHECKOUT_FILE + 1;
        goto END;
    }

    doc->flags = 0;
    for (prop = array_first(&fe.props, &it);
         prop != NULL;
         prop = array_next(&fe.props, &it)) {

        if (strcmp(BDATA(prop->name), ctx->executable_prop_name) == 0) {
            if (prop->value == NULL) {
                doc->mod = 0644;
            } else {
                doc->mod = 0755;
            }
            doc->flags |= SD_FLAG_MOD_SET;
        }
        if (strcmp(BDATA(prop->name), ctx->special_prop_name) == 0) {
            doc->flags |= SD_FLAG_SYMLINK_SET;
        }
    }

    if (doc->flags & SD_FLAG_SYMLINK_SET) {
        if (create_symlink(doc, &fe) != 0) {
            res = CHECKOUT_FILE + 2;
            goto END;
        }
    } else {
        if (create_file(doc, &fe) != 0) {
            res = CHECKOUT_FILE + 3;
            goto END;
        }
    }

END:
    svnc_fileent_fini(&fe);
    TRRET(res);
}

int
svnedit_open_file(svnc_ctx_t *ctx)
{
    int res = 0;

    if ((doc.lp = path_join(ctx->localroot,
                               BDATA(doc.rp))) == NULL) {
        res = SVNEDIT_OPEN_FILE + 1;
        goto END;
    }

    /* make sure the file exists locally */
    if (lstat(doc.lp, &doc.sb) == 0) {
        if (!S_ISREG(doc.sb.st_mode) && !S_ISLNK(doc.sb.st_mode)) {
            /* we are not yet handling it */
            res = SVNEDIT_OPEN_FILE + 2;
            if (ctx->debug_level > 0) {
                LTRACE(1, FRED("Not a file: path=%s rev=%ld"),
                       BDATA(doc.rp), doc.rev);
            }
            goto END;
        }
        /* The file exists. */
    } else {
        if (checkout_file(ctx, &doc, doc.rev) != 0) {
            res = SVNEDIT_OPEN_FILE + 3;
            goto END;
        }
    }

END:
    TRRET(res);
}

int
svnedit_change_file_prop(svnc_ctx_t *ctx,
                         bytes_t *name,
                         bytes_t *value)
{
    /*
     * XXX The below logic is quite simplistic. It should eventually be
     * improved.
     */
    if (strcmp(BDATA(name), ctx->executable_prop_name) == 0) {
        if (value == NULL) {
            doc.mod = 0644;
        } else {
            doc.mod = 0755;
        }
        doc.flags |= SD_FLAG_MOD_SET;
    }

    if (strcmp(BDATA(name), ctx->special_prop_name) == 0) {
        doc.flags |= SD_FLAG_SYMLINK_SET;
    }

    TRRET(0);
}

static int
checksum_cb(svndiff_wnd_t *wnd, MD5_CTX *ctx)
{
    MD5Update(ctx, wnd->tview, wnd->tview_len);
    return 0;
}

int
svnedit_close_file(svnc_ctx_t *ctx,
                   bytes_t *file_token,
                   bytes_t *text_checksum)
{
    int res = 0;
    MD5_CTX mctx;
    array_iter_t it;
    char *checksum = NULL;
    svndiff_wnd_t *wnd;
    ssize_t total_len;

    if ((BDATA(file_token) != NULL) && (BDATA(doc.ft) != NULL)) {
        if (strcmp(BDATA(file_token), BDATA(doc.ft)) != 0) {
            res = SVNEDIT_CLOSE_FILE + 1;
            goto END;
        }
    }

    assert(doc.fd == -1);

    /* verify source view, if available */
    if ((doc.fd = open(doc.lp, O_RDWR|O_NOFOLLOW)) >= 0) {

        /* We deal with a regular file */

        if (doc.base_checksum != NULL) {

            /* 
             * cmd was open-file
             */

            if (svnedit_verify_checksum(doc.fd,
                    doc.base_checksum) != 0) {

                /* check it out clean? */
                if (ctx->debug_level > 2) {
                    LTRACE(1, FRED("Base checksum mismatch: "
                                   "expected %s over %s"),
                           BDATA(doc.base_checksum), doc.lp);
                }

                close(doc.fd);
                doc.fd = -1;

                if (checkout_file(ctx, &doc, target_rev) != 0) {
                    res = SVNEDIT_CLOSE_FILE + 2;
                    goto END;
                }
                goto EDIT_COMPLETE;
            }
        } else {
            /*
             * cmd was add-file, but there was a local file in this
             * place. Will discard its contents and replace with ours.
             */
        }
    } else {
        /*
         * was reg/symlink add-file or symlink open-file?
         *
         * symlink open-files should never have instructions other than
         * new (hopefully), so we think we may pass -1 as fd to
         * svndiff_build_tview().
         */
    }

    /* build target view */
    if (array_traverse(&doc.wnd,
                      (array_traverser_t)svndiff_build_tview,
                      &doc) != 0)  {

        res = SVNEDIT_CLOSE_FILE + 3;
        goto END;
    }

    /* verify target view */
    if (BDATA(text_checksum) != NULL && doc.version != -1) {

        MD5Init(&mctx);
        array_traverse(&doc.wnd, (array_traverser_t)checksum_cb, &mctx);

        if ((checksum = MD5End(&mctx, NULL)) == NULL) {
            perror("ND5End");
            res = SVNEDIT_CLOSE_FILE + 4;
            goto END;
        }

        if (strcmp(checksum, BDATA(text_checksum)) != 0) {

            if (ctx->debug_level > 2) {
                LTRACE(1, FRED("Target checksum mismtach: "
                               "expected %s over %s"),
                       BDATA(text_checksum), doc.lp);
            }

            close(doc.fd);
            doc.fd = -1;

            if (checkout_file(ctx, &doc, target_rev) != 0) {
                res = SVNEDIT_CLOSE_FILE + 5;
                goto END;
            }
            goto EDIT_COMPLETE;
        }
    }

    if (doc.fd == -1) {
        /* regular/symlink add-file or symlink open-file completion */

        if (doc.flags & SD_FLAG_SYMLINK_SET) {
            /* symlink add-file */

            /*
             * for simplicity, we assume the symlink target occupies
             * a single window
             */
            if ((wnd = array_first(&doc.wnd, &it)) != NULL) {
                char *data = wnd->tview;

                *(data + wnd->tview_len) = '\0';

                if (strstr(data, "link ") == NULL) {
                    res = SVNEDIT_CLOSE_FILE + 6;
                    goto END;
                }
                /* strlen("link ") */
                *(data + 4) = '0';
                data += 5;

                if (symlink(data, doc.lp) < 0) {
                    perror("symlink");
                    TRACE(FRED("Failed to symlink %s to %s"), data, doc.lp);
                    res = SVNEDIT_CLOSE_FILE + 7;
                    goto END;
                }

            } else {
                /* nothing to change */
            }
            goto EDIT_COMPLETE;

        } else {
            /* regular add-file or symlink open-file, try open it */
            if ((doc.fd = open(doc.lp,
                               O_RDWR|O_CREAT|O_NOFOLLOW, doc.mod)) < 0) {
                /* symlink open-file */
                if ((wnd = array_first(&doc.wnd, &it)) != NULL) {
                    ssize_t nread;
                    char olddata[1024];
                    char *data;

                    if ((nread = readlink(doc.lp, olddata,
                                          sizeof(olddata))) < 0) {
                        FAIL("readlink");
                    }
                    olddata[nread] = '\0';

                    data = wnd->tview;
                    *(data + wnd->tview_len) = '\0';
                    if (strstr(data, "link ") == NULL) {
                        res = SVNEDIT_CLOSE_FILE + 8;
                        goto END;
                    }
                    /* strlen("link") */
                    *(data + 4) = '0';
                    data += 5;

                    if (ctx->debug_level > 2) {
                        LTRACE(1, "Relinking from %s to %s", olddata, data);
                    }

                    if (unlink(doc.lp) < 0) {
                        perror("unlink");
                        TRACE(FRED("Failed to unlink %s"), doc.lp);
                    }

                    if (symlink(data, doc.lp) < 0) {
                        perror("symlink");
                        TRACE(FRED("Failed to symlink %s to %s"),
                              data, doc.lp);
                        res = SVNEDIT_CLOSE_FILE + 9;
                        goto END;
                    }

                } else {
                    /* nothing to change */
                }
                goto EDIT_COMPLETE;

            }
        }
    }

    /* regular add-file and open-file completion */

    if (doc.version != -1) {

        if (lseek(doc.fd, 0, SEEK_SET) != 0) {
            FAIL("lseek");
        }
        total_len = 0;

        for (wnd = array_first(&doc.wnd, &it);
             wnd != NULL;
             wnd = array_next(&doc.wnd, &it)) {

            if (write(doc.fd, wnd->tview, wnd->tview_len) < 0) {
                FAIL("write");
            }
            total_len += wnd->tview_len;
        }

        if (ftruncate(doc.fd, total_len) != 0) {
            FAIL("ftruncate");
        }

    }

    if (fchmod(doc.fd, doc.mod) != 0) {
        FAIL("fchmod");
    }

EDIT_COMPLETE:
    if (svnc_save_checksum(ctx, BDATA(doc.rp), text_checksum) != 0) {
        res = SVNEDIT_CLOSE_FILE + 10;
        goto END;
    }

    if (ctx->debug_level > 0) {
        if (BDATA(doc.base_checksum) == NULL) {
            /* this is for "presentation" purposes */
            if (doc.flags & SD_FLAG_MOD_SET) {
                LTRACE(1, FGREEN("~ %s -> %s (%04o)"),
                       BDATA(doc.rp), doc.lp, doc.mod);
            } else {
                LTRACE(1, FGREEN("+ %s -> %s"), BDATA(doc.rp), doc.lp);
            }
        } else {
            LTRACE(1, FGREEN("~ %s -> %s"), BDATA(doc.rp), doc.lp);
        }
    }


END:
    if (svndiff_doc_fini(&doc) != 0) {
        TRACE("svndiff_doc_fini() failure, ignoring");
    }
    TRRET(res);
}

