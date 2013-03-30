#ifndef SVNC_PRIVATE_H
#define SVNC_PRIVATE_H

svnproto_state_t * svnproto_state_new(void);
void svnproto_state_destroy(svnproto_state_t *);

#include "mrksvnup/svnc.h"
#endif
