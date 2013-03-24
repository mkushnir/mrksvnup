#include <fcntl.h>
/* goes before md5.h */
#include <sys/types.h>
#include <md5.h>
#include <string.h>
#include <sys/param.h>
#include <sys/stat.h>
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

static char *cmd = NULL;

int
verify_checksum(int fd, const char *cs)
{
    int res = 0;
    char buf[VCBUFSZ];
    ssize_t nread;
    MD5_CTX ctx;
    char *s = NULL;

    MD5Init(&ctx);

    if (lseek(fd, 0, SEEK_SET) != 0) {
        perror("lseek");
        res = VERIFY_CHECKSUM + 1;
        goto END;
    }

    while ((nread = read(fd, buf, VCBUFSZ)) > 0) {
        MD5Update(&ctx, buf, (unsigned int)nread);
    }

    if (nread < 0) {
        perror("read");
        res = VERIFY_CHECKSUM + 2;
        goto END;
    }

    if ((s = MD5End(&ctx, NULL)) == NULL) {
        perror("ND5End");
        res = VERIFY_CHECKSUM + 3;
        goto END;
    }

    //TRACE("s=%s cs=%s", s, cs);

    if (strcmp(s, cs) != 0) {
        res = VERIFY_CHECKSUM + 4;
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
         void *udata)
{
    svndiff_doc_t *doc = udata;

    /* zero or one quantification, always return OK */

    if (svndiff_parse_doc(SDATA(in, st->r.start),
                          SDATA(in, st->r.end),
                          doc) != 0) {

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

        LTRACE(1, FGREEN("- %s"), fname);
        free(fname);

    } else {
        //TRACE("deleting dir: %s", path);

        if (rmdir(path) != 0) {
            perror("rmdir");
            res = 2;
        }
        LTRACE(1, FGREEN("- %s"), path);

    }
    return res;
}

static int
open_root(svnc_ctx_t *ctx,
          bytestream_t *in,
          UNUSED svndiff_doc_t *doc)
{
    int res = 0;
    long rev = -1;
    svnproto_bytes_t *token = NULL;
    struct stat sb;

    if (svnproto_unpack(ctx, in, "((n?)s)", &rev, &token) != 0) {
        res = OPEN_ROOT + 1;
        goto END;
    }

    //TRACE("%s rev=%ld token=%s", cmd, rev, BDATA(token));

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
             bytestream_t *in,
             UNUSED svndiff_doc_t *doc)
{
    int res = 0;
    svnproto_bytes_t *path = NULL;
    long rev = -1;
    svnproto_bytes_t *token = NULL;
    char *localpath = NULL;
    UNUSED struct stat sb;

    if (svnproto_unpack(ctx, in, "(s(n)s)", &path, &rev, &token) != 0) {
        res = DELETE_ENTRY + 1;
        goto END;
    }

    //TRACE("%s path=%s rev=%ld token=%s",
    //      cmd, BDATA(path), rev, BDATA(token));

    if ((localpath = path_join(ctx->localroot,
                               BDATA(path))) == NULL) {
        res = DELETE_ENTRY + 2;
        goto END;
    }

    if (traverse_dir(localpath, delete_entry_cb, NULL) != 0) {
        /* Is this a file in first place? */
        if (lstat(localpath, &sb) != 0) {
            TRACE(FRED("Failed to fully delete %s"), localpath);

        } else {
            if (S_ISREG(sb.st_mode)) {

                LTRACE(1, FGREEN("- %s"), localpath);

                if (unlink(localpath) != 0) {
                    perror("unlink");
                    res = DELETE_ENTRY + 3;
                    goto END;
                }

            } else {
                TRACE("Failed to fully delete %s", localpath);
            }
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
        bytestream_t *in,
        UNUSED svndiff_doc_t *doc)
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

    //TRACE("%s path=%s parent_token=%s child_token=%s "
    //      "copy_path=%s copy_rev=%ld",
    //      cmd, BDATA(path), BDATA(parent_token), BDATA(child_token),
    //      copy_path != NULL ? BDATA(copy_path) : NULL, copy_rev);


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
         bytestream_t *in,
         UNUSED svndiff_doc_t *doc)
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

    //TRACE("%s path=%s parent_token=%s child_token=%s rev=%ld",
    //    cmd, BDATA(path), BDATA(parent_token), BDATAchild_token), rev);

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
                bytestream_t *in,
                UNUSED svndiff_doc_t *doc)
{
    int res = 0;
    svnproto_bytes_t *token = NULL;
    svnproto_bytes_t *name = NULL;
    svnproto_bytes_t *value = NULL;

    if (svnproto_unpack(ctx, in, "(ss(s?))", &token, &name, &value) != 0) {
        res = CHANGE_DIR_PROP + 1;
        goto END;
    }

    //TRACE("%s token=%s name=%s value=%s",
    //      cmd, BDATA(token), BDATA(name),
    //      value != NULL ? BDATA(value) : NULL);

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
          bytestream_t *in,
          UNUSED svndiff_doc_t *doc)
{
    int res = 0;
    svnproto_bytes_t *token = NULL;

    if (svnproto_unpack(ctx, in, "(s)", &token) != 0) {
        res = CLOSE_DIR + 1;
        goto END;
    }

    //TRACE("%s token=%s", cmd, BDATA(token));

END:
    if (token != NULL) {
        free(token);
    }

    TRRET(res);
}

static int
absent_dir(svnc_ctx_t *ctx,
           bytestream_t *in,
           UNUSED svndiff_doc_t *doc)
{
    int res = 0;
    svnproto_bytes_t *path = NULL;
    svnproto_bytes_t *parent_token = NULL;

    if (svnproto_unpack(ctx, in, "(ss)", &path, &parent_token) != 0) {
        res = ABSENT_DIR + 1;
        goto END;
    }

    //TRACE("%s path=%s parent_token=%s",
    //      cmd, BDATA(path), BDATA(parent_token));

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
         bytestream_t *in,
         UNUSED svndiff_doc_t *doc)
{
    int res = 0;
    svnproto_bytes_t *dir_token = NULL;
    svnproto_bytes_t *copy_path = NULL;
    long copy_rev = -1;
    struct stat sb;

    /*
     * XXX support of copy-path
     */
    if (svndiff_doc_init(doc) != 0) {
        res = ADD_FILE + 1;
        goto END;
    }

    if (svnproto_unpack(ctx, in, "(sss(s?n?))",
                        &doc->rp, &dir_token,
                        &doc->ft,
                        &copy_path, &copy_rev) != 0) {
        res = ADD_FILE + 2;
        goto END;
    }

    //TRACE("%s path=%s dir_token=%s file_token=%s copy_path=%s "
    //      "copy_rev=%ld",
    //      cmd, BDATA(doc->rp), BDATA(dir_token),
    //      BDATA(doc->ft),
    //      BDATA(copy_path), copy_rev);


    if ((doc->lp = path_join(ctx->localroot,
                               BDATA(doc->rp))) == NULL) {
        res = ADD_FILE + 3;
        goto END;
    }

    if (lstat(doc->lp, &sb) == 0) {
        if (!S_ISREG(sb.st_mode)) {
            res = ADD_FILE + 4;
            goto END;
        }
        /* The file exists. If it's not empty, will skip it. */
    }

    if ((doc->fd =
         open(doc->lp, O_RDWR|O_CREAT, 0644)) < 0) {
         //open(doc->lp, O_RDWR|O_CREAT|O_TRUNC, 0644)) < 0) {

        res = ADD_FILE + 5;
        goto END;
    }
    if (fchmod(doc->fd, 0644) != 0) {
        res = ADD_FILE + 6;
        goto END;
    }
    lseek(doc->fd, 0, SEEK_SET);

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
open_file(svnc_ctx_t *ctx,
          bytestream_t *in,
          UNUSED svndiff_doc_t *doc)
{
    int res = 0;
    svnproto_bytes_t *dir_token = NULL;
    struct stat sb;

    if (svndiff_doc_init(doc) != 0) {
        res = OPEN_FILE + 1;
        goto END;
    }

    if (svnproto_unpack(ctx, in, "(sss(n))",
                        &doc->rp,
                        &dir_token,
                        &doc->ft,
                        &doc->rev) != 0) {
        res = OPEN_FILE + 2;
        goto END;
    }

    //TRACE("%s path=%s dir_token=%s file_token=%s rev=%ld",
    //      cmd, BDATA(doc->rp), BDATA(dir_token),
    //      BDATA(doc->ft), doc->rev);

    if ((doc->lp = path_join(ctx->localroot,
                               BDATA(doc->rp))) == NULL) {
        res = OPEN_FILE + 3;
        goto END;
    }

    if (lstat(doc->lp, &sb) == 0) {
        if (!S_ISREG(sb.st_mode)) {
            res = OPEN_FILE + 4;
            goto END;
        }
        /* The file exists. */
    } else {
        /* tell the svndiff that it needs checking out */
    }

    if ((doc->fd =
         open(doc->lp, O_RDWR|O_CREAT, 0644)) < 0) {

        res = OPEN_FILE + 5;
        goto END;
    }
    if (fchmod(doc->fd, 0644) != 0) {
        res = OPEN_FILE + 6;
        goto END;
    }

END:
    if (dir_token != NULL) {
        free(dir_token);
    }

    TRRET(res);
}

static int
apply_textdelta(svnc_ctx_t *ctx,
                bytestream_t *in,
                UNUSED svndiff_doc_t *doc)
{
    int res = 0;
    svnproto_bytes_t *file_token = NULL;

    if (svnproto_unpack(ctx, in, "(s(s?))",
                        &file_token,
                        &doc->base_checksum) != 0) {
        res = APPLY_TEXTDELTA + 1;
        goto END;
    }

    //TRACE("%s file_token=%s base_checksum=%s", cmd, BDATA(file_token),
    //      BDATA(doc->base_checksum),
    //      NULL);

END:
    if (file_token != NULL) {
        free(file_token);
    }

    TRRET(res);
}

static int
textdelta_chunk(svnc_ctx_t *ctx,
                bytestream_t *in,
                UNUSED svndiff_doc_t *doc)
{
    int res = 0;
    svnproto_bytes_t *file_token = NULL;

    if (svnproto_unpack(ctx, in, "(sS)", &file_token,
                        chunk_cb, doc) != 0) {
        res = TEXTDELTA_CHUNK + 1;
        goto END;
    }

    //TRACE("%s file_token=%s", cmd, BDATA(file_token));

END:
    if (file_token != NULL) {
        free(file_token);
    }

    TRRET(res);
}

static int
textdelta_end(svnc_ctx_t *ctx,
              bytestream_t *in,
              UNUSED svndiff_doc_t *doc)
{
    int res = 0;
    svnproto_bytes_t *file_token = NULL;

    if (svnproto_unpack(ctx, in, "(s)", &file_token) != 0) {
        res = TEXTDELTA_END + 1;
        goto END;
    }

    //TRACE("%s file_token=%s", cmd, BDATA(file_token));

END:
    if (file_token != NULL) {
        free(file_token);
    }

    TRRET(res);
}

static int
change_file_prop(svnc_ctx_t *ctx,
                 bytestream_t *in,
                 UNUSED svndiff_doc_t *doc)
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

    //TRACE("%s token=%s name=%s value=%s",
    //      cmd, BDATA(file_token), BDATA(name),
    //      BDATA(value));

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
close_file(svnc_ctx_t *ctx,
           bytestream_t *in,
           UNUSED svndiff_doc_t *doc)
{
    int res = 0;
    svnproto_bytes_t *file_token = NULL;
    svnproto_bytes_t *text_checksum = NULL;

    if (svnproto_unpack(ctx, in, "(s(s?))",
                        &file_token, &text_checksum) != 0) {
        res = CLOSE_FILE + 1;
        goto END;
    }

    //TRACE("%s file_token=%s text_checksum=%s",
    //      cmd, BDATA(file_token),
    //      BDATA(text_checksum));

    //svndiff_doc_dump(doc);

    if (BDATA(file_token) && BDATA(doc->ft)) {
        if (strcmp(file_token->data,
                   doc->ft->data) != 0) {
            res = CLOSE_FILE + 2;
            goto END;
        }
    }

    if (svndiff_doc_apply(doc) != 0) {
        LTRACE(1, FRED("C %s -> %s"), BDATA(doc->rp), doc->lp);
    } else {
        if (BDATA(doc->base_checksum) == NULL) {
            LTRACE(1, FGREEN("+ %s -> %s"), BDATA(doc->rp), doc->lp);
        } else {
            LTRACE(1, FGREEN("~ %s -> %s"), BDATA(doc->rp), doc->lp);
        }
    }

    if ((BDATA(doc->base_checksum) != NULL) &&
        BDATA(text_checksum) != NULL &&
        (verify_checksum(doc->fd, BDATA(text_checksum)) != 0)) {

        TRACE(FRED("checksum mismtach: expected %s over %s. Will ignore this file."),
              BDATA(text_checksum), doc->lp);
        //svndiff_doc_dump(doc);
    }

END:
    if (svndiff_doc_fini(doc) != 0) {
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
            bytestream_t *in,
            UNUSED svndiff_doc_t *doc)
{
    int res = 0;
    svnproto_bytes_t *path = NULL;
    svnproto_bytes_t *parent_token = NULL;

    if (svnproto_unpack(ctx, in, "(ss)", &path, &parent_token) != 0) {
        res = ABSENT_FILE + 1;
        goto END;
    }

    TRACE("%s path=%s parent_token=%s",
          cmd, BDATA(path), BDATA(parent_token));

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
           void *udata)
{
    int res = 0;
    svndiff_doc_t *doc = udata;

    if (strcmp(cmd, "target-rev") == 0) {
        long rev = -1;

        if (svnproto_unpack(ctx, in, "(n)", &rev) != 0) {
            res = SVNPROTO_EDITOR + 2;
            goto END;
        }
        //TRACE("%s rev=%ld", cmd, rev);

    } else if (strcmp(cmd, "open-root") == 0) {
        if (open_root(ctx, in, doc) != 0) {
            res = SVNPROTO_EDITOR + 3;
            goto END;
        }

    } else if (strcmp(cmd, "delete-entry") == 0) {
        if (delete_entry(ctx, in, doc) != 0) {
            res = SVNPROTO_EDITOR + 4;
            goto END;
        }

    } else if (strcmp(cmd, "add-dir") == 0) {
        if (add_dir(ctx, in, doc) != 0) {
            res = SVNPROTO_EDITOR + 5;
            goto END;
        }

    } else if (strcmp(cmd, "open-dir") == 0) {
        if (open_dir(ctx, in, doc) != 0) {
            res = SVNPROTO_EDITOR + 6;
            goto END;
        }

    } else if (strcmp(cmd, "change-dir-prop") == 0) {
        if (change_dir_prop(ctx, in, doc) != 0) {
            res = SVNPROTO_EDITOR + 7;
            goto END;
        }

    } else if (strcmp(cmd, "close-dir") == 0) {
        if (close_dir(ctx, in, doc) != 0) {
            res = SVNPROTO_EDITOR + 8;
            goto END;
        }

    } else if (strcmp(cmd, "absent-dir") == 0) {
        if (absent_dir(ctx, in, doc) != 0) {
            res = SVNPROTO_EDITOR + 9;
            goto END;
        }

    } else if (strcmp(cmd, "add-file") == 0) {
        if (add_file(ctx, in, doc) != 0) {
            res = SVNPROTO_EDITOR + 10;
            goto END;
        }

    } else if (strcmp(cmd, "open-file") == 0) {
        if (open_file(ctx, in, doc) != 0) {
            res = SVNPROTO_EDITOR + 11;
            goto END;
        }

    } else if (strcmp(cmd, "apply-textdelta") == 0) {
        if (apply_textdelta(ctx, in, doc) != 0) {
            res = SVNPROTO_EDITOR + 12;
            goto END;
        }

    } else if (strcmp(cmd, "textdelta-chunk") == 0) {
        if (textdelta_chunk(ctx, in, doc) != 0) {
            res = SVNPROTO_EDITOR + 13;
            goto END;
        }

    } else if (strcmp(cmd, "textdelta-end") == 0) {
        if (textdelta_end(ctx, in, doc) != 0) {
            res = SVNPROTO_EDITOR + 14;
            goto END;
        }

    } else if (strcmp(cmd, "change-file-prop") == 0) {
        if (change_file_prop(ctx, in, doc) != 0) {
            res = SVNPROTO_EDITOR + 15;
            goto END;
        }

    } else if (strcmp(cmd, "close-file") == 0) {
        if (close_file(ctx, in, doc) != 0) {
            res = SVNPROTO_EDITOR + 16;
            goto END;
        }

    } else if (strcmp(cmd, "absent-file") == 0) {
        if (absent_file(ctx, in, doc) != 0) {
            res = SVNPROTO_EDITOR + 17;
            goto END;
        }

    } else if (strcmp(cmd, "close-edit") == 0) {
        if (svnproto_unpack(ctx, in, "()") != 0) {
            res = SVNPROTO_EDITOR + 18;
            goto END;
        }

        //TRACE("%s END", cmd);
        res = PARSE_EOD;
        goto END;

    } else if (strcmp(cmd, "abort-edit") == 0) {
        if (svnproto_unpack(ctx, in, "()") != 0) {
            res = SVNPROTO_EDITOR + 19;
            goto END;
        }

        //TRACE("%s END", cmd);
        res = PARSE_EOD;
        goto END;

    } else if (strcmp(cmd, "finish-replay") == 0) {
        if (svnproto_unpack(ctx, in, "()") != 0) {
            res = SVNPROTO_EDITOR + 20;
            goto END;
        }

        //TRACE("%s END", cmd);
        res = PARSE_EOD;
        goto END;


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

        TRACE(FRED("ERROR: %s"), BDATA(ctx->last_error.message));

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

static int
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
           UNUSED void *udata)
{
    int res = 0;
    svndiff_doc_t doc;

    doc.flags = 0;
    res = svnproto_unpack(ctx, in, "(rr)",
                          editor_cb1, NULL,
                          editor_cb2, &doc);


    return res;
}


int
svnproto_editor(svnc_ctx_t *ctx)
{
    /* editor starts with check auth */
    if (svnproto_check_auth(ctx) != 0) {
        TRRET(SVNPROTO_EDITOR + 24);
    }

    cmd = NULL;
    //off_t saved_pos = ctx->in.pos;


    if (svnproto_unpack(ctx, &ctx->in, "r*", editor_cb0, NULL) != 0) {
        //TRRET(SVNPROTO_EDITOR + 25);
    }

    //TRACE("consumed %ld", ctx->in.pos - saved_pos);

    if (cmd != NULL) {
        free(cmd);
        cmd = NULL;
    }

    return 0;
}

