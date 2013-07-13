#ifndef HTTPPROTO_H
#define HTTPPROTO_H

#include "mrksvnup/svnc.h"

#ifdef __cplusplus
extern "C" {
#endif

int httpproto_setup(svnc_ctx_t *);
int httpproto_set_path(svnc_ctx_t *,
                       const char *,
                       long,
                       const char *,
                       svn_depth_t,
                       long);
int httpproto_finish_report(UNUSED svnc_ctx_t *);
int httpproto_get_file(svnc_ctx_t *,
                       const char *,
                       long,
                       int,
                       svnc_fileent_t *);
int httpproto_editor(svnc_ctx_t *);
int httpproto_update(svnc_ctx_t *,
                     long,
                     const char *,
                     svn_depth_t,
                     long,
                     svnc_cb_t,
                     void *);

int httpproto_check_path(svnc_ctx_t *,
                         const char *,
                         long,
                         int *);
int httpproto_get_latest_rev(svnc_ctx_t *,
                             long *);

#ifdef __cplusplus
}
#endif

#endif
