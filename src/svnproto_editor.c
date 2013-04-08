#include <string.h>
#include <stdio.h>
#include <unistd.h>

#include "mrkcommon/array.h"
#include "mrkcommon/util.h"
//#define TRRET_DEBUG
#include "mrkcommon/dumpm.h"
#include "diag.h"

#include "mrkcommon/bytestream.h"
#include "mrksvnup/svnc.h"
#include "mrksvnup/svncdir.h"
#include "mrksvnup/svnedit.h"
#include "mrksvnup/svnproto.h"

/* global editor context */
static char *cmd = NULL;
#define COMMAND_CLOSING 0x01
static int flags = 0;

static int
open_root(svnc_ctx_t *ctx,
          bytestream_t *in)
{
    int res = 0;
    long rev = -1;
    bytes_t *token = NULL;

    res = svnproto_unpack(ctx, in, "((n?)s)", &rev, &token);
    if (res != 0) {
        res = OPEN_ROOT + 1;
        goto END;
    }

    if (ctx->debug_level > 3) {
        LTRACE(1, "%s rev=%ld token=%s", cmd, rev, BDATA(token));
    }

    if (svnedit_open_root(ctx, rev, token) != 0) {
        res = OPEN_ROOT + 2;
        goto END;
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
    bytes_t *path = NULL;
    long rev = -1;
    bytes_t *token = NULL;

    res = svnproto_unpack(ctx, in, "(s(n?)s)", &path, &rev, &token);
    if (res != 0) {
        res = DELETE_ENTRY + 1;
        goto END;
    }

    if (ctx->debug_level > 3) {
        LTRACE(1, "%s path=%s rev=%ld token=%s",
               cmd, BDATA(path), rev, BDATA(token));
    }

    if (svnedit_delete_entry(ctx, path, rev, token) != 0) {
        res = DELETE_ENTRY + 2;
        goto END;
    }

END:
    if (path != NULL) {
        free(path);
    }
    if (token != NULL) {
        free(token);
    }
    TRRET(res);
}

static int
add_dir(svnc_ctx_t *ctx,
        bytestream_t *in)
{
    int res = 0;
    bytes_t *path = NULL;
    bytes_t *parent_token = NULL;
    bytes_t *child_token = NULL;
    bytes_t *copy_path = NULL;
    long copy_rev = -1;

    /*
     * XXX support of copy-path/copy-rev
     */
    res = svnproto_unpack(ctx, in, "(sss(s?n?))",
                          &path, &parent_token, &child_token,
                          &copy_path, &copy_rev);
    if (res != 0) {
        res = ADD_DIR + 1;
        goto END;
    }

    if (ctx->debug_level > 3) {
        LTRACE(1, "%s path=%s parent_token=%s child_token=%s "
               "copy_path=%s copy_rev=%ld",
               cmd, BDATA(path), BDATA(parent_token), BDATA(child_token),
               copy_path != NULL ? BDATA(copy_path) : NULL, copy_rev);
    }

    if (svnedit_add_dir(ctx, path) != 0) {
        res = ADD_DIR + 2;
        goto END;
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
    TRRET(res);
}

static int
open_dir(svnc_ctx_t *ctx,
         bytestream_t *in)
{
    int res = 0;
    bytes_t *path = NULL;
    bytes_t *parent_token = NULL;
    bytes_t *child_token = NULL;
    long rev = -1;

    res = svnproto_unpack(ctx, in, "(sss(n))",
            &path, &parent_token, &child_token, &rev);
    if (res != 0) {
        res = OPEN_DIR + 1;
        goto END;
    }

    if (ctx->debug_level > 3) {
        LTRACE(1, "%s path=%s parent_token=%s child_token=%s rev=%ld",
               cmd, BDATA(path), BDATA(parent_token), BDATA(child_token), rev);
    }

    if (svnedit_open_dir(ctx, path) != 0) {
        res = OPEN_DIR + 2;
        goto END;
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

    TRRET(res);
}

static int
change_dir_prop(svnc_ctx_t *ctx,
                bytestream_t *in)
{
    int res = 0;
    bytes_t *token = NULL;
    bytes_t *name = NULL;
    bytes_t *value = NULL;

    res = svnproto_unpack(ctx, in, "(ss(s?))", &token, &name, &value);
    if (res != 0) {
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
    bytes_t *token = NULL;

    res = svnproto_unpack(ctx, in, "(s)", &token);
    if (res != 0) {
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
    bytes_t *path = NULL;
    bytes_t *parent_token = NULL;

    res = svnproto_unpack(ctx, in, "(ss)", &path, &parent_token);
    if (res != 0) {
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
    bytes_t *dir_token = NULL;
    bytes_t *copy_path = NULL;
    long copy_rev = -1;
    svndiff_doc_t *doc;

    /*
     * XXX support of copy-path
     */
    if ((doc = svnedit_clear_doc()) == NULL) {
        res = ADD_FILE + 1;
        goto END;
    }

    res = svnproto_unpack(ctx, in, "(sss(s?n?))",
                        &doc->rp, &dir_token,
                        &doc->ft,
                        &copy_path, &copy_rev);
    if (res != 0) {
        res = ADD_FILE + 2;
        goto END;
    }

    if (ctx->debug_level > 3) {
        LTRACE(1, "%s path=%s dir_token=%s file_token=%s copy_path=%s "
               "copy_rev=%ld",
               cmd, BDATA(doc->rp), BDATA(dir_token),
               BDATA(doc->ft),
               BDATA(copy_path), copy_rev);
    }

    if (svnedit_add_file(ctx) != 0) {
        res = ADD_FILE + 3;
        goto END;
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
open_file(svnc_ctx_t *ctx,
          bytestream_t *in)
{
    int res = 0;
    bytes_t *dir_token = NULL;
    svndiff_doc_t *doc;

    if ((doc = svnedit_clear_doc()) == NULL) {
        res = OPEN_FILE + 1;
        goto END;
    }

    res = svnproto_unpack(ctx, in, "(sss(n))", &doc->rp,
                        &dir_token, &doc->ft, &doc->rev);
    if (res != 0) {
        res = OPEN_FILE + 2;
        goto END;
    }

    if (ctx->debug_level > 3) {
        LTRACE(1, "%s path=%s dir_token=%s file_token=%s rev=%ld",
               cmd, BDATA(doc->rp), BDATA(dir_token),
               BDATA(doc->ft), doc->rev);
    }

    if (svnedit_open_file(ctx) != 0) {
        res = OPEN_FILE + 3;
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
                bytestream_t *in)
{
    int res = 0;
    bytes_t *file_token = NULL;
    svndiff_doc_t *doc;

    if ((doc = svnedit_get_doc()) == NULL) {
        res = APPLY_TEXTDELTA + 1;
        goto END;
    }

    res = svnproto_unpack(ctx, in, "(s(s?))",
                        &file_token,
                        &doc->base_checksum);
    if (res != 0) {
        res = APPLY_TEXTDELTA + 2;
        goto END;
    }

    if (ctx->debug_level > 3) {
        LTRACE(1, "%s file_token=%s base_checksum=%s",
               cmd, BDATA(file_token),
               BDATA(doc->base_checksum));
    }

END:
    if (file_token != NULL) {
        free(file_token);
    }

    TRRET(res);
}

static int
chunk_cb(UNUSED svnc_ctx_t *ctx,
         bytestream_t *in,
         svnproto_state_t *st,
         void *udata)
{
    svndiff_doc_t *doc = udata;

    if (svndiff_parse_doc(SDATA(in, st->r.start),
                          SDATA(in, st->r.end),
                          doc) != 0) {

        /*
         * XXX do we really need to signal svnproto_vunpack() here?
         * Investigate it.
         */
        TRRET(SVNPROTO_EDITOR + 1);
    }
    return 0;
}

static int
textdelta_chunk(svnc_ctx_t *ctx,
                bytestream_t *in)
{
    int res = 0;
    bytes_t *file_token = NULL;
    svndiff_doc_t *doc;

    if ((doc = svnedit_get_doc()) == NULL) {
        res = TEXTDELTA_CHUNK + 1;
        goto END;
    }

    res = svnproto_unpack(ctx, in, "(sS)", &file_token,
                        chunk_cb, doc);
    if (res != 0) {
        res = TEXTDELTA_CHUNK + 2;
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
    bytes_t *file_token = NULL;

    res = svnproto_unpack(ctx, in, "(s)", &file_token);
    if (res != 0) {
        res = TEXTDELTA_END + 1;
        goto END;
    }

    if (ctx->debug_level > 3) {
        LTRACE(1, "[not implemented] %s file_token=%s",
               cmd, BDATA(file_token));
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
    bytes_t *file_token = NULL;
    bytes_t *name = NULL;
    bytes_t *value = NULL;

    res = svnproto_unpack(ctx, in, "(ss(s?))",
                        &file_token, &name, &value);
    if (res != 0) {
        res = CHANGE_FILE_PROP + 1;
        goto END;
    }

    if (ctx->debug_level > 3) {
        LTRACE(1, "%s token=%s name=%s value=%s",
               cmd, BDATA(file_token), BDATA(name),
               BDATA(value));
    }

    /*
     * XXX Check that the file_token matches doc.ft
     */

    if (svnedit_change_file_prop(ctx, name, value) != 0) {
        res = CHANGE_FILE_PROP + 2;
        goto END;
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
close_file(svnc_ctx_t *ctx,
           bytestream_t *in)
{
    int res = 0;
    bytes_t *file_token = NULL;
    bytes_t *text_checksum = NULL;

    res = svnproto_unpack(ctx, in, "(s(s?))",
                        &file_token, &text_checksum);
    if (res != 0) {
        res = CLOSE_FILE + 1;
        goto END;
    }

    if (ctx->debug_level > 3) {
        LTRACE(1, "%s file_token=%s text_checksum=%s",
               cmd, BDATA(file_token), BDATA(text_checksum));
    }

    if (svnedit_close_file(ctx, file_token, text_checksum) != 0) {
        res = CLOSE_FILE + 2;
        goto END;
    }

END:
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
    bytes_t *path = NULL;
    bytes_t *parent_token = NULL;

    res = svnproto_unpack(ctx, in, "(ss)", &path, &parent_token);
    if (res != 0) {
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
unpack2(svnc_ctx_t *ctx,
           bytestream_t *in,
           UNUSED svnproto_state_t *st,
           UNUSED void *udata)
{
    int res = 0;

    if (strcmp(cmd, "target-rev") == 0) {
        long target_rev = 0;

        res = svnproto_unpack(ctx, in, "(n)", &target_rev);
        if (res != 0) {
            res = SVNPROTO_EDITOR + 2;
            goto END;
        }

        if (ctx->debug_level > 3) {
            LTRACE(1, "%s rev=%ld", cmd, target_rev);
        }

        if (svnedit_target_rev(ctx, target_rev) != 0) {
            res = SVNPROTO_EDITOR + 3;
            goto END;
        }

    } else if (strcmp(cmd, "open-root") == 0) {
        if (open_root(ctx, in) != 0) {
            res = SVNPROTO_EDITOR + 4;
            goto END;
        }

    } else if (strcmp(cmd, "delete-entry") == 0) {
        if (delete_entry(ctx, in) != 0) {
            res = SVNPROTO_EDITOR + 5;
            goto END;
        }

    } else if (strcmp(cmd, "add-dir") == 0) {
        if (add_dir(ctx, in) != 0) {
            res = SVNPROTO_EDITOR + 6;
            goto END;
        }

    } else if (strcmp(cmd, "open-dir") == 0) {
        if (open_dir(ctx, in) != 0) {
            res = SVNPROTO_EDITOR + 7;
            goto END;
        }

    } else if (strcmp(cmd, "change-dir-prop") == 0) {
        if (change_dir_prop(ctx, in) != 0) {
            res = SVNPROTO_EDITOR + 8;
            goto END;
        }

    } else if (strcmp(cmd, "close-dir") == 0) {
        if (close_dir(ctx, in) != 0) {
            res = SVNPROTO_EDITOR + 9;
            goto END;
        }

    } else if (strcmp(cmd, "absent-dir") == 0) {
        if (absent_dir(ctx, in) != 0) {
            res = SVNPROTO_EDITOR + 10;
            goto END;
        }

    } else if (strcmp(cmd, "add-file") == 0) {
        if (add_file(ctx, in) != 0) {
            res = SVNPROTO_EDITOR + 11;
            goto END;
        }

    } else if (strcmp(cmd, "open-file") == 0) {
        if (open_file(ctx, in) != 0) {
            res = SVNPROTO_EDITOR + 12;
            goto END;
        }

    } else if (strcmp(cmd, "apply-textdelta") == 0) {
        if (apply_textdelta(ctx, in) != 0) {
            res = SVNPROTO_EDITOR + 13;
            goto END;
        }

    } else if (strcmp(cmd, "textdelta-chunk") == 0) {
        if (textdelta_chunk(ctx, in) != 0) {
            res = SVNPROTO_EDITOR + 14;
            goto END;
        }

    } else if (strcmp(cmd, "textdelta-end") == 0) {
        if (textdelta_end(ctx, in) != 0) {
            res = SVNPROTO_EDITOR + 15;
            goto END;
        }

    } else if (strcmp(cmd, "change-file-prop") == 0) {
        if (change_file_prop(ctx, in) != 0) {
            res = SVNPROTO_EDITOR + 16;
            goto END;
        }

    } else if (strcmp(cmd, "close-file") == 0) {
        if (close_file(ctx, in) != 0) {
            res = SVNPROTO_EDITOR + 17;
            goto END;
        }

    } else if (strcmp(cmd, "absent-file") == 0) {
        if (absent_file(ctx, in) != 0) {
            res = SVNPROTO_EDITOR + 18;
            goto END;
        }

    } else if (strcmp(cmd, "close-edit") == 0) {
        res = svnproto_unpack(ctx, in, "()");
        if (res != 0) {
            res = SVNPROTO_EDITOR + 19;
            goto END;
        }

        flags |= COMMAND_CLOSING;

        if (ctx->debug_level > 3) {
            LTRACE(1, "%s", cmd);
        }

    } else if (strcmp(cmd, "abort-edit") == 0) {
        res = svnproto_unpack(ctx, in, "()");
        if (res != 0) {
            res = SVNPROTO_EDITOR + 20;
            goto END;
        }

        if (ctx->debug_level > 3) {
            LTRACE(1, "[not implemented] %s", cmd);
        }

    } else if (strcmp(cmd, "finish-replay") == 0) {
        res = svnproto_unpack(ctx, in, "()");
        if (res != 0) {
            res = SVNPROTO_EDITOR + 21;
            goto END;
        }

        if (ctx->debug_level > 3) {
            LTRACE(1, "[not implemented] %s", cmd);
        }

    } else if (strcmp(cmd, "failure") == 0) {
        svnc_clear_last_error(ctx);
        res = svnproto_unpack(ctx, in, "((nssn))",
                              &ctx->last_error.apr_error,
                              &ctx->last_error.message,
                              &ctx->last_error.file,
                              &ctx->last_error.line);

        if (ctx->debug_level > 3) {
            LTRACE(1, "[not implemented] %s apr_error=%ld "
                      "message=%s file=%s line=%ld",
                      cmd,
                      ctx->last_error.apr_error,
                      BDATA(ctx->last_error.message),
                      BDATA(ctx->last_error.file),
                      ctx->last_error.line);
        }

        if (res != 0) {
            res = SVNPROTO_EDITOR + 22;
            goto END;
        }

    } else {
        TRACE(FRED("unknown: %s"), cmd);
        res = SVNPROTO_EDITOR + 23;
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
unpack1(svnc_ctx_t *ctx,
           bytestream_t *in,
           UNUSED svnproto_state_t *st,
           void *udata)
{
    int res = 0;

    res = svnproto_unpack(ctx, in, "(wr)",
                          &cmd,
                          unpack2, udata);
    if (res == 0 && flags & COMMAND_CLOSING) {
        /* we terminate the matching sequence */
        res = SVNPROTO_UNPACK_NOMATCH_GOAHEAD;
    }

    TRRET(res);
}


int
svnproto_editor(svnc_ctx_t *ctx)
{
    int res;

    /* editor starts with check auth */
    if (svnproto_check_auth(ctx) != 0) {
        TRRET(SVNPROTO_EDITOR + 24);
    }

    cmd = NULL;

    if (svnedit_init_shadow_ctx(ctx) != 0) {
        TRRET(SVNPROTO_EDITOR + 25);
    }

    res = svnproto_unpack(ctx, &ctx->in, "r*", unpack1, NULL);
    res = SVNPROTO_IGNORE_VUNPACK(res);

    if (cmd != NULL) {
        free(cmd);
        cmd = NULL;
    }

    svnedit_close_shadow_ctx();

    TRRET(res);
}

