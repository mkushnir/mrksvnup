#ifndef HTTPPROTO_PRIVATE_H
#define HTTPPROTO_PRIVATE_H

#include "mrksvnup/svnc.h"

int dav_request(svnc_ctx_t *, const char *, const char *, svn_depth_t, const char *, size_t);


#include "mrksvnup/httpproto.h"

#endif
