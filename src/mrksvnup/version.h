#ifndef VERSION_H
#define VERSION_H

#ifdef HAVE_CONFIG_H
#include "config.h"
#else
/* stub */
#define PACKAGE_VERSION "0.0"
#endif

/*
 *      0x00000000
 *  mjr____| | | |
 *  mnr______| | |
 *  rel________| |
 *  bld__________|
 */
#define SVNUP_VERSION 0x00010000

#endif
