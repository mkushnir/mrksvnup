#ifndef SVNEDIT_H
#define SVNEDIT_H

#include "mrksvnup/svnc.h"
#include "mrksvnup/svndiff.h"
#include "mrksvnup/svnproto_bytes.h"

int svnedit_verify_checksum(int, const svnproto_bytes_t *);
int svnedit_init(svnc_ctx_t *);
void svnedit_fini(void);
svndiff_doc_t * svnedit_clear_doc(void);
svndiff_doc_t * svnedit_get_doc(void);

int svnedit_target_rev(svnc_ctx_t *,
                       long);

int svnedit_open_root(svnc_ctx_t *,
                     long,
                     svnproto_bytes_t *);

int svnedit_delete_entry(svnc_ctx_t *,
                         svnproto_bytes_t *,
                         long,
                         svnproto_bytes_t *);

int svnedit_add_dir(svnc_ctx_t *,
                    svnproto_bytes_t *);

int svnedit_open_dir(svnc_ctx_t *,
                     svnproto_bytes_t *);

int svnedit_add_file(svnc_ctx_t *);

int svnedit_open_file(svnc_ctx_t *);

int svnedit_change_file_prop(svnc_ctx_t *,
                             svnproto_bytes_t *,
                             svnproto_bytes_t *);

int svnedit_close_file(svnc_ctx_t *,
                       svnproto_bytes_t *,
                       svnproto_bytes_t *);

#endif
