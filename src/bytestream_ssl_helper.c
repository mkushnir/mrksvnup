#include <assert.h>
#include <stdbool.h>
#include <sys/socket.h>
#include <openssl/ssl.h>


//#define TRRET_DEBUG
#include <mrkcommon/dumpm.h>

#include <mrkcommon/bytestream.h>
#include <mrkcommon/util.h>

#include <mrkthr.h>

#include "bytestream_ssl_helper.h"

#define BLOCKSZ 4096

ssize_t
bytestream_ssl_recv_more(mnbytestream_t *stream,
                         void *in,
                         ssize_t sz)
{
    SSL *ssl = in;
    ssize_t nrecv;
    ssize_t need;

    assert(ssl != NULL);

    need = (stream->eod + sz) - stream->buf.sz;

    //TRACE("need=%ld", need);

    if (need > 0) {
        if (bytestream_grow(stream, (need < BLOCKSZ) ? BLOCKSZ : need) != 0) {
            return (-1);
        }
    }

    while (true) {
        if ((nrecv = SSL_read(ssl, stream->buf.data + stream->eod,
                          (int)sz)) > 0) {
            //D16(stream->buf.data + stream->eod, nrecv);
            stream->eod += nrecv;
            break;

        } else {
            int res;
            int fd;

            res = SSL_get_error(ssl, nrecv);
            fd = SSL_get_fd(ssl);
            switch (res) {
            case SSL_ERROR_WANT_READ:
                if (mrkthr_wait_for_read(fd) != 0) {
                    nrecv = -1;
                    goto end;
                }
                break;

            case SSL_ERROR_WANT_WRITE:
                if (mrkthr_wait_for_write(fd) != 0) {
                    nrecv = -1;
                    goto end;
                }
                break;

            default:
#ifdef TRRET_DEBUG
                CTRACE("ssl error %d", SSL_get_error(ssl, res));
#endif
                nrecv = -1;
                goto end;
            }
        }
    }

end:
    //TRACE("nrecv=%ld", nrecv);
    return (nrecv);
}


ssize_t
bytestream_ssl_send(mnbytestream_t *stream,
                            void *out,
                            size_t sz)
{
    SSL *ssl = out;
    ssize_t nwritten;

    assert(ssl != NULL);

    if ((stream->pos + (ssize_t)sz) > stream->eod) {
        return (-1);

    }

    while (true) {
        if ((nwritten = SSL_write(ssl, stream->buf.data + stream->pos, sz)) > 0) {
            break;
        } else {
            int res;
            int fd;

            res = SSL_get_error(ssl, nwritten);
            fd = SSL_get_fd(ssl);
            switch (res) {
            case SSL_ERROR_WANT_READ:
                if (mrkthr_wait_for_read(fd) != 0) {
                    nwritten = -1;
                    goto end;
                }
                break;

            case SSL_ERROR_WANT_WRITE:
                if (mrkthr_wait_for_write(fd) != 0) {
                    nwritten = -1;
                    goto end;
                }
                break;

            default:
                CTRACE("ssl error %d", SSL_get_error(ssl, res));
                nwritten = -1;
                goto end;
            }
        }
    }
    if (nwritten > 0) {
        stream->pos += nwritten;
    }

end:
    //TRACE("nwritten=%ld", nwritten);
    return (nwritten);
}

