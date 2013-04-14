#ifndef BYTESTREAM_SSL_HELPER_H
#define BYTESTREAM_SSL_HERLPER_H

#include "mrkcommon/bytestream.h"

ssize_t bytestream_ssl_recv_more(bytestream_t *, int, ssize_t);

ssize_t bytestream_ssl_send(bytestream_t *, int, size_t);
#endif
