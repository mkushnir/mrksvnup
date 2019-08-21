AUTOMAKE_OPTIONS = foreign

distdir = ../$(PACKAGE)-$(VERSION)/src
dist_HEADERS = svnc_private.h httpproto_private.h bytestream_ssl_helper.h

diags = diag.txt
BUILT_SOURCES = diag.c diag.h
EXTRA_DIST = $(diags)
CLEANFILES = $(BUILT_SOURCES) *.core
#CLEANFILES += *.in

lib_LTLIBRARIES = libmrksvnup.la

nobase_include_HEADERS = mrksvnup/svnc.h \
			 mrksvnup/svncdir.h \
			 mrksvnup/svndiff.h \
			 mrksvnup/svnedit.h \
			 mrksvnup/svnproto.h \
			 mrksvnup/http.h \
			 mrksvnup/httpproto.h \
			 mrksvnup/dav.h \
			 mrksvnup/xmatch.h \
			 mrksvnup/version.h

noinst_HEADERS =

libmrksvnup_la_SOURCES = svnc.c \
			 svnc_util.c \
			 svncdir.c \
			 svndiff.c \
			 svnedit.c \
			 http.c \
			 httpproto.c \
			 httpproto_get_file.c \
			 dav.c \
			 xmatch.c \
			 svnproto.c \
			 svnproto_unpack.c \
			 svnproto_pack.c \
			 svnproto_auth.c \
			 svnproto_get_latest_rev.c \
			 svnproto_check_path.c \
			 svnproto_get_dir.c \
			 svnproto_get_file.c \
			 svnproto_reparent.c \
			 svnproto_update.c \
			 svnproto_set_path.c \
			 svnproto_finish_report.c \
			 svnproto_abort_report.c \
			 svnproto_editor.c \
			 bytestream_ssl_helper.c

nodist_libmrksvnup_la_SOURCES = diag.c

if DEBUG
DEBUG_FLAGS = -g -O0 @CLANG_DEBUG@
else
DEBUG_FLAGS = -DNDEBUG -O3
endif

if FCOLOR
FCOLOR_FLAGS =
else
FCOLOR_FLAGS = -DNOFCOLOR
endif

libmrksvnup_la_CFLAGS = $(DEBUG_FLAGS) -Wall -Wextra -Werror -std=c99 -I$(includedir) $(FCOLOR_FLAGS)

libmrksvnup_la_LDFLAGS = -version-info 0:0:0 -L$(libdir) -lmrkcommon -lmndiag -lz -lbsdxml -lcrypto -lssl

bin_PROGRAMS = svnup

svnup_SOURCES = main.c
nodist_svnup_SOURCES =
svnup_CFLAGS = $(DEBUG_FLAGS) -Wall -Wextra -Werror -g -std=c99 -I$(includedir) $(FCOLOR_FLAGS)
svnup_LDFLAGS = -version-info 0:0:0 -L. -lmrksvnup -L$(libdir) -lmrkcommon -lmndiag

SUBDIRS = . test

svnup$(EXEEXT): libmrksvnup.la

diag.c diag.h: $(diags)
	$(AM_V_GEN) cat $(diags) | sort -u >diag.txt.tmp && mndiagen -v -S diag.txt.tmp -L mrksvnup -H diag.h -C diag.c *.[ch]

run: all
	for i in $(bin_PROGRAMS); do if test -x ./$$i; then LD_LIBRARY_PATH=$(libdir) ./$$i; fi; done;

testrun:
	for i in $(SUBDIRS); do if test "$$i" != "."; then cd $$i && $(MAKE) testrun && cd ..; fi; done;
