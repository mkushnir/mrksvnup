#include <assert.h>
#include <sys/types.h>

#include "diag.h"
#include "mrkcommon/array.h"
#include "mrkcommon/util.h"
//#define TRRET_DEBUG
#include "mrkcommon/dumpm.h"

#include "mrksvnup/svnproto.h"
#include "mrksvnup/bytestream.h"

#define SVNPROTO_ANON "ANONYMOUS"

/*
 * greeting command-request:
 *  ( minver:number maxver:number mechs:list ( cap:word ... ) )
 */

/*
 * greeting response:
 *  ( version:number ( cap:word ... ) url:string
 *             ? ra-client:string ( ? client:string ) )
 *
 */

static int
mycaps(UNUSED svnc_ctx_t *ctx,
       bytestream_t *out,
       UNUSED svnproto_state_t *v,
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
                  bytestream_t *out,
                  UNUSED svnproto_state_t *v,
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


/*
 * auth command-request: ( ( mech:word ... ) realm:string )
 */

/*
 * auth response: ( mech:word [ token:string ] )
 */
static int
auth_response(UNUSED svnc_ctx_t *ctx,
              bytestream_t *out,
              UNUSED svnproto_state_t *v,
              UNUSED void *udata)
{
    if (pack_word(out, strlen(SVNPROTO_ANON), SVNPROTO_ANON) != 0) {
        TRRET(AUTH_RESPONSE + 1);
    }

    if (pack_list(out, NULL, NULL, NULL) != 0) {
        TRRET(AUTH_RESPONSE + 2);
    }

    return 0;
}

/*
 * challenge: [ token:string ]
 */
static int
svnproto_check_challenge(svnc_ctx_t *ctx)
{
    int res = 0;
    char *token = NULL;

    if (svnproto_command_response(ctx, "(s?)", &token) != 0) {
        res = SVNPROTO_CHECK_CHALLENGE + 1;
        goto END;
    }

    //TRACE("token=%s", token);

END:
    if (token != NULL) {
        free(token);
    }

    TRRET(res);
}

int svnproto_check_auth(svnc_ctx_t *ctx)
{
    int res = 0;
    array_t auth_mechs;
    array_iter_t it;
    char *realm = NULL;
    char **ss = NULL;

    svnproto_init_string_array(&auth_mechs);

    /* auth request */
    if (svnproto_command_response(ctx, "((w*)s)", &auth_mechs, &realm) != 0) {
        res = SVNPROTO_CHECK_AUTH + 1;
        goto END;
    }

    //TRACE("realm=%s", realm);

    for (ss = array_first(&auth_mechs, &it);
         ss != NULL;
         ss = array_next(&auth_mechs, &it)) {

        //TRACE("mech=%s", *ss);

        if (strcmp(*ss, SVNPROTO_ANON) == 0) {
            ctx->flags |= SVNC_AUTH_MECH_OK;

            /* auth response */
            if (pack_list(&ctx->out, auth_response, ctx, NULL) != 0) {
                res = SVNPROTO_CHECK_AUTH + 2;
                goto END;
            }

            if (bytestream_produce_data(&ctx->out, ctx->fd) != 0) {
                res = SVNPROTO_CHECK_AUTH + 3;
                goto END;
            }

            bytestream_rewind(&ctx->out);

            if (svnproto_check_challenge(ctx) != 0) {
                res = SVNPROTO_CHECK_AUTH + 4;
                goto END;
            }
        }
    }

END:
    array_fini(&auth_mechs);
    TRRET(res);
}

int
svnproto_setup(svnc_ctx_t *ctx)
{
    int res = 0;
    long minver = -1, maxver = -1;
    array_t server_mechs, server_caps, repo_caps;
    char *uuid = NULL, *repo_url = NULL;

    svnproto_init_string_array(&server_mechs);
    svnproto_init_string_array(&server_caps);
    svnproto_init_string_array(&repo_caps);

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

    if (bytestream_produce_data(&ctx->out, ctx->fd) != 0) {
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
    bytestream_rewind(&ctx->in);
    bytestream_rewind(&ctx->out);

    array_fini(&server_mechs);
    array_fini(&server_caps);
    array_fini(&repo_caps);
    TRRET(res);
}
