#include <assert.h>
#include <stdlib.h>
#include <time.h>

#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>

#include "unittest.h"
#include "diag.h"
#include "mrkcommon/array.h"
#include "mrkcommon/dumpm.h"
#include "mrkcommon/util.h"
#include "mrkcommon/traversedir.h"

#include "mrksvnup/svnc.h"
#include "mrksvnup/svncdir.h"
#include "mrksvnup/svnproto.h"

static int
mystrcmp(const char *a, const char *b)
{
    if (a == NULL || b == NULL) {
        return ((uintptr_t)a ^ (uintptr_t)b);
    }
    return strcmp(a, b);
}

UNUSED static void
test_parse_url(void)
{
    char *host;
    int port;
    char *path;
    int res;

    struct {
        long rnd;
        const char *url;
        int expected;
        const char *host;
        int port;
        const char *path;
    } data[] = {
        {0, "svn://test.com/1/2/3", 0, "test.com", SVN_DEFAULT_PORT, "/1/2/3"},
        {0, "svn://test.com/", 0, "test.com", SVN_DEFAULT_PORT, "/"},
        {0, "svn://test.com", SVN_URL_PARSE + 2, NULL, 0, NULL},
        {0, "svn://test.com:123/1/2/3", 0, "test.com", 123, "/1/2/3"},
        {0, "svn://test.com:123/", 0, "test.com", 123, "/"},
        {0, "svn://test.com:123", SVN_URL_PARSE + 2, NULL, 0, NULL},
        {0, "svn://test.com:qwe/", SVN_URL_PARSE + 4, "test.com", 0, NULL},
        {0, "svn://test.com:qwe", SVN_URL_PARSE + 2, NULL, 0, NULL},
        {0, "test.com:123/1/2/3", SVN_URL_PARSE + 1, NULL, 0, NULL},
    };
    UNITTEST_PROLOG_RAND;

    FOREACHDATA {
        host = NULL;
        port = 0;
        path = NULL;

        //TRACE("Trying %s", CDATA.url);

        res = svn_url_parse(CDATA.url, &host, &port, &path);

        //TRACE("host=%s port=%d path=%s", host, port, path);
        assert(res == CDATA.expected &&
               mystrcmp(CDATA.host, host) == 0 &&
               CDATA.port == port &&
               mystrcmp(CDATA.path, path) == 0);

        if (host != NULL) {
            free(host);
        }

        if (path != NULL) {
            free(path);
        }
    }
}

static int
dump_long(long *v, UNUSED void *udata)
{
    TRACE("v=%ld", *v);
    return 0;
}

static int
init_string(const char **v)
{
    *v = NULL;
    return 0;
}

static int
fini_string(char **v)
{
    if (*v != NULL) {
        free(*v);
        *v = NULL;
    }
    return 0;
}

static int
dump_string(const char **v, UNUSED void *udata)
{
    TRACE("v=%s", *v);
    return 0;
}

UNUSED static void
test_unpack(void)
{
    svnc_ctx_t *ctx;
    long n1, n2, n3, n4, n5 = -1;
    char *s1 = NULL, *s2 = NULL, *s3 = NULL, *s4 = NULL,
         *s5 = NULL, *s6 = NULL, *s7 = NULL, *s8 = NULL;
    array_t ar;

    if ((ctx = svnc_new("svn://svn.freebsd.org/base/user/des/svnsup"))
        == NULL) {

        assert(0);
    }


    if (svnc_debug_open(ctx, "testnumber") != 0) {
        assert(0);
    }

    if (svnproto_unpack(ctx, "nnn", &n1, &n2, &n3) != 0) {
        assert(0);
    }

    TRACE("n1=%ld n2=%ld n3=%ld", n1, n2, n3);

    svnc_close(ctx);


    if (svnc_debug_open(ctx, "testnumber") != 0) {
        assert(0);
    }

    if (svnproto_unpack(ctx, "n?n?n?n?n?", &n1, &n2, &n3, &n4, &n5) != 0) {
        assert(0);
    }

    TRACE("n1=%ld n2=%ld n3=%ld n4=%ld n5=%ld", n1, n2, n3, n4, n5);

    svnc_close(ctx);


    if (array_init(&ar, sizeof(long), 0, NULL, NULL)) {
        assert(0);
    }

    if (svnc_debug_open(ctx, "testnumber") != 0) {
        assert(0);
    }

    if (svnproto_unpack(ctx, "n*", &ar) != 0) {
        assert(0);
    }

    array_traverse(&ar, (array_traverser_t)dump_long, NULL);
    array_fini(&ar);

    svnc_close(ctx);


    if (svnc_debug_open(ctx, "testword") != 0) {
        assert(0);
    }

    if (svnproto_unpack(ctx, "wwwwwww",
                        &s1, &s2, &s3, &s4, &s5, &s6, &s7, &s8) != 0) {
        assert(0);
    }

    TRACE("s1=%s s2=%s s3=%s s4=%s s5=%s s6=%s s7=%s",
          s1, s2, s3, s4, s5, s6, s7);

    svnc_close(ctx);


    if (svnc_debug_open(ctx, "testword") != 0) {
        assert(0);
    }

    if (svnproto_unpack(ctx, "wwwwwwww?",
                        &s1, &s2, &s3, &s4, &s5, &s6, &s7, &s8) != 0) {
        assert(0);
    }

    TRACE("s1=%s s2=%s s3=%s s4=%s s5=%s s6=%s s7=%s s8=%s",
          s1, s2, s3, s4, s5, s6, s7, s8);

    svnc_close(ctx);


    if (svnc_debug_open(ctx, "testword") != 0) {
        assert(0);
    }

    if (svnproto_unpack(ctx, "wwwwwwww?n?",
                        &s1, &s2, &s3, &s4, &s5, &s6, &s7, &s8, &n1) != 0) {
        assert(0);
    }

    TRACE("s1=%s s2=%s s3=%s s4=%s s5=%s s6=%s s7=%s s8=%s n1=%ld",
          s1, s2, s3, s4, s5, s6, s7, s8, n1);

    svnc_close(ctx);


    if (svnc_debug_open(ctx, "testword") != 0) {
        assert(0);
    }

    if (array_init(&ar, sizeof(char *), 0,
                (array_initializer_t)init_string,
                (array_finalizer_t)fini_string)) {
        assert(0);
    }

    if (svnproto_unpack(ctx, "w*n?",
                        &ar, &n1) != 0) {
        assert(0);
    }

    array_traverse(&ar, (array_traverser_t)dump_string, NULL);
    array_fini(&ar);
    TRACE("n1=%ld", n1);

    svnc_close(ctx);


    //if (svnc_debug_open(ctx, "teststring") != 0) {
    //    assert(0);
    //}

    //svnproto_spec_t teststring_sp = {"sss", 0};
    //if (svnproto_unpack(ctx, &teststring_sp) != 0) {
    //    assert(0);
    //}

    //svnc_close(ctx);

    svnc_destroy(ctx);
}

static int
my_unpack_cb(svnc_ctx_t *ctx, UNUSED svnproto_state_t *st, UNUSED void *udata)
{
    int res;
    long n1 = 0;
    array_t ar;

    svnproto_init_string_array(&ar);
    res = svnproto_unpack(ctx, "(nw*)", &n1, &ar);
    svnproto_dump_string_array(&ar);
    array_fini(&ar);
    TRACE("n1=%ld", n1);

    return res;
}

UNUSED static void
test_unpack_cb(void)
{
    svnc_ctx_t *ctx;
    array_t ar;

    if ((ctx = svnc_new("svn://svn.freebsd.org/base/user/des/svnsup"))
        == NULL) {

        assert(0);
    }


    if (svnc_debug_open(ctx, "testlist") != 0) {
        assert(0);
    }

    svnproto_init_string_array(&ar);
    if (svnproto_unpack(ctx,
            "(n*)(n*)(n*)(n*)(n*)(n*)(n*)(n*)(n*)(n*)(w*s)((n?)r*)w*",
            NULL,
            NULL,
            NULL,
            NULL,
            NULL,
            NULL,
            NULL,
            NULL,
            NULL,
            NULL,
            NULL, NULL,
            NULL,
            my_unpack_cb, NULL,
            &ar) != 0) {
        assert(0);
    }

    svnproto_dump_string_array(&ar);
    array_fini(&ar);

    svnc_close(ctx);

    svnc_destroy(ctx);
}

UNUSED static void
test_pack(void)
{
    svnc_ctx_t *ctx;

    if ((ctx =
            svnc_new("svn://svn.freebsd.org/base/user/des/svnsup")) == NULL) {

        assert(0);
    }

    if (svnc_debug_open(ctx, "testnumber") != 0) {
        assert(0);
    }

    if (pack_word(&ctx->out, strlen("This"), "This") != 0) {
        assert(0);
    }
    if (pack_word(&ctx->out, strlen("is"), "is") != 0) {
        assert(0);
    }
    if (pack_word(&ctx->out, strlen("the"), "the") != 0) {
        assert(0);
    }
    if (pack_word(&ctx->out, strlen("test"), "test") != 0) {
        assert(0);
    }

    if (pack_string(&ctx->out, 4, "\x01\x02\x03\x04") != 0) {
        assert(0);
    }

    if (pack_string(&ctx->out, 0, "") != 0) {
        assert(0);
    }

    //bytestream_dump(&ctx->out);

    svnc_close(ctx);

    svnc_destroy(ctx);
}

static int
my_caps(svnc_ctx_t *ctx, UNUSED svnproto_state_t *st, UNUSED void *udata)
{
    unsigned i;
    const char *words[] = {
        "edit-pipeline",
        "svndiff1",
        "absent-entries",
        "commit-revprops",
        "depth",
        "log-revprops",
        "partial-replay",
    };

    for (i = 0; i < countof(words); ++i) {
        if (pack_word(&ctx->out, strlen(words[i]), words[i]) != 0) {
            assert(0);
        }
    }

    return (0);
}

static int
my_response(svnc_ctx_t *ctx, UNUSED svnproto_state_t *st, UNUSED void *udata)
{
    if (pack_number(&ctx->out, 2) != 0) {
        assert(0);
    }

    if (pack_list(&ctx->out, my_caps, ctx, NULL) != 0) {
        assert(0);
    }

    if (pack_string(&ctx->out, strlen("svn://test/"), "svn://test/") != 0) {
        assert(0);
    }

    if (pack_string(&ctx->out, strlen("SVN/9.8.7"), "SVN/9.8.7") != 0) {
        assert(0);
    }

    if (pack_list(&ctx->out, NULL, NULL, NULL) != 0) {
        assert(0);
    }

    return (0);
}

UNUSED static void
test_packresponse(void)
{
    svnc_ctx_t *ctx;

    struct {
        long rnd;
        int in;
        int expected;
    } data[] = {
        {0, 0, 0},
    };
    UNITTEST_PROLOG_RAND;

    FOREACHDATA {
        //TRACE("in=%d expected=%d", CDATA.in, CDATA.expected);
        assert(CDATA.in == CDATA.expected);
    }

    if ((ctx =
            svnc_new("svn://svn.freebsd.org/base/user/des/svnsup")) == NULL) {

        assert(0);
    }

    if (svnc_debug_open(ctx, "testnumber") != 0) {
        assert(0);
    }

    if (pack_list(&ctx->out, my_response, ctx, NULL) != 0) {
        assert(0);
    }

    //bytestream_dump(&ctx->out);

    bytestream_rewind(&ctx->out);

    if (pack_list(&ctx->out, my_response, ctx, NULL) != 0) {
        assert(0);
    }

    //bytestream_dump(&ctx->out);

    svnc_close(ctx);

    svnc_destroy(ctx);
}

UNUSED static void
test_simple(void)
{
    svnc_ctx_t *ctx;
    long rev = 0;
    int kind = -1;
    array_t dirents;
    array_iter_t it;
    svnproto_dirent_t *de;
    svnproto_fileent_t fe;

    if ((ctx =
            svnc_new("svn://svn.freebsd.org/base/user/des/svnsup")) == NULL) {

        assert(0);
    }

    if (svnc_debug_open(ctx, "testsuccess") != 0) {
        assert(0);
    }

    if (svnproto_setup(ctx) != 0) {
        assert(0);
    }

    if (svnproto_get_latest_rev(ctx, &rev) != 0) {
        assert(0);
    }

    TRACE("rev=%ld", rev);

    if (svnproto_check_path(ctx, "", rev, &kind) != 0) {
        assert(0);
    }

    TRACE("kind=%s", svnproto_kind2str(kind));

    svnproto_init_dirent_array(&dirents);

    if (svnproto_get_dir(ctx, "", rev, &dirents) != 0) {
        assert(0);
    }

    svnproto_dump_dirent_array(&dirents);

    if ((de = array_first(&dirents, &it)) == NULL) {
        assert(0);
    }

    svnproto_fileent_init(&fe);
    if (svnproto_get_file(ctx, de->name->data, de->rev,
                          GETFLAG_WANT_PROPS | GETFLAG_WANT_CONTENTS,
                          &fe) != 0) {
        assert(0);
    }
    svnproto_fileent_dump(&fe);
    svnproto_fileent_fini(&fe);

    array_fini(&dirents);

    svnc_close(ctx);
    svnc_destroy(ctx);
}

UNUSED static void
test_conn1(void)
{
    svnc_ctx_t *ctx;
    long rev = 0;
    int kind = -1;
    array_t dirents;
    array_iter_t it;
    svnproto_dirent_t *de;
    svnproto_fileent_t fe;

    if ((ctx =
            svnc_new("svn://svn.freebsd.org/base/user/des/svnsup")) == NULL) {
        assert(0);
    }

    if (svnc_connect(ctx) != 0) {
        assert(0);
    }

    if (svnproto_get_latest_rev(ctx, &rev) != 0) {
        assert(0);
    }

    TRACE("rev=%ld", rev);

    if (svnproto_check_path(ctx, "", rev, &kind) != 0) {
        assert(0);
    }

    TRACE("kind=%s", svnproto_kind2str(kind));

    svnproto_init_dirent_array(&dirents);

    if (svnproto_get_dir(ctx, "", rev, &dirents) != 0) {
        assert(0);
    }

    svnproto_dump_dirent_array(&dirents);


    for (de = array_first(&dirents, &it);
         de != NULL;
         de = array_next(&dirents, &it)) {
        if (de->kind == SVNP_KIND_FILE) {
            break;
        }
    }
    if (de != NULL) {
        svnproto_fileent_init(&fe);
        if (svnproto_get_file(ctx, de->name->data, de->rev,
                              GETFLAG_WANT_PROPS | GETFLAG_WANT_CONTENTS,
                              &fe) != 0) {
            assert(0);
        }
        svnproto_fileent_dump(&fe);
        svnproto_fileent_fini(&fe);
    }


    array_fini(&dirents);

    svnc_close(ctx);
    svnc_destroy(ctx);
}

static int
walk_cb(svnc_ctx_t *ctx,
        UNUSED const char *dir,
        UNUSED svnproto_dirent_t *de,
        const char *path,
        svnproto_fileent_t *fe,
        void *udata)
{
    struct stat sb;
    char *localpath;
    const char *localroot = udata;

    if ((localpath = path_join(localroot, path)) == NULL) {
        FAIL("path_join");
    }

    if (fe != NULL) {
        TRACE(FGREEN("FILE %s -> %s (%s)"), path, localpath, fe->checksum->data);

        if (lstat(localpath, &sb) != 0) {
            int fd;
            svnproto_fileent_t ffe;
            svnproto_bytes_t **chunk;
            array_iter_t it;

            if ((fd = open(localpath, O_RDWR|O_CREAT)) < 0) {
                perror("open");
                return 1;
            }

            /* get-file */
            svnproto_fileent_init(&ffe);

            if (svnproto_get_file(ctx, path, fe->rev,
                                  GETFLAG_WANT_CONTENTS, &ffe) != 0) {

                TRACE(FRED("Failed to get a file"));
                svnproto_fileent_fini(&ffe);
                return 1;
            }


            for (chunk = array_first(&ffe.contents, &it);
                 chunk != NULL;
                 chunk = array_next(&ffe.contents, &it)) {
                if (write(fd, (*chunk)->data, (*chunk)->sz) < 0) {
                    perror("write");
                    svnproto_fileent_fini(&ffe);
                    return 1;
                }
            }
            svnproto_fileent_fini(&ffe);

        } else {
            if (!S_ISREG(sb.st_mode)) {
                TRACE(FRED("Not a file"));
                return 1;
            } else {
                /* check sum */
            }
        }
    } else {

        TRACE(FYELLOW("DIR %s"), path);

        if (lstat(localpath, &sb) != 0) {
            if (mkdir(localpath, 0755) != 0) {
                perror("mkdir");
                return 1;
            }
        } else {
            if (!S_ISDIR(sb.st_mode)) {
                TRACE(FRED("Not a dir"));
                return 1;
            }
        }
    }
    //svnproto_fileent_dump(fe);
    /* directory exists */
    /* no, create directory */

    /* file exists */
    /* no, get-file */

    /* calc md5 over the file */
    /* md5 == fe->checksum */
    /* no, get-file */

    return 0;
}

UNUSED static void
test_conn2(void)
{
    svnc_ctx_t *ctx;
    struct stat sb;
    const char *localroot = "qwe";

    if ((ctx =
            svnc_new("svn://svn.freebsd.org/base/user/des/svnsup")) == NULL) {
        assert(0);
    }

    if (svnc_connect(ctx) != 0) {
        assert(0);
    }

    if (lstat(localroot, &sb) != 0) {
        assert(0);
    } else {
        if (!S_ISDIR(sb.st_mode)) {
            assert(0);
        }
    }

    if (svncdir_walk(ctx, "", walk_cb, (void *)localroot) != 0) {
        assert(0);
    }

    svnc_close(ctx);
    svnc_destroy(ctx);
}

UNUSED static void
test_editor(void)
{
    svnc_ctx_t *ctx;

    if ((ctx =
            svnc_new("svn://svn.freebsd.org/base/user/des/svnsup")) == NULL) {

        assert(0);
    }

    if (svnc_debug_open(ctx, "testeditor") != 0) {
        assert(0);
    }

    if (svnproto_setup(ctx) != 0) {
        assert(0);
    }

    if (svnproto_editor(ctx) != 0) {
        assert(0);
    }

    svnc_close(ctx);
    svnc_destroy(ctx);
}

static int
update_cb(svnc_ctx_t *ctx, UNUSED svnproto_state_t *st, void *udata)
{
    struct {
        const char *path;
        long source_rev;
        long set_path_flags;
    } *params = udata;

    if (svnproto_set_path(ctx, params->path, params->source_rev,
                          NULL, SVN_DEPTH_INFINITY,
                          params->set_path_flags) != 0) {
        assert(0);
    }

    TRACE("set-path OK");

    if (svnproto_finish_report(ctx) != 0) {
        assert(0);
    }

    TRACE("finish-report OK");

    if (svnproto_editor(ctx) != 0) {
        assert(0);
    }

    return 0;
}

UNUSED static void
test_conn3(const char *localroot, long source_rev, long target_rev)
{
    svnc_ctx_t *ctx;
    int kind = -1;
    struct {
        const char *path;
        long source_rev;
        long set_path_flags;
    } update_params = {"", -1, 0};

    if ((ctx =
            svnc_new("svn://svn.FreeBSD.org/base/stable/9")) == NULL) {
            //svnc_new("svn://svn.freebsd.org/base/user/des/svnsup")) == NULL) {
            //svnc_new("svn://localhost/mysvn")) == NULL) {
        assert(0);
    }

    ctx->localroot = strdup(localroot);

    if (svnc_connect(ctx) != 0) {
        assert(0);
    }

    /*
     * main: <rev> = get-latest-rev checkout only (if update, pick saved rev)
     * main: reparent svn://... checkout only
     * main: check-path for dir checkout only
     * main: update <rev> -> report / editor command set
     *          if update, omit <rev>
     * report: set-path <rev> + finish-report
     * edit:
     *  target-rev rev
     *  open-root rev dir-token
     *  change-dir-prop svn:entry:committed-rev
     *  change-dir-prop svn:entry:committed-date
     *  change-dir-prop svn:entry:last-author
     *  change-dir-prop svn:entry:uuid
     *  change-dir-prop svn:ignore
     *  add-file fname fir-token file-token
     *  change-file-prop svn:entry:committed-rev
     *  change-file-prop svn:entry:committed-date
     *  change-file-prop svn:entry:last-author
     *  change-file-prop svn:entry:uuid
     *  change-file-prop svn:eol-style
     *  change-file-prop svn:keywords
     *  apply-textdelta
     *  textdelta-chunk
     *  ...
     *  textdelta-end
     *  close-file
     *
     *  add-dir path parent-token child-token
     *  change-dir-prop
     *  ...
     */


    if (source_rev <= 0) {
        /* check out */
        if (target_rev <= 0) {
            if (svnproto_get_latest_rev(ctx, &target_rev) != 0) {
                assert(0);
            }
        }
        source_rev = target_rev;

        update_params.set_path_flags |= SETPFLAG_START_EMPTY;
    } else {
        /* update */
        if (target_rev <= 0) {
            if (target_rev <= 0) {
                if (svnproto_get_latest_rev(ctx, &target_rev) != 0) {
                    assert(0);
                }
            }
        }
    }

    if (svnproto_check_path(ctx, update_params.path,
                            target_rev, &kind) != 0) {
        assert(0);
    }
    TRACE("check_path kind=%s", svnproto_kind2str(kind));

    update_params.source_rev = source_rev;

    if (target_rev > 0) {
        assert(source_rev <= target_rev);
    }

    if (svnproto_update(ctx, target_rev, update_params.path, 0,
                        UPFLAG_RECURSE, update_cb, &update_params) != 0) {
        assert(0);
    }

    TRACE("update OK");

    svnc_close(ctx);
    svnc_destroy(ctx);
}


int
main(void)
{
    //test_parse_url();
    //test_unpack();
    //test_unpack_cb();
    //test_pack();
    //test_packresponse();
    //test_simple();
    //test_editor();
    test_conn3("./qwe", -1, -1);
    //test_conn3("./qwe", -1, 2);
    //test_conn3("./qwe", 2, 3);
    //test_conn3("./qwe", 3, -1);
    return 0;
}
