#include <assert.h>
#include <string.h>
#include <sys/types.h>

#include "diag.h"
#include "mrkcommon/util.h"
//#define TRRET_DEBUG
#include "mrkcommon/dumpm.h"

#include "mrksvnup/svnc.h"
#include "mrksvnup/svnproto.h"

/*
 * command-response:   ( success params:list )
 *                   | ( failure ( err:error ... ) )
 *                   | ( step [ token:string ] )
 * error: ( apr-err:number message:string file:string line:number )
 */

static int
unpack1(svnc_ctx_t *ctx,
        mnbytestream_t *in,
        UNUSED svnproto_state_t *st,
        UNUSED void *udata)
{
    int res;

    res = svnproto_unpack(ctx, in, "(nssn)",
                          &ctx->last_error.apr_error,
                          &ctx->last_error.message,
                          &ctx->last_error.file,
                          &ctx->last_error.line);

    TRRET(res);
}

int
svnproto_command_response(svnc_ctx_t *ctx,
                          const char *spec,
                          ...)
{
    va_list ap;
    int res = 0;
    char *status = NULL;

    svnc_clear_last_error(ctx);

    res = svnproto_unpack(ctx, &ctx->in, "(w", &status);
    if (res != 0) {
        res = SVNPROTO_COMMAND_RESPONSE + 1;
        goto END;
    }

    //TRACE("status=%s", status);

    if (status == NULL || strcmp(status, "success") != 0) {
        res = SVNPROTO_COMMAND_RESPONSE + 2;
        goto TESTFAILURE;
    }

    va_start(ap, spec);
    res = svnproto_vunpack(ctx, &ctx->in, spec, ap);
    if (res != 0) {
        res = SVNPROTO_COMMAND_RESPONSE + 3;
        va_end(ap);
        goto END;
    }
    va_end(ap);

    res = svnproto_unpack(ctx, &ctx->in, ")");
    if (res != 0) {
        res = SVNPROTO_COMMAND_RESPONSE + 4;
        goto END;
    }
END:
    if (status != NULL) {
        free(status);
    }
    if (ctx->last_error.apr_error != -1) {
        res = SVNPROTO_COMMAND_RESPONSE + 5;
    }
    TRRET(res);

TESTFAILURE:

    if (status == NULL || strcmp(status, "failure") != 0) {
        res = SVNPROTO_COMMAND_RESPONSE + 6;
        goto END;
    }

    res = svnproto_unpack(ctx, &ctx->in, "(r*)", unpack1, NULL);
    if (res != 0) {
        res = SVNPROTO_COMMAND_RESPONSE + 7;
        goto END;
    }

    res = svnproto_unpack(ctx, &ctx->in, ")");
    if (res != 0) {
        res = SVNPROTO_COMMAND_RESPONSE + 8;
        goto END;
    }

    goto END;
}

/*
 * greeting
 *      params:     ( minver:number maxver:number mechs:list
 *                    ( cap:word ... )
 *                  )
 *
 *      response:   ( version:number ( cap:word ... ) url:string
 *                    ? ra-client:string ( ? client:string )
 *                  )
 */

static int
mycaps(UNUSED svnc_ctx_t *ctx,
       mnbytestream_t *out,
       UNUSED void *v,
       UNUSED void *data)
{
    unsigned i;
    const char *caps[] = {
        "edit-pipeline",
        "svndiff1",
        "absent-entries",
        "depth",
        "mergeinfo",
        "log-revprops",
    };

    for (i = 0; i < countof(caps); ++i) {
        if (pack_word(out, strlen(caps[i]), caps[i]) != 0) {
            TRRET(MYCAPS + 1);
        }
    }

    return 0;
}

static int
greeting_response(svnc_ctx_t *ctx,
                  mnbytestream_t *out,
                  UNUSED void *v,
                  UNUSED void *udata)
{
    if (pack_number(out, 2) != 0) {
        TRRET(GREETING_RESPONSE + 1);
    }

    if (pack_list(out, mycaps, NULL, NULL) != 0) {
        TRRET(GREETING_RESPONSE + 2);
    }

    if (pack_string(out, strlen(ctx->url), ctx->url) != 0) {
        TRRET(GREETING_RESPONSE + 3);
    }

    if (pack_string(out, strlen(RA_CLIENT), RA_CLIENT) != 0) {
        TRRET(GREETING_RESPONSE + 4);
    }

    if (pack_list(out, NULL, NULL, NULL) != 0) {
        TRRET(GREETING_RESPONSE + 5);
    }

    return 0;
}


int
svnproto_setup(svnc_ctx_t *ctx)
{
    int res = 0;
    long minver = -1, maxver = -1;
    mnarray_t server_mechs, server_caps, repo_caps;
    char *uuid = NULL, *repo_url = NULL;

    init_string_array(&server_mechs);
    init_string_array(&server_caps);
    init_string_array(&repo_caps);

    /* greeting request */
    if (svnproto_command_response(ctx, "(nn(w*)(w*))",
                                  &minver, &maxver,
                                  &server_mechs, &server_caps) != 0) {
        res = SVNPROTO_SETUP + 1;
        goto END;
    }

    //TRACE("%minver=%ld maxver=%ld", minver, maxver);
    //TRACE("server_mechs");
    //svnproto_dump_string_array(&server_mechs);
    //TRACE("server_caps");
    //svnproto_dump_string_array(&server_caps);

    if (! (minver == 2 && maxver == 2)) {
        res = SVNPROTO_SETUP + 2;
        goto END;
    }

    /* greeting response */
    if (pack_list(&ctx->out, greeting_response, ctx, NULL) != 0) {
        res = SVNPROTO_SETUP + 3;
        goto END;
    }

    if (bytestream_produce_data(&ctx->out, (void *)(intptr_t)ctx->fd) != 0) {
        res = SVNPROTO_SETUP + 4;
        goto END;
    }

    if (svnproto_check_auth(ctx) != 0) {
        res = SVNPROTO_SETUP + 5;
        goto END;
    }
    /* repos-info */
    if (svnproto_command_response(ctx, "(ss(w*))",
                                  &uuid, &repo_url, &repo_caps) != 0) {
        res = SVNPROTO_SETUP + 6;
        goto END;
    }

    //TRACE("uuid=%s repo_url=%s", uuid, repo_url);
    //svnproto_dump_string_array(&repo_caps);

END:
    bytestream_rewind(&ctx->out);

    array_fini(&server_mechs);
    array_fini(&server_caps);
    array_fini(&repo_caps);
    if (uuid != NULL) {
        free(uuid);
        uuid = NULL;
    }
    if (repo_url != NULL) {
        free(repo_url);
        repo_url = NULL;
    }
    TRRET(res);
}
