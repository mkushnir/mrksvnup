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
//#include "mrksvnup/bytestream.h"
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

        TRRETNULL(DECODE_VERSION + 1);
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
    return start;
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
            TRRETNULL(SVNDIFF_DECODE_INSN + 1);
        }
    }

    if (insn->code != SVN_TXDELTA_NEW) {
        /* only source and target contain offset */
        if ((start = decode_number(start, end, &insn->offset)) == NULL) {
            TRRETNULL(SVNDIFF_DECODE_INSN + 2);
        }
    }
    return start;
}

static const char *
decode_bytes(const char *start, const char *end,
             size_t encoded_len, svnproto_bytes_t **out)
{
    long orig_len;
    const char *savedstart = start;

    if ((start = decode_number(start, end, &orig_len)) == NULL) {
        TRRETNULL(SVNDIFF_DECODE_BYTES + 1);

    }
    encoded_len -= (start - savedstart);

    if ((*out = malloc(sizeof(svnproto_bytes_t) + orig_len)) == NULL) {
        FAIL("malloc");
    }
    (*out)->sz = orig_len;

    //TRACE("encoded_len=%ld orig_len=%ld", encoded_len, orig_len);


    if (encoded_len == (*out)->sz) {
        memcpy((*out)->data, start, (*out)->sz);

    } else {
        if (uncompress((unsigned char *)(*out)->data,
                       (unsigned long *)&(*out)->sz,
                       (const unsigned char *)start,
                       encoded_len) != Z_OK) {

            free(*out);
            *out = NULL;
            TRRETNULL(SVNDIFF_DECODE_BYTES + 2);
        }
    }

    return start;
}

int
svndiff_parse_doc(const char *start,
                  const char *end,
                  svndiff_doc_t *doc)
{
    int res = 0;

    //D64(start, MIN(64, end-start));

    while (res == 0 && start < end) {

        if (doc->parse_state == SD_STATE_START) {
            if ((start = decode_version(start,
                                        end, &doc->version)) == NULL) {
                res = SVNDIFF_PARSE_DOC + 1;
            } else {
                //TRACE("doc version:%d", doc->version);
                doc->parse_state = SD_STATE_VERSION;
            }

        } else if (doc->parse_state == SD_STATE_VERSION) {
            if ((start = decode_number(start, end,
                                       &doc->sview_offset)) == NULL) {

                res = SVNDIFF_PARSE_DOC + 2;
            } else {
                //TRACE("doc sview_offset:%ld", doc->sview_offset);
                doc->parse_state = SD_STATE_SVO;
            }

        } else if (doc->parse_state == SD_STATE_SVO) {
            if ((start = decode_number(start, end,
                                       &doc->sview_len)) == NULL) {

                res = SVNDIFF_PARSE_DOC + 3;
            } else {
                //TRACE("doc sview_len:%ld", doc->sview_len);
                doc->parse_state = SD_STATE_SVL;
            }

        } else if (doc->parse_state == SD_STATE_SVL) {
            if ((start = decode_number(start, end,
                                       &doc->tview_len)) == NULL) {

                res = SVNDIFF_PARSE_DOC + 4;
            } else {
                //TRACE("doc tview_len:%ld", doc->tview_len);
                doc->parse_state = SD_STATE_TVL;
            }


        } else if (doc->parse_state == SD_STATE_TVL) {
            if ((start = decode_number(start, end,
                                       &doc->inslen)) == NULL) {

                res = SVNDIFF_PARSE_DOC + 5;
            } else {
                doc->inslen_check = 0;
                //TRACE("doc inslen:%ld", doc->inslen);
                doc->parse_state = SD_STATE_INSL;
            }

        } else if (doc->parse_state == SD_STATE_INSL) {
            if ((start = decode_number(start, end,
                                       &doc->newlen)) == NULL) {

                res = SVNDIFF_PARSE_DOC + 6;
            } else {
                //TRACE("doc newlen:%ld", doc->newlen);
                doc->parse_state = SD_STATE_NEWL;
            }

        } else if (doc->parse_state == SD_STATE_NEWL) {
            const char *saved_start;

            saved_start = start;
            if ((start = decode_number(start, end,
                                       &doc->orig_inslen)) == NULL) {
                res = SVNDIFF_PARSE_DOC + 7;
                break;
            }
            doc->inslen_check += start - saved_start;

            //TRACE("orig_inslen=%ld", doc->orig_inslen);

            while (doc->inslen_check < doc->inslen) {

                svndiff_insn_t probe = {-1, -1, -1}, *insn = NULL;

                saved_start = start;
                if ((start = decode_insn(start, end, &probe)) == NULL) {
                    break;

                } else {
                    if ((insn = array_incr(&doc->insns)) == NULL) {
                        FAIL("array_incr");
                    }

                    *insn = probe;
                    doc->inslen_check += start - saved_start;
                    //TRACE("insn added, inslen_check:%ld",
                    //      doc->inslen_check);
                }
            }
            /* end of insns ? */
            if (doc->inslen_check >= doc->inslen) {
                doc->parse_state = SD_STATE_INSNS;
            } else {
                res = SVNDIFF_PARSE_DOC + 8;
            }

        } else if (doc->parse_state == SD_STATE_INSNS) {
            svnproto_bytes_t **b = NULL;

            if ((b = array_incr(&doc->bytes)) == NULL) {
                FAIL("array_incr");
            }

            if ((start = decode_bytes(start, end,
                                      end - start, b)) == NULL) {

                array_decr(&doc->bytes);
                res = SVNDIFF_PARSE_DOC + 9;

            } else {
                doc->parse_state = SD_STATE_BYTES;
            }

        } else {
            break;
        }
    }

    TRRET(res);

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
    TRACE("code=%s offset=%ld len=%ld",
          (insn->code == SVN_TXDELTA_SOURCE ? "source" :
           insn->code == SVN_TXDELTA_TARGET ? "target" :
           insn->code == SVN_TXDELTA_NEW ? "new" :
           "<unknown>"), insn->offset, insn->len);
    return 0;
}


void
svndiff_init_insn_array(array_t *ar)
{
    if (array_init(ar, sizeof(svndiff_insn_t), 0,
                   (array_initializer_t)init_insn,
                   NULL) != 0) {
        FAIL("array_init");
    }
}

void
svndiff_fini_insn_array(array_t *ar)
{
    if (array_fini(ar) != 0) {
        FAIL("array_fini");
    }
}

void
svndiff_dump_insn_array(array_t *ar)
{
    array_traverse(ar, (array_traverser_t)dump_insn, NULL);
}

int
svndiff_doc_apply(svndiff_doc_t *doc)
{
    int res = 0;
    array_iter_t it;
    svndiff_insn_t *insn;
    char *tbuf = NULL, *ptbuf;
    ssize_t navail;
    svnproto_bytes_t **bytes;
    char *pbytes;

    /* Set up target */
    if (doc->tview_len < 0) {
        svndiff_doc_dump(doc);
    }
    if ((tbuf = malloc(doc->tview_len)) == NULL) {
        FAIL("malloc");
    }
    ptbuf = tbuf;
    navail = doc->tview_len;

    /*
     * Prepare new bytes. We assume we have a single chunk in doc->bytes.
     * Might be worth of
     * fixing it.
     */
    if ((bytes = array_first(&doc->bytes, &it)) != NULL) {

        pbytes = (*bytes)->data;

        /* Set up and verify source */

        if (doc->base_checksum != NULL) {
            if (verify_checksum(doc->fd, doc->base_checksum->data) != 0) {
                /* check it out clean? */
                res = SVNPROTO_EDITOR + 31;
                goto END;
            }
        } else {
            /*
             * It's come from an add-file command, fd must point at an empty
             * file. If it's not empty, ...
             */
        }

        //TRACE("+ %s", BDATA(doc->rp));

        for (insn = array_first(&doc->insns, &it);
             insn != NULL;
             insn = array_next(&doc->insns, &it)) {

            //TRACE("applying %d", insn->code);
            if (insn->len > navail) {
                res = SVNDIFF_DOC_APPLY + 1;
                goto END;
            }

            if (insn->code == SVN_TXDELTA_SOURCE) {
                ssize_t nread;

                /* read len bytes from source at offset */
                if ((nread = pread(doc->fd, ptbuf, insn->len,
                                   insn->offset)) < 0) {
                    perror("read");
                    res = SVNDIFF_DOC_APPLY + 2;
                    goto END;
                }
                if (nread < insn->len) {
                    /* corrupt insn? */
                    res = SVNDIFF_DOC_APPLY + 3;
                    goto END;
                }

                ptbuf += insn->len;
                navail -= insn->len;

            } else if (insn->code == SVN_TXDELTA_NEW) {
                /* read len bytes from new at offset */
                if (((ssize_t)(*bytes)->sz) - (pbytes - (*bytes)->data) <
                    insn->len) {

                    /* corrupt insn? */
                    res = SVNDIFF_DOC_APPLY + 4;
                    goto END;
                }

                memcpy(ptbuf, pbytes, insn->len);
                pbytes += insn->len;

                ptbuf += insn->len;
                navail -= insn->len;

            } else if (insn->code == SVN_TXDELTA_TARGET) {
                /* read len bytes from new at offset */
                char *sbuf = tbuf + insn->offset;
                char *sbuf_end = sbuf + insn->len;

                /* XXX validation */

                /* patterning copy in terms of libsvn_delta/text_delta.c */
                for (; sbuf < sbuf_end; ++sbuf, ++ptbuf) {
                    *ptbuf = *sbuf;
                }

                navail -= insn->len;

            } else {
                res = SVNDIFF_DOC_APPLY + 5;
                goto END;
            }
        }
    } else {
        /* zero-length file */
        //svndiff_doc_dump(doc);
    }

    if (lseek(doc->fd, 0, SEEK_SET) != 0) {
        perror("lseek");
        res = SVNDIFF_DOC_APPLY + 6;
        goto END;
    }

    if (write(doc->fd, tbuf, doc->tview_len) < 0) {
        FAIL("write");
    }

    if (ftruncate(doc->fd, doc->tview_len) < 0) {
        FAIL("ftruncate");
    }

END:
    if (tbuf != NULL) {
        free(tbuf);
        tbuf = NULL;
    }
    TRRET(res);
}

int
svndiff_doc_init(svndiff_doc_t *doc)
{
    /*
     * algorithm check: assume svndiff_doc_init() and svndiff_doc_fini()
     * are balanced.
     */
    assert(!(doc->flags & SDFL_INITED));

    doc->version = -1;
    doc->sview_offset = 0;
    doc->sview_len = 0;
    doc->tview_len = 0;
    doc->inslen = 0;
    doc->newlen = 0;
    doc->parse_state = SD_STATE_START;
    doc->inslen_check = 0;
    doc->orig_inslen = 0;

    svndiff_init_insn_array(&doc->insns);
    svnproto_init_bytes_array(&doc->bytes);
    doc->base_checksum = NULL;
    doc->rp = NULL;
    doc->lp = NULL;
    doc->rev = -1;
    doc->ft = NULL;
    doc->fd = -1;
    doc->flags |= SDFL_INITED;
    return 0;
}


void
svndiff_clear_current_file(svndiff_doc_t *doc)
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
    doc->sview_offset = 0;
    doc->sview_len = 0;
    doc->tview_len = 0;
    doc->inslen = 0;
    doc->newlen = 0;
    doc->parse_state = SD_STATE_START;
    doc->inslen_check = 0;
    doc->orig_inslen = 0;
    svndiff_fini_insn_array(&doc->insns);
    svnproto_fini_bytes_array(&doc->bytes);
    if (doc->base_checksum != NULL) {
        free(doc->base_checksum);
        doc->base_checksum = NULL;
    }
    svndiff_clear_current_file(doc);
    doc->flags &= ~SDFL_INITED;
    return 0;
}

int
svndiff_doc_dump(svndiff_doc_t *doc)
{
    TRACE("doc version=%d sview_offset=%ld sview_len=%ld "
          "tview_len=%ld inslen=%ld inslen_check=%ld "
          "orig_inslen=%ld newlen=%ld base_checksum=%s rp=%s rev=%ld lp=%s fd=%d",
          doc->version,
          doc->sview_offset,
          doc->sview_len,
          doc->tview_len,
          doc->inslen,
          doc->inslen_check,
          doc->orig_inslen,
          doc->newlen,
          BDATA(doc->base_checksum),
          BDATA(doc->rp),
          doc->rev,
          doc->lp,
          doc->fd
         );
    svndiff_dump_insn_array(&doc->insns);
    svnproto_dump_bytes_array(&doc->bytes);
    return 0;
}

