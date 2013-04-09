#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <zlib.h>

#include "diag.h"
#include "mrkcommon/array.h"
#include "mrkcommon/util.h"
//#define TRRET_DEBUG
#include "mrkcommon/dumpm.h"

#include "mrksvnup/svnc.h"
#include "mrksvnup/svnproto.h"
#include "mrksvnup/svndiff.h"

/* based on libsvn_delta/svndiff.c */
#define MAX_ENCODED_INT_LEN 10

static const char *
decode_version(const char *start, const char *end, char *n)
{
    if (end - start < 4) {
        TRRETNULL(DECODE_VERSION + 1);
    }
    if (!(start[0] == 'S' &&
          start[1] == 'V' &&
          start[2] == 'N')) {

        TRRETNULL(DECODE_VERSION + 2);
    }

    *n = *(start + 3);

    return start + 4;
}

static const char *
decode_number(const char *start, const char *end, long *val)
{
    *val = 0;
    unsigned char c;

    end = MIN(end, start + MAX_ENCODED_INT_LEN);
    //TRACE("start=%p end=%p", start, end);

    for (c = *start; start < end; c = *start)
    {
        *val = (*val << 7) | (c & 0x7f);
        if (c < 0x80)
        {
            ++start;
            return start;
        }
        ++start;
    }
    return NULL;
}

static const char *
decode_insn(const char *start, const char *end, svndiff_insn_t *insn)
{
    unsigned char c;

    c = *(unsigned char *)start++;
    insn->code = (c >> 6) & 0x03;
    insn->len = c & 0x3f;

    if (insn->len == 0) {
        /* find size next to insncode */
        if ((start = decode_number(start, end, &insn->len)) == NULL) {
            TRRETNULL(DECODE_INSN + 1);
        }
    }

    if (insn->code != SVN_TXDELTA_NEW) {
        /* only source and target contain offset */
        if ((start = decode_number(start, end, &insn->offset)) == NULL) {
            TRRETNULL(DECODE_INSN + 2);
        }
    }
    return start;
}

static const char *
decode_bytes(const char *start, const char *end,
             size_t encoded_len, bytes_t **out)
{
    long orig_len;
    const char *savedstart = start;

    //D8(start, 8);
    if ((start = decode_number(start, end, &orig_len)) == NULL) {
        TRRETNULL(DECODE_BYTES + 1);

    }
    //TRACE("orig_len=%ld", orig_len);
    encoded_len -= (start - savedstart);

    if ((*out = malloc(sizeof(bytes_t) + orig_len)) == NULL) {
        FAIL("malloc");
    }
    (*out)->sz = orig_len;


    if (encoded_len == (*out)->sz) {
        memcpy((*out)->data, start, (*out)->sz);

    } else {
        if (uncompress((unsigned char *)(*out)->data,
                       (unsigned long *)&(*out)->sz,
                       (const unsigned char *)start,
                       encoded_len) != Z_OK) {

            free(*out);
            *out = NULL;

            TRACE("encoded_len=%ld orig_len=%ld", encoded_len, orig_len);

            TRRETNULL(DECODE_BYTES + 2);
        }
    }
    start += encoded_len;

    return start;
}

static int
init_insn(svndiff_insn_t *insn)
{
    insn->code = -1;
    insn->offset = -1;
    insn->len = -1;
    return 0;
}

static int
dump_insn(svndiff_insn_t *insn, UNUSED void *udata)
{
    TRACE("from %s %ld/%ld",
          (insn->code == SVN_TXDELTA_SOURCE ? "source" :
           insn->code == SVN_TXDELTA_TARGET ? "target" :
           insn->code == SVN_TXDELTA_NEW ? "new" :
           "<unknown>"), insn->offset, insn->len);
    return 0;
}


static void
init_insn_array(array_t *ar)
{
    if (array_init(ar, sizeof(svndiff_insn_t), 0,
                   (array_initializer_t)init_insn,
                   NULL) != 0) {
        FAIL("array_init");
    }
}

static void
fini_insn_array(array_t *ar)
{
    if (array_fini(ar) != 0) {
        FAIL("array_fini");
    }
}

static void
dump_insn_array(array_t *ar)
{
    array_traverse(ar, (array_traverser_t)dump_insn, NULL);
}

static int
init_wnd(svndiff_wnd_t *wnd)
{
    wnd->sview_offset = 0;
    wnd->sview_len = 0;
    wnd->tview_len = 0;
    wnd->inslen = 0;
    wnd->newlen = 0;
    wnd->orig_inslen = 0;
    init_insn_array(&wnd->insns);
    wnd->bytes = NULL;
    wnd->tview = NULL;
    return 0;
}

static int
fini_wnd(svndiff_wnd_t *wnd)
{
    wnd->sview_offset = 0;
    wnd->sview_len = 0;
    wnd->tview_len = 0;
    wnd->inslen = 0;
    wnd->newlen = 0;
    wnd->orig_inslen = 0;
    fini_insn_array(&wnd->insns);
    if (wnd->bytes != NULL) {
        free(wnd->bytes);
        wnd->bytes = NULL;
    }
    if (wnd->tview != NULL) {
        free(wnd->tview);
        wnd->tview = NULL;
    }
    return 0;
}

static int
dump_wnd(svndiff_wnd_t *wnd, UNUSED void *udata)
{
    TRACE("wnd sview_offset=%ld sview_len=%ld "
          "tview_len=%ld inslen=%ld "
          "orig_inslen=%ld newlen=%ld ",
          wnd->sview_offset,
          wnd->sview_len,
          wnd->tview_len,
          wnd->inslen,
          wnd->orig_inslen,
          wnd->newlen
         );
    dump_insn_array(&wnd->insns);
    return 0;
}

static void
init_wnd_array(array_t *ar)
{
    if (array_init(ar, sizeof(svndiff_wnd_t), 0,
                   (array_initializer_t)init_wnd,
                   (array_finalizer_t)fini_wnd) != 0) {
        FAIL("array_init");
    }
}

static void
fini_wnd_array(array_t *ar)
{
    if (array_fini(ar) != 0) {
        FAIL("array_fini");
    }
}

static void
dump_wnd_array(array_t *ar)
{
    array_traverse(ar, (array_traverser_t)dump_wnd, NULL);
}

int
svndiff_doc_init(svndiff_doc_t *doc)
{
    doc->version = -1;
    doc->parse_state = SD_STATE_VERSION;
    doc->base_checksum = NULL;
    doc->rp = NULL;
    doc->lp = NULL;
    doc->rev = -1;
    doc->ft = NULL;
    doc->fd = -1;
    doc->mod = 0644;
    init_wnd_array(&doc->wnd);
    if (array_init(&doc->txdelta, sizeof(char), 0, NULL, NULL) != 0) {
        FAIL("array_init");
    }
    doc->flags = 0;
    return 0;
}

static void
clear_current_file(svndiff_doc_t *doc)
{
    if (doc->rp != NULL) {
        free(doc->rp);
        doc->rp = NULL;
    }
    if (doc->lp != NULL) {
        free(doc->lp);
        doc->lp = NULL;
    }
    doc->rev = -1;
    if (doc->ft != NULL) {
        free(doc->ft);
        doc->ft = NULL;
    }
    if (doc->fd != -1) {
        close(doc->fd);
        doc->fd = -1;
    }
}

int
svndiff_doc_fini(svndiff_doc_t *doc)
{
    doc->version = -1;
    doc->parse_state = SD_STATE_VERSION;
    if (doc->base_checksum != NULL) {
        free(doc->base_checksum);
        doc->base_checksum = NULL;
    }
    clear_current_file(doc);
    doc->mod = 0644;
    fini_wnd_array(&doc->wnd);
    if (array_fini(&doc->txdelta) != 0) {
        FAIL("array_fini");
    }
    doc->flags = 0;
    return 0;
}

int
svndiff_doc_dump(svndiff_doc_t *doc)
{
    TRACE("doc version=%d "
          "base_checksum=%s rp=%s rev=%ld lp=%s fd=%d mod=%04o",
          doc->version,
          BDATA(doc->base_checksum),
          BDATA(doc->rp),
          doc->rev,
          doc->lp,
          doc->fd,
          doc->mod
         );
    dump_wnd_array(&doc->wnd);
    return 0;
}

int
svndiff_parse_doc(const char *start,
                  const char *end,
                  svndiff_doc_t *doc)
{
    int res = 0;

    while (res == 0 && start < end) {

        //TRACE("state=%d", doc->parse_state);
        //D64(start, MIN(64, end-start));

        if (doc->parse_state == SD_STATE_VERSION) {
            if ((start = decode_version(start,
                                        end, &doc->version)) == NULL) {
                res = SVNDIFF_PARSE_DOC + 1;
            } else {
                doc->parse_state = SD_STATE_NWND;
            }

        } else if (doc->parse_state == SD_STATE_NWND) {

            if ((doc->current_wnd = array_incr(&doc->wnd)) == NULL) {
                FAIL("array_incr");
            }
            doc->parse_state = SD_STATE_SVO;

        } else if (doc->parse_state == SD_STATE_SVO) {
            if ((start = decode_number(start, end,
                 &doc->current_wnd->sview_offset)) == NULL) {

                res = SVNDIFF_PARSE_DOC + 2;
            } else {
                doc->parse_state = SD_STATE_SVL;
            }

        } else if (doc->parse_state == SD_STATE_SVL) {
            if ((start = decode_number(start, end,
                 &doc->current_wnd->sview_len)) == NULL) {

                res = SVNDIFF_PARSE_DOC + 3;
            } else {
                doc->parse_state = SD_STATE_TVL;
            }

        } else if (doc->parse_state == SD_STATE_TVL) {
            if ((start = decode_number(start, end,
                 &doc->current_wnd->tview_len)) == NULL) {

                res = SVNDIFF_PARSE_DOC + 4;
            } else {
                doc->parse_state = SD_STATE_INSL;
            }


        } else if (doc->parse_state == SD_STATE_INSL) {
            if ((start = decode_number(start, end,
                 &doc->current_wnd->inslen)) == NULL) {

                res = SVNDIFF_PARSE_DOC + 5;
            } else {
                doc->parse_state = SD_STATE_NEWL;
            }

        } else if (doc->parse_state == SD_STATE_NEWL) {
            if ((start = decode_number(start, end,
                 &doc->current_wnd->newlen)) == NULL) {

                res = SVNDIFF_PARSE_DOC + 6;
            } else {
                doc->parse_state = SD_STATE_INSNS;
            }

        } else if (doc->parse_state == SD_STATE_INSNS) {
            bytes_t *b = NULL;

            if ((start = decode_bytes(start,
                                      end,
                                      doc->current_wnd->inslen,
                                      &b)) == NULL) {

                res = SVNDIFF_PARSE_DOC + 7;

            } else {
                const char *instart = b->data;
                const char *insend = instart + b->sz;

                //D8(instart, b->sz);

                while (instart < insend) {

                    svndiff_insn_t probe = {-1, -1, -1}, *insn = NULL;

                    if ((instart = decode_insn(instart,
                                               insend,
                                               &probe)) == NULL) {
                        break;

                    } else {
                        if ((insn = array_incr(&doc->current_wnd->insns))
                             == NULL) {
                            FAIL("array_incr");
                        }

                        *insn = probe;
                    }
                }
                /* check */

                free(b);
                b = NULL;
            }


            //TRACE("start after decode_bytes: %p", start);
            doc->parse_state = SD_STATE_BYTES;

        } else if (doc->parse_state == SD_STATE_BYTES) {

            //TRACE("start before decode_bytes: %p", start);
            //if (doc->current_wnd->newlen != (end - start)) {
            //    TRACE("newlen=%ld avail=%ld", doc->current_wnd->newlen, (end - start));
            //}

            if ((start = decode_bytes(start, end,
                                      doc->current_wnd->newlen,
                                      &doc->current_wnd->bytes)) == NULL) {

                res = SVNDIFF_PARSE_DOC + 8;

            } else {
                //TRACE("start after decode_bytes: %p", start);
                doc->parse_state = SD_STATE_NWND;
            }

        } else {
            break;
        }
    }

    TRRET(res);

}

int
svndiff_build_tview(svndiff_wnd_t *wnd, svndiff_doc_t *doc)
{
    int res = 0;
    array_iter_t it;
    svndiff_insn_t *insn;
    char *ptbuf;
    ssize_t navail;
    char *pbytes;

    /* Set up target */
    if ((wnd->tview = malloc(wnd->tview_len)) == NULL) {
        FAIL("malloc");
    }
    ptbuf = wnd->tview;
    navail = wnd->tview_len;


    pbytes = wnd->bytes->data;

    /* Set up and verify source */

    //TRACE("+ %s", BDATA(doc->rp));

    for (insn = array_first(&wnd->insns, &it);
         insn != NULL;
         insn = array_next(&wnd->insns, &it)) {

        if (insn->len > navail) {
            res = SVNDIFF_BUILD_TVIEW + 1;
            goto END;
        }

        if (insn->code == SVN_TXDELTA_SOURCE) {
            ssize_t nread;

            /* read len bytes from source at offset */
            if ((nread = pread(doc->fd, ptbuf, insn->len,
                               insn->offset + wnd->sview_offset)) < 0) {
                /* add-file was combined with source insn? */
                perror("pread");
                res = SVNDIFF_BUILD_TVIEW + 2;
                goto END;
            }
            if (nread < insn->len) {
                /* corrupt insn? */
                res = SVNDIFF_BUILD_TVIEW + 3;
                goto END;
            }

            //TRACE("Applied from source:");
            //D32(ptbuf, MIN(insn->len, 64));
            //if (insn->len > 64) {
            //    D32(ptbuf + insn->len - 64, 64);
            //}

            ptbuf += insn->len;
            navail -= insn->len;

        } else if (insn->code == SVN_TXDELTA_NEW) {
            /* read len bytes from new at offset */
            if (((ssize_t)wnd->bytes->sz) - (pbytes - wnd->bytes->data) <
                insn->len) {

                /* corrupt insn? */
                res = SVNDIFF_BUILD_TVIEW + 4;
                goto END;
            }

            memcpy(ptbuf, pbytes, insn->len);
            pbytes += insn->len;

            //TRACE("Applied from new:");
            //D32(ptbuf, MIN(insn->len, 64));
            //if (insn->len > 64) {
            //    D32(ptbuf + insn->len - 64, 64);
            //}

            ptbuf += insn->len;
            navail -= insn->len;

        } else if (insn->code == SVN_TXDELTA_TARGET) {
            /* read len bytes from new at offset */
            char *sbuf = wnd->tview + insn->offset;
            char *sbuf_end = sbuf + insn->len;

            /* validation */

            if ((insn->offset + insn->len) >= wnd->tview_len) {
                TRACE("insn->offset + insn->len=%ld >= wnd->tview_len=%ld",
                       (insn->offset + insn->len), wnd->tview_len);
                res = SVNDIFF_BUILD_TVIEW + 5;
                goto END;
            }

            /* patterning copy in terms of libsvn_delta/text_delta.c */
            for (; sbuf < sbuf_end; ++sbuf, ++ptbuf) {
                *ptbuf = *sbuf;
            }

            navail -= insn->len;

        } else {
            res = SVNDIFF_BUILD_TVIEW + 6;
            svndiff_doc_dump(doc);
            goto END;
        }
    }


END:
    TRRET(res);
}

