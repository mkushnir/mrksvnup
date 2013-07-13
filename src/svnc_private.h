#ifndef SVNC_PRIVATE_H
#define SVNC_PRIVATE_H

#ifdef __cplusplus
extern "C" {
#endif

svnproto_state_t * svnproto_state_new(void);
void svnproto_state_destroy(svnproto_state_t *);

#ifdef __cplusplus
}
#endif

#include "mrksvnup/svnc.h"
#endif
