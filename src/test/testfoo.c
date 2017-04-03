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
#include "mrksvnup/http.h"
#include "mrksvnup/svncdir.h"
#include "mrksvnup/svnproto.h"

UNUSED static int
mystrcmp(const char *a, const char *b)
{
    if (a == NULL || b == NULL) {
        return ((uintptr_t)a ^ (uintptr_t)b);
    }
    return strcmp(a, b);
}

UNUSED static int
mymemcmp(const char *a, const char *b, size_t sz)
{
    if (a == NULL || b == NULL) {
        return ((uintptr_t)a ^ (uintptr_t)b);
    }
    return memcmp(a, b, sz);
}

UNUSED static void
test_parse_url(void)
{
    int scheme;
    char *host;
    int port;
    char *path;
    int res;

    struct {
        long rnd;
        const char *url;
        int expected;
        int scheme;
        const char *host;
        int port;
        const char *path;
    } data[] = {
        {0, "svn://test.com/1/2/3", 0, SVNC_SCHEME_SVN, "test.com", SVN_DEFAULT_PORT, "/1/2/3"},
        {0, "svn://test.com/", 0, SVNC_SCHEME_SVN, "test.com", SVN_DEFAULT_PORT, "/"},
        {0, "svn://test.com", SVNC_SCHEME_SVN, SVN_URL_PARSE + 2, NULL, 0, NULL},
        {0, "svn://test.com:123/1/2/3", 0, SVNC_SCHEME_SVN, "test.com", 123, "/1/2/3"},
        {0, "svn://test.com:123/", 0, SVNC_SCHEME_SVN, "test.com", 123, "/"},
        {0, "svn://test.com:123", SVN_URL_PARSE + 2, SVNC_SCHEME_SVN, NULL, 0, NULL},
        {0, "svn://test.com:qwe/", SVN_URL_PARSE + 4, SVNC_SCHEME_SVN, "test.com", 0, NULL},
        {0, "svn://test.com:qwe", SVN_URL_PARSE + 2, SVNC_SCHEME_SVN, NULL, 0, NULL},
        {0, "test.com:123/1/2/3", SVN_URL_PARSE + 1, SVNC_SCHEME_SVN, NULL, 0, NULL},
    };
    UNITTEST_PROLOG_RAND;

    FOREACHDATA {
        scheme = 0;
        host = NULL;
        port = 0;
        path = NULL;

        //TRACE("Trying %s", CDATA.url);

        res = svn_url_parse(CDATA.url, &scheme, &host, &port, &path);

        //TRACE("host=%s port=%d path=%s", host, port, path);
        assert(res == CDATA.expected &&
               CDATA.scheme == scheme &&
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

UNUSED static void
test_utlencode(void)
{
    char *encoded;

    struct {
        long rnd;
        const char *url;
        size_t sz;
        const char*expected;
    } data[] = {
        {0, "qwe", 3, "qwe"},
        {0, "This ", 5, "This%20"},
        {0, "!*'();:@&=+$,/?#[]", 18,
            "%21%2A%27%28%29%3B%3A%40%26%3D%2B%24%2C%2F%3F%23%5B%5D"},
        {0, "\x00\x01\x02\x03\x04\x05\x06\x07\x08\x09\x0a\x0b\x0c\x0d\x0e\x0f",
            16,
            "%00%01%02%03%04%05%06%07%08%09%0A%0B%0C%0D%0E%0F"},
        {0, "\x10\x11\x12\x13\x14\x15\x16\x17\x18\x19\x1a\x1b\x1c\x1d\x1e\x1f",
            16,
            "%10%11%12%13%14%15%16%17%18%19%1A%1B%1C%1D%1E%1F"},
    };
    UNITTEST_PROLOG_RAND;

    FOREACHDATA {
        TRACE("Trying");
        D16(CDATA.url, CDATA.sz);
        encoded = http_urlencode_reserved(CDATA.url, CDATA.sz);
        TRACE("Encoded: %s", encoded);
        assert(mystrcmp(CDATA.expected, encoded) == 0);

        if (encoded != NULL) {
            free(encoded);
        }
    }
}

UNUSED static void
test_utldecode(void)
{
    struct {
        long rnd;
        const char *encoded;
        const char*expected;
    } data[] = {
        {0, "qwe", "qwe"},
        {0, "This%20", "This "},
        {0,
            "%21%2A%27%28%29%3B%3A%40%26%3D%2B%24%2C%2F%3F%23%5B%5D",
            "!*'();:@&=+$,/?#[]",
        },
        {0,
            "%21%2a%27%28%29%3b%3a%40%26%3d%2b%24%2c%2f%3f%23%5b%5d",
            "!*'();:@&=+$,/?#[]",
        },
        {0,
            "%00%01%02%03%04%05%06%07%08%09%0A%0B%0C%0D%0E%0F",
            "\x00\x01\x02\x03\x04\x05\x06\x07\x08\x09\x0a\x0b\x0c\x0d\x0e\x0f",
        },
        {0,
            "%00%01%02%03%04%05%06%07%08%09%0a%0b%0c%0d%0e%0f",
            "\x00\x01\x02\x03\x04\x05\x06\x07\x08\x09\x0a\x0b\x0c\x0d\x0e\x0f",
        },
        {0,
            "%10%11%12%13%14%15%16%17%18%19%1A%1B%1C%1D%1E%1F",
            "\x10\x11\x12\x13\x14\x15\x16\x17\x18\x19\x1a\x1b\x1c\x1d\x1e\x1f",
        },
        {0,
            "%10%11%12%13%14%15%16%17%18%19%1a%1b%1c%1d%1e%1f",
            "\x10\x11\x12\x13\x14\x15\x16\x17\x18\x19\x1a\x1b\x1c\x1d\x1e\x1f",
        },
    };
    UNITTEST_PROLOG_RAND;

    FOREACHDATA {
        char *start, *end;

        TRACE("Trying %s", CDATA.encoded);
        start = strdup(CDATA.encoded);
        end = http_urldecode(start);
        TRACE("Decoded:");
        D16(start, end - start);
        assert(mymemcmp(start, CDATA.expected, end - start) == 0);

        if (start != NULL) {
            free(start);
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

static int
str_cb(UNUSED svnc_ctx_t *ctx,
       UNUSED mnbytestream_t *in,
       svnproto_state_t *st,
       UNUSED void *udata)
{
    int res = 0;
    mnbytes_t **b;
    mnarray_t *ar = udata;


    if (st->r.end == st->r.start) {
        TRACE(FRED("!!!!!!!!"));
        res = SVNPROTO_UNPACK_NOMATCH_GOAHEAD;
    } else {
        TRACE("str %s", SDATA(in, st->r.start));
        if ((b = array_incr(ar)) == NULL) {
            FAIL("array_incr");
        }

        if ((*b = malloc(sizeof(mnbytes_t) + st->r.end - st->r.start)) == NULL) {
            FAIL("malloc");
        }
        (*b)->sz = st->r.end - st->r.start;
        memcpy((*b)->data, SDATA(in, st->r.start), (*b)->sz);
    }

    return res;
}

UNUSED static void
test_unpack(void)
{
    int res;
    svnc_ctx_t *ctx;
    long n1, n2, n3, n4, n5 = -1;
    char *s1 = NULL, *s2 = NULL, *s3 = NULL, *s4 = NULL,
         *s5 = NULL, *s6 = NULL, *s7 = NULL, *s8 = NULL;
    mnarray_t ar;

    if ((ctx = svnc_new("svn://localhost/mysvn", "qwe", 0, 0))
        == NULL) {

        assert(0);
    }


    if (svnc_debug_open(ctx, "testnumber") != 0) {
        assert(0);
    }

    res = svnproto_unpack(ctx, &ctx->in, "nnn", &n1, &n2, &n3);
    TRACE("res=%s", diag_str(res));
    //if (SVNPROTO_UNPACK(ctx, &ctx->in, "nnn", &n1, &n2, &n3) != 0) {
    //    assert(0);
    //}

    TRACE("n1=%ld n2=%ld n3=%ld", n1, n2, n3);

    svnc_close(ctx);


    if (svnc_debug_open(ctx, "testnumber") != 0) {
        assert(0);
    }

    if (svnproto_unpack(ctx, &ctx->in, "n?n?n?n?n?",
                        &n1, &n2, &n3, &n4, &n5) != 0) {
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

    if (svnproto_unpack(ctx, &ctx->in, "n*", &ar) != 0) {
        assert(0);
    }

    array_traverse(&ar, (array_traverser_t)dump_long, NULL);
    array_fini(&ar);

    svnc_close(ctx);


    if (svnc_debug_open(ctx, "testword") != 0) {
        assert(0);
    }

    if (svnproto_unpack(ctx, &ctx->in, "wwwwwww",
                        &s1, &s2, &s3, &s4, &s5, &s6, &s7, &s8) != 0) {
        assert(0);
    }

    TRACE("s1=%s s2=%s s3=%s s4=%s s5=%s s6=%s s7=%s",
          s1, s2, s3, s4, s5, s6, s7);

    svnc_close(ctx);


    if (svnc_debug_open(ctx, "testword") != 0) {
        assert(0);
    }

    if (svnproto_unpack(ctx, &ctx->in, "wwwwwwww?",
                        &s1, &s2, &s3, &s4, &s5, &s6, &s7, &s8) != 0) {
        assert(0);
    }

    TRACE("s1=%s s2=%s s3=%s s4=%s s5=%s s6=%s s7=%s s8=%s",
          s1, s2, s3, s4, s5, s6, s7, s8);

    svnc_close(ctx);


    if (svnc_debug_open(ctx, "testword") != 0) {
        assert(0);
    }

    if (svnproto_unpack(ctx, &ctx->in, "wwwwwwww?n?",
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

    if (svnproto_unpack(ctx, &ctx->in, "w*n?",
                        &ar, &n1) != 0) {
        assert(0);
    }

    array_traverse(&ar, (array_traverser_t)dump_string, NULL);
    array_fini(&ar);
    TRACE("n1=%ld", n1);

    svnc_close(ctx);


    if (svnc_debug_open(ctx, "teststring") != 0) {
        assert(0);
    }

    init_bytes_array(&ar);
    res = svnproto_unpack(ctx, &ctx->in, "S*", str_cb, &ar);

    if (res == PARSE_EOD) {
        res = 0;
    }
    if (res != 0) {
        assert(0);
    }

    dump_bytes_array(&ar);
    fini_bytes_array(&ar);

    if (svnproto_unpack(ctx, &ctx->in, "(w)", &s1) != 0) {
        assert(0);
    }

    TRACE("s=%s", s1);

    svnc_close(ctx);

    svnc_destroy(ctx);
    free(ctx);
}

static int
my_unpack_cb(svnc_ctx_t *ctx, UNUSED svnproto_state_t *st, UNUSED void *udata)
{
    int res;
    long n1 = 0;
    mnarray_t ar;

    init_string_array(&ar);
    res = svnproto_unpack(ctx, &ctx->in, "(nw*)", &n1, &ar);
    TRACE("n1=%ld", n1);
    dump_string_array(&ar);
    array_fini(&ar);

    if (res != 0) {
        return PARSE_EOD;
    }
    return res;
}

UNUSED static void
test_unpack_cb(void)
{
    svnc_ctx_t *ctx;
    mnarray_t ar;

    if ((ctx = svnc_new("svn://localhost/mysvn", "qwe", 0, 0))
        == NULL) {

        assert(0);
    }


    if (svnc_debug_open(ctx, "testlist") != 0) {
        assert(0);
    }

    init_string_array(&ar);
    if (svnproto_unpack(ctx, &ctx->in,
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

    dump_string_array(&ar);
    array_fini(&ar);

    svnc_close(ctx);

    svnc_destroy(ctx);
    free(ctx);
}

UNUSED static void
test_pack(void)
{
    svnc_ctx_t *ctx;

    if ((ctx =
            svnc_new("svn://localhost/mysvn", "qwe", 0, 0)) == NULL) {

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
    free(ctx);
}

static int
my_caps(UNUSED svnc_ctx_t *ctx,
        mnbytestream_t *out,
        UNUSED void *st,
        UNUSED void *udata)
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
        if (pack_word(out, strlen(words[i]), words[i]) != 0) {
            assert(0);
        }
    }

    return (0);
}

static int
my_response(UNUSED svnc_ctx_t *ctx,
            mnbytestream_t *out,
            UNUSED void *st,
            UNUSED void *udata)
{
    if (pack_number(out, 2) != 0) {
        assert(0);
    }

    if (pack_list(out, my_caps, NULL, NULL) != 0) {
        assert(0);
    }

    if (pack_string(out, strlen("svn://test/"), "svn://test/") != 0) {
        assert(0);
    }

    if (pack_string(out, strlen("SVN/9.8.7"), "SVN/9.8.7") != 0) {
        assert(0);
    }

    if (pack_list(out, NULL, NULL, NULL) != 0) {
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
            svnc_new("svn://localhost/mysvn", "qwe", 0, 0)) == NULL) {

        assert(0);
    }

    if (svnc_debug_open(ctx, "testnumber") != 0) {
        assert(0);
    }

    if (pack_list(&ctx->out, my_response, NULL, NULL) != 0) {
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
    free(ctx);
}

UNUSED static void
test_simple(void)
{
    svnc_ctx_t *ctx;
    long rev = 0;
    int kind = -1;
    mnarray_t dirents;
    mnarray_iter_t it;
    svnc_dirent_t *de;
    svnc_fileent_t fe;

    if ((ctx =
            svnc_new("svn://localhsot/mysvn", "qwe", 0, 0)) == NULL) {

        assert(0);
    }

    if (svnc_debug_open(ctx, "testsuccess") != 0) {
        assert(0);
    }

    if (svnproto_setup(ctx) != 0) {
        assert(0);
    }

    assert(ctx->get_latest_rev !=  NULL);
    if (ctx->get_latest_rev(ctx, &rev) != 0) {
        assert(0);
    }

    TRACE("rev=%ld", rev);

    assert(ctx->check_path != NULL);
    if (ctx->check_path(ctx, "", rev, &kind) != 0) {
        assert(0);
    }

    TRACE("kind=%s", svnc_kind2str(kind));

    svnproto_init_dirent_array(&dirents);

    if (svnproto_get_dir(ctx, "", rev, &dirents) != 0) {
        assert(0);
    }

    svnproto_dump_dirent_array(&dirents);

    if ((de = array_first(&dirents, &it)) == NULL) {
        assert(0);
    }

    svnc_fileent_init(&fe);
    assert(ctx->get_file != NULL);
    if (ctx->get_file(ctx, de->name->data, de->rev,
                          GETFLAG_WANT_PROPS | GETFLAG_WANT_CONTENTS,
                          &fe) != 0) {
        assert(0);
    }
    svnc_fileent_dump(&fe);
    svnc_fileent_fini(&fe);

    array_fini(&dirents);

    svnc_close(ctx);
    svnc_destroy(ctx);
    free(ctx);
}

static int
walk_cb(svnc_ctx_t *ctx,
        UNUSED const char *dir,
        UNUSED svnc_dirent_t *de,
        const char *path,
        svnc_fileent_t *fe,
        void *udata)
{
    struct stat sb;
    char *localpath;
    const char *localroot = udata;

    if ((localpath = path_join(localroot, path)) == NULL) {
        FAIL("path_join");
    }

    if (fe != NULL) {
        LTRACE(1, FGREEN("FILE %s -> %s (%s)"),
                     path, localpath, fe->checksum->data);

        svnc_save_checksum(ctx, path, fe->checksum);

        if (lstat(localpath, &sb) != 0) {
            int fd;
            svnc_fileent_t ffe;
            mnbytes_t **chunk;
            mnarray_iter_t it;

            if ((fd = open(localpath, O_RDWR|O_CREAT)) < 0) {
                perror("open");
                return 1;
            }

            /* get-file */
            svnc_fileent_init(&ffe);

            assert(ctx->get_file != NULL);
            if (ctx->get_file(ctx, path, fe->rev,
                                  GETFLAG_WANT_CONTENTS, &ffe) != 0) {

                TRACE(FRED("Failed to get a file"));
                svnc_fileent_fini(&ffe);
                return 1;
            }


            for (chunk = array_first(&ffe.contents, &it);
                 chunk != NULL;
                 chunk = array_next(&ffe.contents, &it)) {
                if (write(fd, (*chunk)->data, (*chunk)->sz) < 0) {
                    perror("write");
                    svnc_fileent_fini(&ffe);
                    return 1;
                }
            }
            svnc_fileent_fini(&ffe);

        } else {
            if (!S_ISREG(sb.st_mode)) {
                TRACE(FRED("Not a file"));
                return 1;
            } else {
                /* check sum */
            }
        }
    } else {

        LTRACE(1, FYELLOW("DIR %s"), path);

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
    return 0;
}

UNUSED static void
test_conn2(void)
{
    UNUSED int res;
    svnc_ctx_t *ctx;
    struct stat sb;
    const char *localroot = "qwe";
    char *p = NULL;
    mnbytes_t *c = NULL;
    int i;

    if ((ctx =
            svnc_new("svn://localhost/mysvn", localroot, 0, 0)) == NULL) {
        assert(0);
    }

    if (svnc_connect(ctx) != 0) {
        assert(0);
    }

    mkdir(localroot, 0755);

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

    for (i = svnc_first_checksum(ctx, &p, &c);
         i == 0;
         i = svnc_next_checksum(ctx, &p, &c)) {

        TRACE("deleting %s %s", p, BDATA(c));

        if ((res = svnc_delete_checksum(ctx, p)) != 0) {
            TRACE("res=%s", diag_str(res));
        }

        free(p);
        p = NULL;
        free(c);
        c = NULL;

    }

    svnc_close(ctx);
    svnc_destroy(ctx);
    free(ctx);
}

UNUSED static void
test_editor(void)
{
    svnc_ctx_t *ctx;

    if ((ctx =
            svnc_new("svn://localhost/mysvn", "qwe", 0, 0)) == NULL) {

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
    free(ctx);
}

UNUSED static void
test_mkdirs(void)
{
    //svncdir_mkdirs("");
    //svncdir_mkdirs(".");
    //svncdir_mkdirs("./.");
    //svncdir_mkdirs(".//.");
    //svncdir_mkdirs("..//..");
    //svncdir_mkdirs("/..//..");
    //svncdir_mkdirs("/..//../");
    //svncdir_mkdirs("a");
    //svncdir_mkdirs("a/b");
    //svncdir_mkdirs("a/b/c");
    //svncdir_mkdirs("a//b");
    //svncdir_mkdirs("a//b//c");
    //svncdir_mkdirs(".a//.b");
    //svncdir_mkdirs(".a//.b//.c");
    //svncdir_mkdirs("/home/mkushnir/tmp/aa//bb");
    //svncdir_mkdirs("/aa//bb//c");
    //svncdir_mkdirs("/aa//bb/");
    //svncdir_mkdirs("/aa//bb/c");
    //svncdir_mkdirs("/aa//bb/cc//");
    //svncdir_mkdirs("/aa/bb/cc/");
}

UNUSED static void
test_get_file(void)
{
    svnc_ctx_t *ctx;
    svnc_fileent_t fe;

    if ((ctx =
            svnc_new("svn://localhost/mysvn", "qwe", 0, 0)) == NULL) {
        assert(0);
    }

    if (svnc_connect(ctx) != 0) {
        assert(0);
    }

    svnc_fileent_init(&fe);
    assert(ctx->get_file != NULL);
    ctx->get_file(ctx, "qweqwe", 12, GETFLAG_WANT_CONTENTS|GETFLAG_WANT_PROPS, &fe);
    ctx->get_file(ctx, "qweqwe", 12, GETFLAG_WANT_CONTENTS|GETFLAG_WANT_PROPS, &fe);
    ctx->get_file(ctx, "qweqwe", 12, GETFLAG_WANT_CONTENTS|GETFLAG_WANT_PROPS, &fe);
    ctx->get_file(ctx, "qweqwe", 12, GETFLAG_WANT_CONTENTS|GETFLAG_WANT_PROPS, &fe);
    svnc_fileent_fini(&fe);

    svnc_close(ctx);
    svnc_destroy(ctx);
    free(ctx);

}

static int
mychunkcb(mnhttp_ctx_t *ctx,
            UNUSED mnbytestream_t *in,
            UNUSED void *udata)
{
    TRACE("%ld-%ld=%ld", ctx->current_chunk.end, ctx->current_chunk.start,
          ctx->current_chunk.end - ctx->current_chunk.start);
    return 0;
}


UNUSED static void
test_http_simple(void)
{
    int res = 0;

    svnc_ctx_t *ctx;
    if ((ctx =
            svnc_new("http://localhost/mysvn", "qwe", 0, 0)) == NULL) {
        assert(0);
    }
    if (svnc_debug_open(ctx, "testhttpsimple") != 0) {
        assert(0);
    }

    if (http_start_request(&ctx->out, "GET", "/") != 0) {
        assert(0);
    }

    if (http_add_header_field(&ctx->out, "Host", "localhost") != 0) {
        assert(0);
    }

    if (http_end_of_header(&ctx->out) != 0) {
        assert(0);
    }

    if (http_add_body(&ctx->out, "This is the test",
                      strlen("This is the test")) != 0) {
        assert(0);
    }

    if (bytestream_produce_data(&ctx->out, (void *)(intptr_t)ctx->fd) != 0) {
        assert(0);
    }

    bytestream_rewind(&ctx->out);

    res = http_parse_response(ctx->fd, &ctx->in, NULL, NULL, mychunkcb, NULL);
    TRACE("res=%s", diag_str(res));


    svnc_close(ctx);
    svnc_destroy(ctx);
    free(ctx);
}

static int
mybigbodycb(mnhttp_ctx_t *ctx,
            UNUSED mnbytestream_t *in,
            UNUSED void *udata)
{
    TRACE("%ld-%ld=%ld", ctx->body.end, ctx->body.start,
          ctx->body.end - ctx->body.start);
    TRACE("%ld-%ld=%ld", ctx->current_chunk.end, ctx->current_chunk.start,
          ctx->current_chunk.end - ctx->current_chunk.start);
    return 0;
}


UNUSED static void
test_http_bigbody(void)
{
    int res = 0;

    svnc_ctx_t *ctx;
    if ((ctx =
            svnc_new("http://localhost/mysvn", "qwe", 0, 0)) == NULL) {
        assert(0);
    }
    if (svnc_debug_open(ctx, "testhttpbigbody") != 0) {
        assert(0);
    }

    if (http_start_request(&ctx->out, "GET", "/") != 0) {
        assert(0);
    }

    if (http_add_header_field(&ctx->out, "Host", "localhost") != 0) {
        assert(0);
    }

    if (http_end_of_header(&ctx->out) != 0) {
        assert(0);
    }

    if (bytestream_produce_data(&ctx->out, (void *)(intptr_t)ctx->fd) != 0) {
        assert(0);
    }

    bytestream_rewind(&ctx->out);

    res = http_parse_response(ctx->fd, &ctx->in, NULL, NULL, mybigbodycb, NULL);
    TRACE("res=%s", diag_str(res));


}

UNUSED static void
test_conn3(void)
{
    svnc_ctx_t *ctx;
    long rev = 0;
    int kind = -1;

    if ((ctx =
            svnc_new("http://svn.freebsd.org/base/user/des/svnsup/bin/apply",
                     "qwe", 0, 0)) == NULL) {
        assert(0);
    }

    if (svnc_connect(ctx) != 0) {
        assert(0);
    }

    assert(ctx->get_latest_rev != NULL);
    if (ctx->get_latest_rev(ctx, &rev) != 0) {
        assert(0);
    }

    TRACE("rev=%ld", rev);

    assert(ctx->check_path != NULL);
    if (ctx->check_path(ctx, "", rev, &kind) != 0) {
        assert(0);
    }

    TRACE("kind=%s", svnc_kind2str(kind));

    svnc_close(ctx);
    svnc_destroy(ctx);
    free(ctx);

}

UNUSED static void
test_get_file_http(void)
{
    svnc_ctx_t *ctx;
    svnc_fileent_t fe;

    if ((ctx =
            svnc_new("http://localhost:8001/repos/mysvn", "qwe", 0, 0)) == NULL) {
            //svnc_new("svn://localhost/mysvn", "qwe", 0, 0)) == NULL) {
        assert(0);
    }

    if (svnc_connect(ctx) != 0) {
        assert(0);
    }

    assert(ctx->get_file != NULL);
    svnc_fileent_init(&fe);
    ctx->get_file(ctx, "asd/ASDASD", 58, GETFLAG_WANT_CONTENTS|GETFLAG_WANT_PROPS, &fe);
    //ctx->get_file(ctx, "qweqwe", 12, GETFLAG_WANT_CONTENTS|GETFLAG_WANT_PROPS, &fe);
    //ctx->get_file(ctx, "qweqwe", 12, GETFLAG_WANT_CONTENTS|GETFLAG_WANT_PROPS, &fe);
    //ctx->get_file(ctx, "qweqwe", 12, GETFLAG_WANT_CONTENTS|GETFLAG_WANT_PROPS, &fe);
    svnc_fileent_dump(&fe);
    svnc_fileent_fini(&fe);

    svnc_fileent_init(&fe);
    ctx->get_file(ctx, "asd/ERT", 58, GETFLAG_WANT_CONTENTS|GETFLAG_WANT_PROPS, &fe);
    svnc_fileent_dump(&fe);
    svnc_fileent_fini(&fe);

    svnc_close(ctx);
    svnc_destroy(ctx);
    free(ctx);

}

int
main(void)
{
    //test_parse_url();
    //test_utlencode();
    //test_utldecode();
    //test_unpack();
    //test_unpack_cb();
    //test_pack();
    //test_packresponse();
    //test_cache();
    //
    //test_http_simple();
    //test_http_bigbody();

    //test_conn2();
    //test_conn3();
    //test_get_file();
    test_get_file_http();

    /* broken, need to fix test data */
    //test_simple();
    //test_editor();
    return 0;
}
