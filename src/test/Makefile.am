AUTOMAKE_OPTIONS = foreign
CLEANFILES = *.core
#CLEANFILES += *.in

noinst_PROGRAMS=testfoo

distdir = ../../$(PACKAGE)-$(VERSION)/src/test

diags = ../diag.txt
BUILT_SOURCES = diag.c diag.h

if DEBUG
DEBUG_FLAGS = -g -O0 @CLANG_DEBUG@
else
DEBUG_FLAGS = -DNDEBUG -O3
endif

CFLAGS = $(DEBUG_FLAGS) -Wall -Wextra -Werror -std=c99 -I.. -I$(includedir)
LDFLAGS += -L$(libdir) -lmrkcommon -lmndiag -lz -lbsdxml -lcrypto -lssl

dist_HEADERS = unittest.h \
			 ../mrksvnup/svnc.h \
			 ../mrksvnup/svncdir.h \
			 ../mrksvnup/svndiff.h \
			 ../mrksvnup/svnedit.h \
			 ../mrksvnup/svnproto.h \
			 ../mrksvnup/http.h \
			 ../mrksvnup/httpproto.h \
			 ../mrksvnup/dav.h \
			 ../mrksvnup/xmatch.h \
			 ../mrksvnup/version.h

nodist_testfoo_SOURCES = diag.c
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
		  ../bytestream_ssl_helper.c \
		  testfoo.c
testfoo_CFLAGS =
testfoo_LDFLAGS = -L$(libdir) -lmrkapp

diag.c diag.h: $(diags)
	$(AM_V_GEN) cat $(diags) | sort -u >diag.txt.tmp && mndiagen -v -S diag.txt.tmp -L mrksvnup -H diag.h -C diag.c ../*.[ch] ./*.[ch]

testrun: all
	for i in $(noinst_PROGRAMS); do if test -x ./$$i; then LD_LIBRARY_PATH=$(libdir) ./$$i; fi; done;
