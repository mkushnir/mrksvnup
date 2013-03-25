#include <assert.h>
#include <stdarg.h>
#include <stdlib.h>

#include "diag.h"
#include "mrkcommon/array.h"
#include "mrkcommon/util.h"
//#define TRRET_DEBUG
#include "mrkcommon/dumpm.h"

#include "mrksvnup/svnproto.h"
#include "mrksvnup/bytestream.h"

/* data helpers */

static int
dump_long(long *v, UNUSED void *udata)
{
    TRACE("v=%ld", *v);
    return 0;
}

void
svnproto_init_long_array(array_t *ar)
{
    if (array_init(ar, sizeof(long), 0,
                   NULL, NULL) != 0) {
        FAIL("array_init");
    }
}

void
svnproto_dump_long_array(array_t *ar) {
    array_traverse(ar, (array_traverser_t)dump_long, NULL);
}

static int
init_string(const char **v)
{
    *v = NULL;
    return 0;
}

static int
fini_string(char **v)
{
    if (*v != NULL) {
        free(*v);
        *v = NULL;
    }
    return 0;
}

static int
dump_string(const char **v, UNUSED void *udata)
{
    TRACE("v=%s", *v);
    return 0;
}

void
svnproto_init_string_array(array_t *ar)
{
    if (array_init(ar, sizeof(char *), 0,
                   (array_initializer_t)init_string,
                   (array_finalizer_t)fini_string) != 0) {
        FAIL("array_init");
    }
}

void
svnproto_dump_string_array(array_t *ar) {
    array_traverse(ar, (array_traverser_t)dump_string, NULL);
}

static int
parse_one_value(bytestream_t *in, svnproto_state_t *st)
{
    while (!SNEEDMORE(in)) {
        char c;

        if (st->tokenizer_state == TS_STRING_IN) {
            //TRACE("pos=%ld start=%ld", SPOS(in), st->r.start);
            if (SPOS(in) < (st->r.start + st->i)) {
                SINCR(in);
                continue;

            } else {
                st->tokenizer_state = TS_STRING_CHECK;
                st->r.end = SPOS(in);
                /*
                 * Don't SINCR() here since we are already out of the
                 * string. Make another loop to check for the space
                 * at the end of the string.
                 */
                continue;
            }

        } else {
            c = SPCHR(in);

            if (ISSPACE(c)) {
                if (st->tokenizer_state == TS_TOK_IN) {
                    st->tokenizer_state = TS_TOK_OUT;
                    st->r.end = SPOS(in);
                    SPCHR(in) = '\0';

                } else if (st->tokenizer_state == TS_LIST_START ||
                            st->tokenizer_state == TS_LIST_END) {
                    st->tokenizer_state = TS_START; //fake ...
                    SINCR(in);
                    continue;

                } else if (st->tokenizer_state == TS_NUM_IN) {
                    st->tokenizer_state = TS_NUM_OUT;
                    st->r.end = SPOS(in);
                    SPCHR(in) = '\0';
                    st->i = strtol(SDATA(in, st->r.start), NULL, 10);

                } else if (st->tokenizer_state == TS_STRING_CHECK) {
                    st->tokenizer_state = TS_STRING_OUT;
                    SPCHR(in) = '\0';

                } else if (st->tokenizer_state & TS_OUT) {
                    SINCR(in);
                    continue;

                } else {
                    TRRET(PARSE_ONE_VALUE + 1);
                }

            } else if (c == '(') {
                if (st->tokenizer_state & TS_OUT) {
                    st->tokenizer_state = TS_LIST_START;

                } else {
                    TRRET(PARSE_ONE_VALUE + 2);
                }

            } else if (c == ')') {
                if (st->tokenizer_state & TS_OUT) {
                    st->tokenizer_state = TS_LIST_END;

                } else {
                    //TRACE("st=%s", TSSTR(st->tokenizer_state));
                    TRRET(PARSE_ONE_VALUE + 3);
                }

            } else if (ISDIGIT(c)) {
                if (st->tokenizer_state & TS_OUT) {
                    st->tokenizer_state = TS_NUM_IN;
                    st->r.start = SPOS(in);
                    SINCR(in);
                    continue;

                } else if (st->tokenizer_state == TS_NUM_IN) {
                    SINCR(in);
                    continue;

                } else if (st->tokenizer_state == TS_TOK_IN) {
                    SINCR(in);
                    continue;

                } else {
                    TRRET(PARSE_ONE_VALUE + 4);
                }

            } else if (c == ':') {
                if (st->tokenizer_state == TS_NUM_IN) {
                    st->tokenizer_state = TS_STRING_IN;
                    st->r.end = SPOS(in);
                    SPCHR(in) = '\0';
                    SINCR(in);

                    st->i = strtol(SDATA(in, st->r.start), NULL, 10);
                    st->r.start = SPOS(in);

                    continue;

                } else {
                    TRRET(PARSE_ONE_VALUE + 5);
                }

            } else if (ISWORD(c)) {
                if (st->tokenizer_state & TS_OUT) {
                    st->tokenizer_state = TS_TOK_IN;
                    st->r.start = SPOS(in);
                    SINCR(in);
                    continue;

                } else if (st->tokenizer_state == TS_TOK_IN) {
                    SINCR(in);
                    continue;

                } else {
                    TRRET(PARSE_ONE_VALUE + 6);
                }
            } else {
                TRRET(PARSE_ONE_VALUE + 7);
            }
        }

        SINCR(in);
        return 0;
    }

    return PARSE_NEED_MORE;
}

static int
read_one_value(int fd, bytestream_t *in, svnproto_state_t *st)
{
    int res;
    //off_t savedpos;

    if (!st->next_read) {
        st->next_read = 1;
        return 0;
    }

    res = PARSE_NEED_MORE;
    //savedpos = SPOS(in);

    while (res == PARSE_NEED_MORE) {
        if (SNEEDMORE(in)) {
            if ((res = bytestream_consume_data(in, fd)) != 0) {
                /* this must be treated as EOF condition */
                res = PARSE_EOD;
                break;
            }
        }

        res = parse_one_value(in, st);

        if (res == 0) {
            break;

        } else if (res == PARSE_NEED_MORE) {
            if (SNEEDMORE(in)) {
                continue;
            } else {
                break;
            }

        } else {
            /* error */
            //SPOS(in) = savedpos;
            break;
        }
    }

    return res;
}

static svnproto_bytes_t *
byte_chunk(bytestream_t *in, svnproto_state_t *st)
{
    char *res = NULL;
    svnproto_bytes_t *b = NULL;

    if ((res = malloc(st->r.end - st->r.start +
                      sizeof(svnproto_bytes_t) + 1)) != NULL) {
        b = (svnproto_bytes_t *)res;
        b->sz = st->r.end - st->r.start;
        memcpy(b->data, SDATA(in, st->r.start), b->sz);
        b->data[b->sz] = '\0';
    }
    return b;
}

static void
svnproto_state_init(svnproto_state_t *st)
{
    st->tokenizer_state = TS_START;
    st->i = 0;
    st->r.start = -1;
    st->r.end = -1;
    st->next_read = 1;
}

int
svnproto_vunpack(svnc_ctx_t *ctx,
                 bytestream_t *in,
                 const char *spec,
                 va_list ap)
{
    int res;
    const char *ps;
    char ch, ch1;
    svnproto_state_t st;

    svnproto_state_init(&st);

    //TRACE(">>>spec=%s", spec);

    for (ps = spec; ps; ++ps) {

        /* check spec */
        ch = *ps;
        //TRACE("ch='%c'", ch);

        if (ch == '\0') {
            res = 0;
            break;

        } else {
            ch1 = *(ps + 1);
            if (ch1 == '?' || ch1 == '*') {
                ++ps;
            }

            if (ch == '(') {
                if ((res = read_one_value(ctx->fd, in, &st)) != 0) {
                    /* read error */
                    goto END;
                }

                if (!(st.tokenizer_state & (TS_LIST_START | TS_IGNORE))) {
                    /* no match */
                    TRACE("ch='%c' ch1='%c' st=%s res=%d st.i=%ld st.r=%s",
                          ch, ch1,
                          TSSTR(st.tokenizer_state),
                          res, st.i, SDATA(in, st.r.start));
                    res = SVNPROTO_VUNPACK + 1;
                    goto END;
                }

                /* no variable to pick */

            } else if (ch == ')') {
                if ((res = read_one_value(ctx->fd, in, &st)) != 0) {
                    /* read error */
                    goto END;
                }

                if (!(st.tokenizer_state & (TS_LIST_END | TS_IGNORE))) {
                    /* no match */
                    res = SVNPROTO_VUNPACK + 2;
                    goto END;
                }

                /* no variable */

            } else if (ch == 'n') {
                array_t *ar;
                long *pv;

                if ((res = read_one_value(ctx->fd, in, &st)) != 0) {
                    /* read error */
                    goto END;
                }

                if (ch1 == '*') {
                    ar = va_arg(ap, array_t *);

                    if (ar != NULL) {
                        while (st.tokenizer_state == TS_NUM_OUT) {

                            if ((pv = array_incr(ar)) == NULL) {
                                FAIL("array_incr");
                            }

                            *pv = st.i;

                            if ((res = read_one_value(ctx->fd, in, &st)) != 0) {
                                /* read error */
                                goto END;
                            }
                        }
                        /* no match continue */

                    } else {
                        while (st.tokenizer_state == TS_NUM_OUT) {

                            if ((res = read_one_value(ctx->fd, in, &st)) != 0) {
                                /* read error */
                                goto END;
                            }
                        }
                        /* no match continue */
                    }

                    st.next_read = 0;

                } else if (ch1 == '?') {
                    pv = va_arg(ap, long *);

                    if (st.tokenizer_state == TS_NUM_OUT) {
                        if (pv != NULL) {
                            *pv = st.i;
                        }
                    } else {
                        /* no match continue */
                        if (pv != NULL) {
                            *pv = 0L;
                        }
                        st.next_read = 0;
                    }

                } else {
                    if (st.tokenizer_state == TS_NUM_OUT) {
                        pv = va_arg(ap, long *);
                        if (pv != NULL) {
                            *pv = st.i;
                        }

                    } else {
                        /* no match, error  */
                        res = SVNPROTO_VUNPACK + 3;
                        goto END;
                    }
                }

            } else if (ch == 'w') {
                array_t *ar;
                char **pv;

                if ((res = read_one_value(ctx->fd, in, &st)) != 0) {
                    /* read error */
                    goto END;
                }

                if (ch1 == '*') {
                    ar = va_arg(ap, array_t *);

                    if (ar != NULL) {
                        while (st.tokenizer_state == TS_TOK_OUT) {

                            if ((pv = array_incr(ar)) == NULL) {
                                FAIL("array_incr");
                            }

                            *pv = strdup(SDATA(in, st.r.start));

                            if ((res = read_one_value(ctx->fd, in, &st)) != 0) {
                                /* read error */
                                goto END;
                            }
                        }
                        /* no match continue */

                    } else {
                        while (st.tokenizer_state == TS_TOK_OUT) {

                            if ((res = read_one_value(ctx->fd, in, &st)) != 0) {
                                /* read error */
                                goto END;
                            }
                        }
                        /* no match continue */
                    }

                    st.next_read = 0;

                } else if (ch1 == '?') {
                    pv = va_arg(ap, char **);

                    if (st.tokenizer_state == TS_TOK_OUT) {
                        if (pv != NULL) {
                            *pv = strdup(SDATA(in, st.r.start));
                        }
                    } else {
                        /* no match continue */
                        if (pv != NULL) {
                            *pv = NULL;
                        }
                        st.next_read = 0;
                    }

                } else {
                    if (st.tokenizer_state == TS_TOK_OUT) {

                        pv = va_arg(ap, char **);
                        if (pv != NULL) {
                            *pv = strdup(SDATA(in, st.r.start));
                        }

                    } else {
                        /* no match error */
                        res = SVNPROTO_VUNPACK + 4;
                        goto END;
                    }
                }

            } else if (ch == 's') {
                array_t *ar;
                svnproto_bytes_t **pv;

                if ((res = read_one_value(ctx->fd, in, &st)) != 0) {
                    /* read error */
                    goto END;
                }

                if (ch1 == '*') {
                    ar = va_arg(ap, array_t *);

                    if (ar != NULL) {
                        while (st.tokenizer_state == TS_STRING_OUT) {

                            if ((pv = array_incr(ar)) == NULL) {
                                FAIL("array_incr");
                            }

                            *pv = byte_chunk(in, &st);

                            if ((res = read_one_value(ctx->fd, in, &st)) != 0) {
                                goto END;
                            }
                        }
                        /* no match continue */

                    } else {
                        while (st.tokenizer_state == TS_STRING_OUT) {

                            if ((res = read_one_value(ctx->fd, in, &st)) != 0) {
                                /* read error */
                                goto END;
                            }
                        }
                        /* no match continue */
                    }

                    st.next_read = 0;

                } else if (ch1 == '?') {
                    pv = va_arg(ap, svnproto_bytes_t **);

                    if (st.tokenizer_state == TS_STRING_OUT) {
                        if (pv != NULL) {
                            *pv = byte_chunk(in, &st);
                        }
                    } else {
                        if (pv != NULL) {
                            *pv = NULL;
                        }
                        /* no match continue */
                        st.next_read = 0;
                    }

                } else {
                    if (st.tokenizer_state == TS_STRING_OUT) {

                        pv = va_arg(ap, svnproto_bytes_t **);
                        if (pv != NULL) {
                            *pv = byte_chunk(in, &st);
                        }

                    } else {
                        /* no match error */
                        res = SVNPROTO_VUNPACK + 5;
                        goto END;
                    }
                }

            } else if (ch == 'S') {
                svnproto_cb_t cb;
                void *udata;

                cb = va_arg(ap, svnproto_cb_t);
                udata = va_arg(ap, void *);


                if ((res = read_one_value(ctx->fd, in, &st)) != 0) {
                    /* read error */
                    goto END;
                }

                if (ch1 == '*') {

                    if (cb != NULL) {
                        while (st.tokenizer_state == TS_STRING_OUT) {
                            if (cb(ctx, in, &st, udata) != 0) {
                                /*
                                 * user-level no-match, continue (with the
                                 * next ofrmat character)
                                 */
                                break;
                            }

                            if ((res = read_one_value(ctx->fd, in, &st)) != 0) {
                                /* read error */
                                goto END;
                            }
                        }

                    } else {
                        while (st.tokenizer_state == TS_STRING_OUT) {

                            if ((res = read_one_value(ctx->fd, in, &st)) != 0) {
                                /* read error */
                                goto END;
                            }
                        }
                    }

                    st.next_read = 0;

                } else if (ch1 == '?') {

                    if (st.tokenizer_state == TS_STRING_OUT) {
                        if (cb != NULL) {
                            if (cb(ctx, in, &st, udata) != 0) {
                                /*
                                 * user-level no-math, continue with the
                                 * next character (that is ignore this
                                 * result).
                                 */
                            }
                        }

                    } else {
                        st.next_read = 0;
                    }

                } else {
                    if (st.tokenizer_state == TS_STRING_OUT) {

                        if (cb != NULL) {
                            if (cb(ctx, in, &st, udata) != 0) {
                                /*
                                 * user-level no-match. Ignore this result
                                 * adn continue with the next format
                                 * character.
                                 */
                            }
                        }

                    } else {
                        /* no match error */
                        res = SVNPROTO_VUNPACK + 6;
                        goto END;
                    }
                }

            } else if (ch == 'r') {
                svnproto_cb_t cb;
                void *udata;

                cb = va_arg(ap, svnproto_cb_t);
                udata = va_arg(ap, void *);

                /*
                 * XXX we don't ready any values here. The values are
                 * supposed to be read in the callback.
                 *
                 * Return values:
                 *      * return 0 - loop
                 *      * return PARSE_EOD - no-match continue with the
                 *          next format chracter
                 *      * return other - read error
                 */

                if (ch1 == '*') {

                    if (cb != NULL) {
                        while ((res = cb(ctx, in, &st, udata)) == 0) {
                            ;
                        }
                        if (res == PARSE_EOD) {
                            /*
                             * user-level interrupt, means no-match, reset
                             * next_read and continue.
                             */

                        } else {
                            /* read error */
                            res = SVNPROTO_VUNPACK + 7;
                            goto END;
                        }

                    } else {
                        /* weird */
                        /* noop */
                    }
                    svnproto_state_init(&st);
                    st.next_read = 0;

                } else if (ch1 == '?') {

                    if (cb != NULL) {
                        if ((res = cb(ctx, in, &st, udata)) != 0) {
                            if (res == PARSE_EOD) {
                                /*
                                 * user-level interrupt, no match, reset
                                 * next read and continue
                                 */
                                svnproto_state_init(&st);
                                st.next_read = 0;

                            } else {
                                /* read error */
                                res = SVNPROTO_VUNPACK + 8;
                                goto END;
                            }

                        } else {
                            /* continue with the next format character */
                            svnproto_state_init(&st);
                        }

                    } else {
                        /* noop */
                    }

                } else {
                    if (cb != NULL) {
                        if ((res = cb(ctx, in, &st, udata)) != 0) {
                            /* read error, no choice  */
                            res = SVNPROTO_VUNPACK + 9;
                            goto END;
                        }
                    }
                    /* continue with the next format character */
                    svnproto_state_init(&st);
                }

                /* an ugly hack to not block parse_one_value(). */
                st.tokenizer_state = TS_IGNORE;
                bytestream_recycle(in);

            } else {
                res = SVNPROTO_VUNPACK + 10;
                goto END;
            }

        }

        //TRACE("ch='%c' ch1='%c' st=%s res=%d st.i=%ld st.r=%s",
        //      ch, ch1,
        //      TSSTR(st.tokenizer_state),
        //      res, st.i, SDATA(in, st.r.start));
    }

    //TRACE("<<<spec=%s", spec);


END:
    return res;
}

int
svnproto_unpack(svnc_ctx_t *ctx,
                bytestream_t *in,
                const char *spec,
                ...)
{
    va_list ap;
    int res;

    va_start(ap, spec);
    res = svnproto_vunpack(ctx, in, spec, ap);
    va_end(ap);

    TRRET(res);
}

