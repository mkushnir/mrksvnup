# $FreeBSD$
PORTNAME=	mrksvnup
PORTVERSION=	0.1
CATEGORIES=	net
#MASTER_SITES=	http://localhost:8000/distfiles/
MASTER_SITES=	https://raw.github.com/mkushnir/distfiles/master/

MAINTAINER=	markiyan.kushnir@gmail.com
COMMENT=	A fast svnup for FreeBSD

MAN1=		svnup.1
MANCOMPRESSED=	no
USE_LDCONFIG=	yes

# I assume it will be there,
# https://github.com/mkushnir/mrkcommon
#LIB_DEPENDS=	mrkcommon:devel/mrkcommon

GNU_CONFIGURE=	yes
CONFIGURE_ENV=	LDFLAGS=-L${LOCALBASE}/lib

OPTIONS_DEFINE=	FCOLOR DEBUG
FCOLOR_DESC=	Enable colored output
DEBUG_DESC=	Build debugging version

.include <bsd.port.options.mk>

.if ${PORT_OPTIONS:MFCOLOR}
CONFIGURE_ARGS+= --enable-fcolor
.endif

.if ${PORT_OPTIONS:MDEBUG}
CONFIGURE_ARGS+= --enable-debug
.endif

.include <bsd.port.mk>
