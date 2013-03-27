#include <assert.h>
#include <fcntl.h>
/* goes before md5.h */
#include <sys/types.h>
#include <md5.h>
#include <string.h>
#include <sys/param.h>
#include <sys/stat.h>
#include <stdio.h>
#include <unistd.h>

#include "mrkcommon/array.h"
#include "mrkcommon/util.h"
#include "mrkcommon/traversedir.h"
//#define TRRET_DEBUG
#include "mrkcommon/dumpm.h"
#include "diag.h"

#include "mrksvnup/bytestream.h"
#include "mrksvnup/svnc.h"
#include "mrksvnup/svncdir.h"
#include "mrksvnup/svnproto.h"
#include "mrksvnup/svndiff.h"

#define VCBUFSZ (PAGE_SIZE * 512)

/* global editor context */
static char *cmd = NULL;
static svndiff_doc_t doc;
#define SVNPE_CLOSING 0x01
static int flags = 0;
static svnc_ctx_t *shadow_ctx;
static long target_rev;

int
svnproto_editor_verify_checksum(int fd, const svnproto_bytes_t *cs)
{
    int res = 0;
    char buf[VCBUFSZ];
    ssize_t nread;
    MD5_CTX ctx;
    char *s = NULL;

    MD5Init(&ctx);

    if (lseek(fd, 0, SEEK_SET) != 0) {
        perror("lseek");
        res = VERIFY_CHECKSUM_FD + 1;
        goto END;
    }

    while ((nread = read(fd, buf, VCBUFSZ)) > 0) {
        MD5Update(&ctx, buf, (unsigned int)nread);
    }

    if (nread < 0) {
        perror("read");
        res = VERIFY_CHECKSUM_FD + 2;
        goto END;
    }

    if ((s = MD5End(&ctx, NULL)) == NULL) {
        perror("ND5End");
        res = VERIFY_CHECKSUM_FD + 3;
        goto END;
    }

    //TRACE("s=%s cs=%s", s, cs);

    if (strncmp(s, cs->data, cs->sz) != 0) {
        res = VERIFY_CHECKSUM_FD + 4;
        goto END;
    }

END:
    if (s != NULL) {
        free(s);
        s = NULL;
    }
    return res;
}

static int
chunk_cb(UNUSED svnc_ctx_t *ctx,
         bytestream_t *in,
         svnproto_state_t *st,
         UNUSED void *udata)
{

    if (svndiff_parse_doc(SDATA(in, st->r.start),
                          SDATA(in, st->r.end),
                          &doc) != 0) {

        /*
         * XXX do we really need to signal svnproto_vunpack() here?
         * Investigate it.
         */
        TRRET(SVNPROTO_EDITOR + 1);
    }
    return 0;
}

static int
delete_entry_cb(const char *path,
                struct dirent *de,
                UNUSED void *udata)
{
    int res = 0;
    char *fname;

    /*
     * XXX Check for modified and third-party files/dirs.
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

static int
open_root(svnc_ctx_t *ctx,
          bytestream_t *in)
{
    int res = 0;
    long rev = -1;
    svnproto_bytes_t *token = NULL;
    struct stat sb;

    if (svnproto_unpack(ctx, in, "((n?)s)", &rev, &token) != 0) {
        res = OPEN_ROOT + 1;
        goto END;
    }

    if (ctx->debug_level > 3) {
        LTRACE(1, "%s rev=%ld token=%s", cmd, rev, BDATA(token));
    }

    if (lstat(ctx->localroot, &sb) != 0) {
        if (mkdir(ctx->localroot, 0755) != 0) {
            res = OPEN_ROOT + 2;
            goto END;
        }
    } else {
        if (!S_ISDIR(sb.st_mode)) {
            res = OPEN_ROOT + 3;
            goto END;
        }
    }

END:
    if (token != NULL) {
        free(token);
    }

    TRRET(res);
}

static int
delete_entry(svnc_ctx_t *ctx,
             bytestream_t *in)
{
    int res = 0;
    svnproto_bytes_t *path = NULL;
    long rev = -1;
    svnproto_bytes_t *token = NULL;
    char *localpath = NULL;
    UNUSED struct stat sb;

    if (svnproto_unpack(ctx, in, "(s(n?)s)", &path, &rev, &token) != 0) {
        res = DELETE_ENTRY + 1;
        goto END;
    }

    if (ctx->debug_level > 3) {
        LTRACE(1, "%s path=%s rev=%ld token=%s",
               cmd, BDATA(path), rev, BDATA(token));
    }

    if ((localpath = path_join(ctx->localroot,
                               BDATA(path))) == NULL) {
        res = DELETE_ENTRY + 2;
        goto END;
    }

    if (traverse_dir(localpath, delete_entry_cb, NULL) != 0) {
        /* Is this a file in first place? */
        if (lstat(localpath, &sb) != 0) {
            if (ctx->debug_level > 1) {
                LTRACE(1, FGREEN("- %s -> %s"), BDATA(path), localpath);
            }

        } else {
            if (S_ISREG(sb.st_mode)) {

                if (unlink(localpath) != 0) {
                    perror("unlink");
                    res = DELETE_ENTRY + 3;
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
    if (path != NULL) {
        free(path);
    }
    if (token != NULL) {
        free(token);
    }
    if (localpath != NULL) {
        free(localpath);
    }
    TRRET(res);
}

static int
add_dir(svnc_ctx_t *ctx,
        bytestream_t *in)
{
    int res = 0;
    svnproto_bytes_t *path = NULL;
    svnproto_bytes_t *parent_token = NULL;
    svnproto_bytes_t *child_token = NULL;
    svnproto_bytes_t *copy_path = NULL;
    long copy_rev = -1;
    char *localpath = NULL;
    struct stat sb;

    /*
     * XXX support of copy-path/copy-rev
     */
    if (svnproto_unpack(ctx, in, "(sss(s?n?))",
                        &path, &parent_token, &child_token,
                        &copy_path, &copy_rev) != 0) {
        res = ADD_DIR + 1;
        goto END;
    }

    if (ctx->debug_level > 3) {
        LTRACE(1, "%s path=%s parent_token=%s child_token=%s "
               "copy_path=%s copy_rev=%ld",
               cmd, BDATA(path), BDATA(parent_token), BDATA(child_token),
               copy_path != NULL ? BDATA(copy_path) : NULL, copy_rev);
    }

    if ((localpath = path_join(ctx->localroot,
                               BDATA(path))) == NULL) {
        res = ADD_DIR + 2;
        goto END;
    }

    if (lstat(localpath, &sb) != 0) {
        if (mkdir(localpath, 0755) != 0) {
            res = ADD_DIR + 3;
            goto END;
        }
    } else {
        if (!S_ISDIR(sb.st_mode)) {
            res = ADD_DIR + 4;
            goto END;
        }
    }

    if (ctx->debug_level > 0) {
        LTRACE(1, FGREEN("+ %s -> %s"), BDATA(path), localpath);
    }
END:
    if (path != NULL) {
        free(path);
    }
    if (parent_token != NULL) {
        free(parent_token);
    }
    if (child_token != NULL) {
        free(child_token);
    }
    if (copy_path != NULL) {
        free(copy_path);
    }
    if (localpath != NULL) {
        free(localpath);
    }

    TRRET(res);
}

static int
open_dir(svnc_ctx_t *ctx,
         bytestream_t *in)
{
    int res = 0;
    svnproto_bytes_t *path = NULL;
    svnproto_bytes_t *parent_token = NULL;
    svnproto_bytes_t *child_token = NULL;
    long rev = -1;
    char *localpath = NULL;
    struct stat sb;

    if (svnproto_unpack(ctx, in, "(sss(n))",
            &path, &parent_token, &child_token, &rev) != 0) {
        res = OPEN_DIR + 1;
        goto END;
    }

    if (ctx->debug_level > 3) {
        LTRACE(1, "%s path=%s parent_token=%s child_token=%s rev=%ld",
               cmd, BDATA(path), BDATA(parent_token), BDATA(child_token), rev);
    }

    if ((localpath = path_join(ctx->localroot,
                               BDATA(path))) == NULL) {
        res = OPEN_DIR + 2;
        goto END;
    }

    if (lstat(localpath, &sb) != 0) {
        /*
         * we are being told to open dir, but it doesn't even
         * exist. Create it.
         */
        if (mkdir(localpath, 0755) != 0) {
            res = OPEN_DIR + 3;
            goto END;
        }
    } else {
        if (!S_ISDIR(sb.st_mode)) {
            res = OPEN_DIR + 4;
            goto END;
        }
    }

END:
    if (path != NULL) {
        free(path);
    }
    if (parent_token != NULL) {
        free(parent_token);
    }
    if (child_token != NULL) {
        free(child_token);
    }
    if (localpath != NULL) {
        free(localpath);
    }

    TRRET(res);
}

static int
change_dir_prop(svnc_ctx_t *ctx,
                bytestream_t *in)
{
    int res = 0;
    svnproto_bytes_t *token = NULL;
    svnproto_bytes_t *name = NULL;
    svnproto_bytes_t *value = NULL;

    if (svnproto_unpack(ctx, in, "(ss(s?))", &token, &name, &value) != 0) {
        res = CHANGE_DIR_PROP + 1;
        goto END;
    }

    if (ctx->debug_level > 3) {
        LTRACE(1, "[not implemented] %s token=%s name=%s value=%s",
               cmd, BDATA(token), BDATA(name),
               value != NULL ? BDATA(value) : NULL);
    }

END:
    if (token != NULL) {
        free(token);
    }
    if (name != NULL) {
        free(name);
    }
    if (value != NULL) {
        free(value);
    }

    TRRET(res);
}

static int
close_dir(svnc_ctx_t *ctx,
          bytestream_t *in)
{
    int res = 0;
    svnproto_bytes_t *token = NULL;

    if (svnproto_unpack(ctx, in, "(s)", &token) != 0) {
        res = CLOSE_DIR + 1;
        goto END;
    }

    if (ctx->debug_level > 3) {
        LTRACE(1, "[not implemented] %s token=%s", cmd, BDATA(token));
    }

END:
    if (token != NULL) {
        free(token);
    }

    TRRET(res);
}

static int
absent_dir(svnc_ctx_t *ctx,
           bytestream_t *in)
{
    int res = 0;
    svnproto_bytes_t *path = NULL;
    svnproto_bytes_t *parent_token = NULL;

    if (svnproto_unpack(ctx, in, "(ss)", &path, &parent_token) != 0) {
        res = ABSENT_DIR + 1;
        goto END;
    }

    if (ctx->debug_level > 3) {
        LTRACE(1, "[not implemented] %s path=%s parent_token=%s",
               cmd, BDATA(path), BDATA(parent_token));
    }

END:
    if (path != NULL) {
        free(path);
    }
    if (parent_token != NULL) {
        free(parent_token);
    }

    TRRET(res);
}

static int
add_file(svnc_ctx_t *ctx,
         bytestream_t *in)
{
    int res = 0;
    svnproto_bytes_t *dir_token = NULL;
    svnproto_bytes_t *copy_path = NULL;
    long copy_rev = -1;

    /*
     * XXX support of copy-path
     */
    if (svndiff_doc_init(&doc) != 0) {
        res = ADD_FILE + 1;
        goto END;
    }

    if (svnproto_unpack(ctx, in, "(sss(s?n?))",
                        &doc.rp, &dir_token,
                        &doc.ft,
                        &copy_path, &copy_rev) != 0) {
        res = ADD_FILE + 2;
        goto END;
    }

    if (ctx->debug_level > 3) {
        LTRACE(1, "%s path=%s dir_token=%s file_token=%s copy_path=%s "
               "copy_rev=%ld",
               cmd, BDATA(doc.rp), BDATA(dir_token),
               BDATA(doc.ft),
               BDATA(copy_path), copy_rev);
    }

    if ((doc.lp = path_join(ctx->localroot,
                               BDATA(doc.rp))) == NULL) {
        res = ADD_FILE + 3;
        goto END;
    }

    if (lstat(doc.lp, &doc.sb) == 0) {
        if (!S_ISREG(doc.sb.st_mode)) {
            /* we are not yet handling it */
            res = ADD_FILE + 4;
            goto END;
        }
        if (unlink(doc.lp) != 0) {
            FAIL("unlink");
        }
    }

END:
    if (dir_token != NULL) {
        free(dir_token);
    }
    if (copy_path != NULL) {
        free(copy_path);
    }

    TRRET(res);
}

static int
create_file(svndiff_doc_t *doc, svnproto_fileent_t *fe)
{
    int res = 0;
    svnproto_bytes_t **s;
    array_iter_t it;
    ssize_t total_len = 0;
    int fd = -1;

    if ((fd = open(doc->lp,
                        O_RDWR|O_CREAT|O_TRUNC|O_NOFOLLOW,
                        doc->mod)) < 0) {
        perror("open");
        TRACE(FRED("Failed to open %s"), doc->lp);
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
create_symlink(svndiff_doc_t *doc, svnproto_fileent_t *fe)
{
    int res = 0;
    svnproto_bytes_t **s;
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
checkout_file(svndiff_doc_t *doc, long rev)
{
    int res = 0;
    svnproto_fileent_t fe;
    svnproto_prop_t *prop;
    array_iter_t it;

    svnproto_fileent_init(&fe);

    if (shadow_ctx->debug_level > 1) {
        LTRACE(1, FYELLOW("! %s,%ld ->%s"), BDATA(doc->rp), rev, doc->lp);
    }

    if (svnproto_get_file(shadow_ctx,
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

        if (strcmp(BDATA(prop->name), "svn:executable") == 0) {
            doc->mod = 0755;
            doc->flags |= SD_FLAG_MOD_SET;
        }
        if (strcmp(BDATA(prop->name), "svn:special") == 0) {
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
    svnproto_fileent_fini(&fe);
    TRRET(res);
}

static int
open_file(svnc_ctx_t *ctx,
          bytestream_t *in)
{
    int res = 0;
    svnproto_bytes_t *dir_token = NULL;

    if (svndiff_doc_init(&doc) != 0) {
        res = OPEN_FILE + 1;
        goto END;
    }

    if (svnproto_unpack(ctx, in, "(sss(n))", &doc.rp,
                        &dir_token, &doc.ft, &doc.rev) != 0) {
        res = OPEN_FILE + 2;
        goto END;
    }

    if (ctx->debug_level > 3) {
        LTRACE(1, "%s path=%s dir_token=%s file_token=%s rev=%ld",
               cmd, BDATA(doc.rp), BDATA(dir_token),
               BDATA(doc.ft), doc.rev);
    }

    if ((doc.lp = path_join(ctx->localroot,
                               BDATA(doc.rp))) == NULL) {
        res = OPEN_FILE + 3;
        goto END;
    }

    /* make sure the file exists locally */
    if (lstat(doc.lp, &doc.sb) == 0) {
        if (!S_ISREG(doc.sb.st_mode) && !S_ISLNK(doc.sb.st_mode)) {
            /* we are not yet handling it */
            res = OPEN_FILE + 4;
            if (ctx->debug_level > 0) {
                LTRACE(1, FRED("%s Not a file: path=%s dir_token=%s "
                       "file_token=%s rev=%ld"),
                       cmd, BDATA(doc.rp), BDATA(dir_token),
                       BDATA(doc.ft), doc.rev);
            }
            goto END;
        }
        /* The file exists. */
    } else {
        if (checkout_file(&doc, doc.rev) != 0) {
            res = OPEN_FILE + 5;
            goto END;
        }
    }

END:
    if (dir_token != NULL) {
        free(dir_token);
    }

    TRRET(res);
}

static int
apply_textdelta(svnc_ctx_t *ctx,
                bytestream_t *in)
{
    int res = 0;
    svnproto_bytes_t *file_token = NULL;

    if (svnproto_unpack(ctx, in, "(s(s?))",
                        &file_token,
                        &doc.base_checksum) != 0) {
        res = APPLY_TEXTDELTA + 1;
        goto END;
    }

    if (ctx->debug_level > 3) {
        LTRACE(1, "%s file_token=%s base_checksum=%s",
               cmd, BDATA(file_token),
               BDATA(doc.base_checksum));
    }

END:
    if (file_token != NULL) {
        free(file_token);
    }

    TRRET(res);
}

static int
textdelta_chunk(svnc_ctx_t *ctx,
                bytestream_t *in)
{
    int res = 0;
    svnproto_bytes_t *file_token = NULL;

    if (svnproto_unpack(ctx, in, "(sS)", &file_token,
                        chunk_cb, &doc) != 0) {
        res = TEXTDELTA_CHUNK + 1;
        goto END;
    }

    if (ctx->debug_level > 3) {
        LTRACE(1, "%s file_token=%s", cmd, BDATA(file_token));
    }

END:
    if (file_token != NULL) {
        free(file_token);
    }

    TRRET(res);
}

static int
textdelta_end(svnc_ctx_t *ctx,
              bytestream_t *in)
{
    int res = 0;
    svnproto_bytes_t *file_token = NULL;

    if (svnproto_unpack(ctx, in, "(s)", &file_token) != 0) {
        res = TEXTDELTA_END + 1;
        goto END;
    }

    if (ctx->debug_level > 3) {
        LTRACE(1, "[not implemented] %s file_token=%s", cmd, BDATA(file_token));
    }

END:
    if (file_token != NULL) {
        free(file_token);
    }

    TRRET(res);
}

static int
change_file_prop(svnc_ctx_t *ctx,
                 bytestream_t *in)
{
    int res = 0;
    svnproto_bytes_t *file_token = NULL;
    svnproto_bytes_t *name = NULL;
    svnproto_bytes_t *value = NULL;

    if (svnproto_unpack(ctx, in, "(ss(s?))",
                        &file_token, &name, &value) != 0) {
        res = CHANGE_FILE_PROP + 1;
        goto END;
    }

    if (ctx->debug_level > 3) {
        LTRACE(1, "%s token=%s name=%s value=%s",
               cmd, BDATA(file_token), BDATA(name),
               BDATA(value));
    }

    if (strcmp(BDATA(name), "svn:executable") == 0) {
        doc.mod = 0755;
        doc.flags |= SD_FLAG_MOD_SET;
    }

    if (strcmp(BDATA(name), "svn:special") == 0) {
        doc.flags |= SD_FLAG_SYMLINK_SET;
    }

END:
    if (file_token != NULL) {
        free(file_token);
    }
    if (name != NULL) {
        free(name);
    }
    if (value != NULL) {
        free(value);
    }

    TRRET(res);
}

static int
checksum_cb(svndiff_wnd_t *wnd, MD5_CTX *ctx)
{
    MD5Update(ctx, wnd->tview, wnd->tview_len);
    return 0;
}

static int
close_file(svnc_ctx_t *ctx,
           bytestream_t *in)
{
    int res = 0;
    svnproto_bytes_t *file_token = NULL;
    svnproto_bytes_t *text_checksum = NULL;
    svndiff_wnd_t *wnd;
    array_iter_t it;
    MD5_CTX mctx;
    char *checksum = NULL;
    ssize_t total_len;

    if (svnproto_unpack(ctx, in, "(s(s?))",
                        &file_token, &text_checksum) != 0) {
        res = CLOSE_FILE + 1;
        goto END;
    }

    if (ctx->debug_level > 3) {
        LTRACE(1, "%s file_token=%s text_checksum=%s",
               cmd, BDATA(file_token), BDATA(text_checksum));
    }

    if (BDATA(file_token) && BDATA(doc.ft)) {
        if (strcmp(file_token->data,
                   doc.ft->data) != 0) {
            res = CLOSE_FILE + 2;
            goto END;
        }
    }

    assert(doc.fd == -1);

    /* verify regular open-file (aka source view) */
    if ((doc.fd = open(doc.lp, O_RDWR|O_NOFOLLOW)) >= 0) {

        if (doc.base_checksum != NULL) {

            if (svnproto_editor_verify_checksum(doc.fd,
                    doc.base_checksum) != 0) {

                /* check it out clean? */
                if (ctx->debug_level > 2) {
                    LTRACE(1, FRED("Base checksum mismtach: expected %s over %s"),
                           BDATA(doc.base_checksum), doc.lp);
                }

                close(doc.fd);
                doc.fd = -1;

                if (checkout_file(&doc, target_rev) != 0) {
                    res = CLOSE_FILE + 3;
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
         * was reg/symlink add-file or symlink open-file.
         *
         * symlink open-files should never have instructions other than
         * new, so it's safe to pass -1 as fd to svndiff_build_tview().
         */
    }

    /* build target view */
    if (array_traverse(&doc.wnd,
                      (array_traverser_t)svndiff_build_tview,
                      &doc) != 0)  {

        res = CLOSE_FILE + 4;
        goto END;
    }

    /* verify target view */
    if (BDATA(text_checksum) != NULL) {

        MD5Init(&mctx);
        array_traverse(&doc.wnd, (array_traverser_t)checksum_cb, &mctx);

        if ((checksum = MD5End(&mctx, NULL)) == NULL) {
            perror("ND5End");
            res = CLOSE_FILE + 5;
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

            if (checkout_file(&doc, target_rev) != 0) {
                res = CLOSE_FILE + 6;
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
                    res = CLOSE_FILE + 1;
                    goto END;
                }
                /* strlen("link ") */
                *(data + 4) = '0';
                data += 5;

                if (symlink(data, doc.lp) < 0) {
                    perror("symlink");
                    TRACE(FRED("Failed to symlink %s to %s"), data, doc.lp);
                    res = CLOSE_FILE + 2;
                    goto END;
                }

            } else {
                /* nothing to change */
            }
            goto EDIT_COMPLETE;

        } else {
            /* regular add-file or symlink open-file, try open it */
            if ((doc.fd = open(doc.lp, O_RDWR|O_CREAT|O_NOFOLLOW, doc.mod)) < 0) {
                /* symlink open-file */
                if ((wnd = array_first(&doc.wnd, &it)) != NULL) {
                    ssize_t nread;
                    char olddata[1024];
                    char *data;

                    if ((nread = readlink(doc.lp, olddata, sizeof(olddata))) < 0) {
                        FAIL("readlink");
                    }
                    olddata[nread] = '\0';

                    data = wnd->tview;
                    *(data + wnd->tview_len) = '\0';
                    if (strstr(data, "link ") == NULL) {
                        res = CLOSE_FILE + 1;
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
                        TRACE(FRED("Failed to symlink %s to %s"), data, doc.lp);
                        res = CLOSE_FILE + 2;
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

    if (fchmod(doc.fd, doc.mod) != 0) {
        FAIL("fchmod");
    }

EDIT_COMPLETE:
    if (svnc_save_checksum(ctx, BDATA(doc.rp), text_checksum) != 0) {
        res = CLOSE_FILE + 8;
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
    if (file_token != NULL) {
        free(file_token);
    }
    if (text_checksum != NULL) {
        free(text_checksum);
    }

    TRRET(res);
}

static int
absent_file(svnc_ctx_t *ctx,
            bytestream_t *in)
{
    int res = 0;
    svnproto_bytes_t *path = NULL;
    svnproto_bytes_t *parent_token = NULL;

    if (svnproto_unpack(ctx, in, "(ss)", &path, &parent_token) != 0) {
        res = ABSENT_FILE + 1;
        goto END;
    }

    if (ctx->debug_level > 3) {
        LTRACE(1, "[not implemented] %s path=%s parent_token=%s",
               cmd, BDATA(path), BDATA(parent_token));
    }

END:
    if (path != NULL) {
        free(path);
    }
    if (parent_token != NULL) {
        free(parent_token);
    }

    TRRET(res);
}

static int
editor_cb2(svnc_ctx_t *ctx,
           bytestream_t *in,
           UNUSED svnproto_state_t *st,
           UNUSED void *udata)
{
    int res = 0;

    if (strcmp(cmd, "target-rev") == 0) {

        if (svnproto_unpack(ctx, in, "(n)", &target_rev) != 0) {
            res = SVNPROTO_EDITOR + 2;
            goto END;
        }

        if (ctx->debug_level > 3) {
            LTRACE(1, "%s rev=%ld", cmd, target_rev);
        }

    } else if (strcmp(cmd, "open-root") == 0) {
        if (open_root(ctx, in) != 0) {
            res = SVNPROTO_EDITOR + 3;
            goto END;
        }

    } else if (strcmp(cmd, "delete-entry") == 0) {
        if (delete_entry(ctx, in) != 0) {
            res = SVNPROTO_EDITOR + 4;
            goto END;
        }

    } else if (strcmp(cmd, "add-dir") == 0) {
        if (add_dir(ctx, in) != 0) {
            res = SVNPROTO_EDITOR + 5;
            goto END;
        }

    } else if (strcmp(cmd, "open-dir") == 0) {
        if (open_dir(ctx, in) != 0) {
            res = SVNPROTO_EDITOR + 6;
            goto END;
        }

    } else if (strcmp(cmd, "change-dir-prop") == 0) {
        if (change_dir_prop(ctx, in) != 0) {
            res = SVNPROTO_EDITOR + 7;
            goto END;
        }

    } else if (strcmp(cmd, "close-dir") == 0) {
        if (close_dir(ctx, in) != 0) {
            res = SVNPROTO_EDITOR + 8;
            goto END;
        }

    } else if (strcmp(cmd, "absent-dir") == 0) {
        if (absent_dir(ctx, in) != 0) {
            res = SVNPROTO_EDITOR + 9;
            goto END;
        }

    } else if (strcmp(cmd, "add-file") == 0) {
        if (add_file(ctx, in) != 0) {
            res = SVNPROTO_EDITOR + 10;
            goto END;
        }

    } else if (strcmp(cmd, "open-file") == 0) {
        if (open_file(ctx, in) != 0) {
            res = SVNPROTO_EDITOR + 11;
            goto END;
        }

    } else if (strcmp(cmd, "apply-textdelta") == 0) {
        if (apply_textdelta(ctx, in) != 0) {
            res = SVNPROTO_EDITOR + 12;
            goto END;
        }

    } else if (strcmp(cmd, "textdelta-chunk") == 0) {
        if (textdelta_chunk(ctx, in) != 0) {
            res = SVNPROTO_EDITOR + 13;
            goto END;
        }

    } else if (strcmp(cmd, "textdelta-end") == 0) {
        if (textdelta_end(ctx, in) != 0) {
            res = SVNPROTO_EDITOR + 14;
            goto END;
        }

    } else if (strcmp(cmd, "change-file-prop") == 0) {
        if (change_file_prop(ctx, in) != 0) {
            res = SVNPROTO_EDITOR + 15;
            goto END;
        }

    } else if (strcmp(cmd, "close-file") == 0) {
        if (close_file(ctx, in) != 0) {
            res = SVNPROTO_EDITOR + 16;
            goto END;
        }

    } else if (strcmp(cmd, "absent-file") == 0) {
        if (absent_file(ctx, in) != 0) {
            res = SVNPROTO_EDITOR + 17;
            goto END;
        }

    } else if (strcmp(cmd, "close-edit") == 0) {
        if (svnproto_unpack(ctx, in, "()") != 0) {
            res = SVNPROTO_EDITOR + 18;
            goto END;
        }

        if (ctx->debug_level > 3) {
            LTRACE(1, "%s", cmd);
        }
        flags |= SVNPE_CLOSING;

    } else if (strcmp(cmd, "abort-edit") == 0) {
        if (svnproto_unpack(ctx, in, "()") != 0) {
            res = SVNPROTO_EDITOR + 19;
            goto END;
        }

        if (ctx->debug_level > 3) {
            LTRACE(1, "[not implemented] %s", cmd);
        }
        flags |= SVNPE_CLOSING;

    } else if (strcmp(cmd, "finish-replay") == 0) {
        if (svnproto_unpack(ctx, in, "()") != 0) {
            res = SVNPROTO_EDITOR + 20;
            goto END;
        }

        if (ctx->debug_level > 3) {
            LTRACE(1, "[not implemented] %s", cmd);
        }
        flags |= SVNPE_CLOSING;


    } else if (strcmp(cmd, "failure") == 0) {
        svnc_clear_last_error(ctx);
        if (svnproto_unpack(ctx, in, "((nssn))",
                              &ctx->last_error.apr_error,
                              &ctx->last_error.message,
                              &ctx->last_error.file,
                              &ctx->last_error.line) != 0) {
            res = SVNPROTO_EDITOR + 21;
            goto END;
        }

        //TRACE(FRED("ERROR: %s"), BDATA(ctx->last_error.message));

        res = PARSE_EOD;
        goto END;


    } else {
        TRACE(FRED("unknown: %s"), cmd);
        res = SVNPROTO_EDITOR + 22;
        goto END;
    }

END:
    if (cmd != NULL) {
        free(cmd);
        cmd = NULL;
    }

    TRRET(res);
}

UNUSED static int
editor_cb1(svnc_ctx_t *ctx,
           bytestream_t *in,
           UNUSED svnproto_state_t *st,
           UNUSED void *udata)
{
    if ((svnproto_unpack(ctx, in, "w", &cmd)) != 0) {
        TRRET(SVNPROTO_EDITOR + 23);
    }
    return 0;

}

static int
editor_cb0(svnc_ctx_t *ctx,
           bytestream_t *in,
           UNUSED svnproto_state_t *st,
           void *udata)
{
    int res = 0;

    if (flags && SVNPE_CLOSING) {
        res = PARSE_EOD;

    } else {
        res = svnproto_unpack(ctx, in, "(wr)",
                              &cmd,
                              editor_cb2, udata);
    }

    TRRET(res);
}


int
svnproto_editor(svnc_ctx_t *ctx)
{
    /* editor starts with check auth */
    if (svnproto_check_auth(ctx) != 0) {
        TRRET(SVNPROTO_EDITOR + 24);
    }

    cmd = NULL;

    flags = 0;

    if ((shadow_ctx = svnc_new(ctx->url,
                               ctx->localroot,
                               SVNC_NNOCACHE,
                               ctx->debug_level)) == NULL) {
        TRRET(SVNPROTO_EDITOR + 25);
    }

    if (svnc_connect(shadow_ctx) != 0) {
        TRRET(SVNPROTO_EDITOR + 26);
    }

    if (svnproto_unpack(ctx, &ctx->in, "r*", editor_cb0, &doc) != 0) {
        TRRET(SVNPROTO_EDITOR + 27);
    }

    //TRACE("consumed %ld", ctx->in.pos - saved_pos);

    if (cmd != NULL) {
        free(cmd);
        cmd = NULL;
    }

    svnc_close(shadow_ctx);
    svnc_destroy(shadow_ctx);
    free(shadow_ctx);

    return 0;
}

