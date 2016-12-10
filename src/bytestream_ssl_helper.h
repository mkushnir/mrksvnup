#ifndef BYTESTREAM_SSL_HELPER_H
#define BYTESTREAM_SSL_HELPER_H

#include "mrkcommon/bytestream.h"

#ifdef __cplusplus
extern "C" {
#endif

ssize_t bytestream_ssl_recv_more(mnbytestream_t *, int, ssize_t);

ssize_t bytestream_ssl_send(mnbytestream_t *, int, size_t);

#ifdef __cplusplus
}
#endif

#endif
