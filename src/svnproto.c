#include <assert.h>
#include <string.h>
#include <sys/types.h>

#include "diag.h"
#include "mrkcommon/util.h"
//#define TRRET_DEBUG
#include "mrkcommon/dumpm.h"

#include "mrksvnup/svnc.h"
#include "mrksvnup/svnproto.h"

static const char *kinds[] = {
    "none",
    "file",
    "dir",
    "unknown",
};

int
svnproto_kind2int(const char *kind)
{
    unsigned i;

    if (kind != NULL) {
        for (i = 0; i < countof(kinds); ++i) {
            if (strcmp(kind, kinds[i]) == 0) {
                return i;
            }
        }
    }

    return SVNP_KIND_UNKNOWN;
}

const char *
svnproto_kind2str(int kind)
{
    if (kind < 0 || kind > SVNP_KIND_UNKNOWN) {
        kind = SVNP_KIND_UNKNOWN;
    }
    return kinds[kind];
}

/*
 * command-response:   ( success params:list )
 *                   | ( failure ( err:error ... ) )
 *                   | ( step [ token:string ] )
 * error: ( apr-err:number message:string file:string line:number )
 */

static int
unpack1(svnc_ctx_t *ctx,
        bytestream_t *in,
        UNUSED svnproto_state_t *st,
        UNUSED void *udata)
{
    int res;

    res = svnproto_unpack(ctx, in, "(nssn)",
                          &ctx->last_error.apr_error,
                          &ctx->last_error.message,
                          &ctx->last_error.file,
                          &ctx->last_error.line);

    if (BDATA(ctx->last_error.message) != NULL) {
        //TRACE(FRED("E %s (code %ld file '%s' line %ld)"),
        //           BDATA(ctx->last_error.message),
        //           ctx->last_error.apr_error,
        //           BDATA(ctx->last_error.file),
        //           ctx->last_error.line
        //           );
    }
    if (res != 0) {
        res = PARSE_EOD;
    }
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

    if ((res = svnproto_unpack(ctx, &ctx->in, "(w", &status)) != 0) {
        res = SVNPROTO_COMMAND_RESPONSE + 1;
        goto END;
    }

    //TRACE("status=%s", status);

    if (status == NULL || strcmp(status, "success") != 0) {
        res = SVNPROTO_COMMAND_RESPONSE + 2;
        goto TESTFAILURE;
    }

    va_start(ap, spec);
    if ((res = svnproto_vunpack(ctx, &ctx->in, spec, ap)) != 0) {
        res = SVNPROTO_COMMAND_RESPONSE + 3;
        goto END;
    }
    va_end(ap);

    if ((res = svnproto_unpack(ctx, &ctx->in, ")")) != 0) {
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

    if ((res = svnproto_unpack(ctx, &ctx->in, "(r)", unpack1, NULL)) != 0) {
        res = SVNPROTO_COMMAND_RESPONSE + 7;
        goto END;
    }

    if ((res = svnproto_unpack(ctx, &ctx->in, ")")) != 0) {
        res = SVNPROTO_COMMAND_RESPONSE + 8;
        goto END;
    }

    goto END;

}

