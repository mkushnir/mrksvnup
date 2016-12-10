#ifndef DAV_H
#define DAV_H

#include <bsdxml.h>

#include "mrkcommon/array.h"

#include "mrksvnup/svnc.h"
#include "mrksvnup/xmatch.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct _dav_ctx {
    XML_Parser p;
    long youngest_rev;
    char *me;
    char *reproot;
    char *revroot;
    xmatch_t xmatch;
    int match_result;

    /* update params */
    long source_rev;
    long target_rev;
    svn_depth_t depth;

    long flags;
    mnarray_t cwp;

    /* xml parser context */
#define XPS_SET_PROP 1
    int xml_parser_state;
    mnbytes_t *set_prop_name;
    mnbytes_t *text_checksum;

    /* ad hoc weak ref */
    const char *target;
    /* ad hoc weak ref to svnc_ctx_t */
    svnc_ctx_t *svnctx;
    /* ad hoc weak ref to svnc_fileent_t */
    svnc_fileent_t *fe;

} dav_ctx_t;

typedef struct _dav_xml_cb {
    XML_StartNamespaceDeclHandler ns_start;
    XML_EndNamespaceDeclHandler ns_end;
    XML_StartElementHandler el_start;
    XML_EndElementHandler el_end;
    XML_CharacterDataHandler chardata;
} dav_xml_cb_t;

void debug_ns_start(void *, const XML_Char *, const XML_Char *);
void debug_ns_end(void *, const XML_Char *);
void debug_el_start(void *, const XML_Char *, const XML_Char **);
void debug_el_end(void *, const XML_Char *);
void debug_chardata(void *, const XML_Char *, int);
void pattern_match_el_start(void *, const XML_Char *, const XML_Char **);
void pattern_match_el_end(void *, const XML_Char *);

dav_ctx_t *dav_ctx_new(void);
void dav_ctx_destroy(dav_ctx_t *);
void dav_setup_xml_parser(dav_ctx_t *, dav_xml_cb_t *, void *, const char *);
void dav_cwp_enter(dav_ctx_t *, const char *);
void dav_cwp_leave(dav_ctx_t *);
mnbytes_t *dav_cwp(dav_ctx_t *);
char *dav_rvr_path(dav_ctx_t *, const char *, long);

#ifdef __cplusplus
}
#endif

#endif
