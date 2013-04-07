#ifndef DAV_H
#define DAV_H

#include <bsdxml.h>

#include "mrkcommon/array.h"

#include "mrksvnup/svnc.h"
#include "mrksvnup/xmatch.h"

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
    /* weak ref */
    const char *target;
    long flags;
    /* weak ref to svnc_ctx_t */
    svnc_ctx_t *svnctx;
    array_t cwd;

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
void dav_setup_xml_parser(dav_ctx_t *, dav_xml_cb_t *, void *, const char *);
void dav_dir_enter(dav_ctx_t *, const char *);
void dav_dir_leave(dav_ctx_t *);
void dav_ctx_destroy(dav_ctx_t *);

#endif
