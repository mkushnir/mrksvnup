AUTOMAKE_OPTIONS = foreign
CLEANFILES = *.core
#CLEANFILES += *.in

noinst_PROGRAMS=testfoo

distdir = ../../$(PACKAGE)-$(VERSION)/src/test
dist_HEADERS = unittest.h

BUILT_SOURCES = ../diag.c ../diag.h

CFLAGS = -Wall -Wextra -Werror -std=c99 -I.. -g -O0 -ftrapv -fcatch-undefined-behavior -I$(includedir)
LDFLAGS += -L$(libdir) -lmrkcommon -lz -lbsdxml -lcrypto -lssl

nobase_include_HEADERS = ../mrksvnup/svnc.h \
			 ../mrksvnup/svncdir.h \
			 ../mrksvnup/svndiff.h \
			 ../mrksvnup/svnedit.h \
			 ../mrksvnup/svnproto.h \
			 ../mrksvnup/http.h \
			 ../mrksvnup/httpproto.h \
			 ../mrksvnup/dav.h \
			 ../mrksvnup/xmatch.h \
			 ../mrksvnup/version.h

nodist_testfoo_SOURCES = ../diag.c
testfoo_SOURCES = ../svnc.c \
		  ../http.c \
		  ../httpproto.c \
		  ../httpproto_get_file.c \
		  ../dav.c \
		  ../xmatch.c \
		  ../svnc_util.c \
		  ../svncdir.c \
		  ../svndiff.c \
		  ../svnedit.c \
		  ../svnproto.c \
		  ../svnproto_pack.c \
		  ../svnproto_unpack.c \
		  ../svnproto_auth.c \
		  ../svnproto_get_latest_rev.c \
		  ../svnproto_check_path.c \
		  ../svnproto_get_dir.c \
		  ../svnproto_get_file.c \
		  ../svnproto_reparent.c \
		  ../svnproto_update.c \
		  ../svnproto_set_path.c \
		  ../svnproto_finish_report.c \
		  ../svnproto_abort_report.c \
		  ../svnproto_editor.c \
		  testfoo.c
testfoo_CFLAGS =
testfoo_LDFLAGS =

../diag.c ../diag.h: ../diag.txt
	sh ../gen-diag

testrun: all
	for i in $(noinst_PROGRAMS); do if test -x ./$$i; then LD_LIRARY_PATH=$(libdir) ./$$i; fi; done;
