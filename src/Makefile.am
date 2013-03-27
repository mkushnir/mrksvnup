AUTOMAKE_OPTIONS = foreign

distdir = ../$(PACKAGE)-$(VERSION)/src
dist_HEADERS =

BUILT_SOURCES = diag.c diag.h
EXTRA_DIST = diag.txt gen-diag
CLEANFILES = $(BUILT_SOURCES) *.core
#CLEANFILES += *.in

lib_LTLIBRARIES = libmrksvnup.la

nobase_include_HEADERS = mrksvnup/svnc.h \
			 mrksvnup/svncdir.h \
			 mrksvnup/svndiff.h \
			 mrksvnup/svnproto.h \
			 mrksvnup/svnproto_bytes.h \
			 mrksvnup/bytestream.h
			 mrksvnup/version.h

libmrksvnup_la_SOURCES = svnc.c \
			 svncdir.c \
			 svndiff.c \
			 svnproto.c \
			 svnproto_bytes.c \
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
			 bytestream.c

nodist_libmrksvnup_la_SOURCES = diag.c
libmrksvnup_la_CFLAGS = -Wall -Wextra -Werror -g -std=c99 -I$(includedir) -O0 -ftrapv -fcatch-undefined-behavior
libmrksvnup_la_LDFLAGS = -version-info 0:0:0 -L$(libdir) -lmrkcommon -lz -lmd

bin_PROGRAMS = svnup

svnup_SOURCES = main.c
nodist_svnup_SOURCES =
svnup_CFLAGS = -Wall -Wextra -Werror -g -std=c99 -I$(includedir)
svnup_LDFLAGS = -version-info 0:0:0 -L$(libdir) -lmrkcommon -lmrksvnup

SUBDIRS = . test

diag.c diag.h: diag.txt
	sh ./gen-diag

run: all
	for i in $(bin_PROGRAMS); do if test -x ./$$i; then LD_LIRARY_PATH=$(libdir) ./$$i; fi; done;

testrun:
	for i in $(SUBDIRS); do if test "$$i" != "."; then cd $$i && $(MAKE) testrun && cd ..; fi; done;
