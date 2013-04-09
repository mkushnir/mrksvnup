AUTOMAKE_OPTIONS = foreign

distdir = ../$(PACKAGE)-$(VERSION)/src
dist_HEADERS = svnc_private.h httpproto_private.h

BUILT_SOURCES = diag.c diag.h
EXTRA_DIST = diag.txt gen-diag
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
			 svnproto_editor.c

nodist_libmrksvnup_la_SOURCES = diag.c
if FCOLOR
libmrksvnup_la_CFLAGS = -Wall -Wextra -Werror -g -std=c99 -I$(includedir) -O0 -ftrapv -fcatch-undefined-behavior
else
libmrksvnup_la_CFLAGS = -Wall -Wextra -Werror -g -std=c99 -I$(includedir) -O0 -ftrapv -fcatch-undefined-behavior -DNOFCOLOR
endif
libmrksvnup_la_LDFLAGS = -version-info 0:0:0 -L$(libdir) -lmrkcommon -lz -lbsdxml -lcrypto -ssl

bin_PROGRAMS = svnup

svnup_SOURCES = main.c
nodist_svnup_SOURCES =
if FCOLOR
svnup_CFLAGS = -Wall -Wextra -Werror -g -std=c99 -I$(includedir) -O0 -ftrapv -fcatch-undefined-behavior
else
svnup_CFLAGS = -Wall -Wextra -Werror -g -std=c99 -I$(includedir) -O0 -ftrapv -fcatch-undefined-behavior -DNOFCOLOR
endif
svnup_LDFLAGS = -version-info 0:0:0 -L$(libdir) -lmrkcommon -lmrksvnup

SUBDIRS = . test

diag.c diag.h: diag.txt
	sh ./gen-diag

run: all
	for i in $(bin_PROGRAMS); do if test -x ./$$i; then LD_LIRARY_PATH=$(libdir) ./$$i; fi; done;

testrun:
	for i in $(SUBDIRS); do if test "$$i" != "."; then cd $$i && $(MAKE) testrun && cd ..; fi; done;
