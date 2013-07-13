#ifndef VERSION_H
#define VERSION_H

#ifdef __cplusplus
extern "C" {
#endif

#ifdef HAVE_CONFIG_H
#include "config.h"
#else
/* stub */
#define PACKAGE_VERSION "0.0"
#define PACKAGE_NAME "mrksvnup"
#endif

/*
 *      0x00000000
 *  mjr____| | | |
 *  mnr______| | |
 *  rel________| |
 *  bld__________|
 */
#define SVNUP_VERSION 0x00010000

#ifdef __cplusplus
}
#endif

#endif
