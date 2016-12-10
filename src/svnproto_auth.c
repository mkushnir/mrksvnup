#include <assert.h>
#include <sys/types.h>

#include "diag.h"
#include "mrkcommon/array.h"
#include "mrkcommon/util.h"
//#define TRRET_DEBUG
#include "mrkcommon/dumpm.h"

#include "mrksvnup/svnproto.h"
#include "mrkcommon/bytestream.h"

#define SVNPROTO_ANON "ANONYMOUS"

/*
 * auth command-request: ( ( mech:word ... ) realm:string )
 */

/*
 * auth response: ( mech:word [ token:string ] )
 */
static int
auth_response(UNUSED svnc_ctx_t *ctx,
              mnbytestream_t *out,
              UNUSED void *v,
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
    mnarray_t auth_mechs;
    mnarray_iter_t it;
    char *realm = NULL;
    char **ss = NULL;

    init_string_array(&auth_mechs);

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
    if (realm != NULL) {
        free(realm);
        realm = NULL;
    }
    TRRET(res);
}

