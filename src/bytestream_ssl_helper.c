#include <assert.h>
#include <sys/socket.h>
#include <openssl/ssl.h>

#include "mrkcommon/util.h"

#include "mrksvnup/http.h"
#include "bytestream_ssl_helper.h"
#include "mrkcommon/bytestream.h"

#define BLOCKSZ 4096

ssize_t
bytestream_ssl_recv_more(bytestream_t *stream,
                         UNUSED int fd,
                         ssize_t sz)
{
    http_ctx_t *httpctx = stream->udata;
    SSL *ssl = httpctx->udata;
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

    if ((nrecv = SSL_read(ssl, stream->buf.data + stream->eod,
                      (int)sz)) >= 0) {
        //D16(stream->buf.data + stream->eod, nrecv);
        stream->eod += nrecv;
    }

    //TRACE("nrecv=%ld", nrecv);
    return (nrecv);
}


ssize_t bytestream_ssl_send(bytestream_t *stream,
                            UNUSED int fd,
                            size_t sz)
{
    http_ctx_t *httpctx = stream->udata;
    SSL *ssl = httpctx->udata;
    ssize_t nwritten;

    assert(ssl != NULL);

    if ((stream->pos + (ssize_t)sz) > stream->eod) {
        return (-1);

    }

    nwritten = SSL_write(ssl, stream->buf.data + stream->pos, sz);
    stream->pos += nwritten;

    return (nwritten);
}

