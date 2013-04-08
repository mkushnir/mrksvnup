#ifndef SVNEDIT_H
#define SVNEDIT_H

#include "mrksvnup/svnc.h"
#include "mrksvnup/svndiff.h"

int svnedit_verify_checksum(int, const bytes_t *);
int svnedit_init_shadow_ctx(svnc_ctx_t *);
void svnedit_close_shadow_ctx(void);
svndiff_doc_t * svnedit_clear_doc(void);
svndiff_doc_t * svnedit_get_doc(void);

int svnedit_target_rev(svnc_ctx_t *,
                       long);

int svnedit_open_root(svnc_ctx_t *,
                     long,
                     bytes_t *);

int svnedit_delete_entry(svnc_ctx_t *,
                         bytes_t *,
                         long,
                         bytes_t *);

int svnedit_add_dir(svnc_ctx_t *,
                    bytes_t *);

int svnedit_open_dir(svnc_ctx_t *,
                     bytes_t *);

int svnedit_add_file(svnc_ctx_t *);

int svnedit_open_file(svnc_ctx_t *);

int svnedit_change_file_prop(svnc_ctx_t *,
                             bytes_t *,
                             bytes_t *);

int svnedit_close_file(svnc_ctx_t *,
                       bytes_t *,
                       bytes_t *);

#endif
