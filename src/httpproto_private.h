#ifndef HTTPPROTO_PRIVATE_H
#define HTTPPROTO_PRIVATE_H

#include "mrksvnup/svnc.h"
struct _extra_header {
    const char *name;
    const char *value;
};

int dav_request(svnc_ctx_t *,
                const char *,
                const char *,
                svn_depth_t,
                const char *,
                size_t,
                const struct _extra_header[]);


#include "mrksvnup/httpproto.h"

#endif
