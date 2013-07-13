#ifndef SVNCDIR_H
#define SVNCDIR_H

#include "mrksvnup/svnc.h"
#include "mrksvnup/svnproto.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef int (*svncdir_cb_t) (svnc_ctx_t *,
                             const char *,
                             svnc_dirent_t *,
                             const char *,
                             svnc_fileent_t *,
                             void *);
int svncdir_walk(svnc_ctx_t *, const char *, svncdir_cb_t, void *udata);

int svncdir_mkdirs(const char *);

#ifdef __cplusplus
}
#endif

#endif
