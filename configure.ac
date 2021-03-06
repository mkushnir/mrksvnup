AC_INIT(mrksvnup, 0.1, markiyan.kushnir@gmail.com)

AC_CONFIG_MACRO_DIR([m4])
AC_CONFIG_AUX_DIR([.ac-aux])
AC_CONFIG_FILES([Makefile src/Makefile src/test/Makefile man/Makefile])
AC_CONFIG_HEADERS(config.h)

if test -n "$USE_GCC"
then
    if test "$USE_GCC" = "46" && which gcc46
    then
        CC="gcc46"
        CXX="c++46"
        CPP="cpp"
    else
        if which gcc
        then
            CC="gcc"
            CXX="g++"
            CPP="cpp"
        else
            echo "Cannot find gcc"
            exit 1
        fi
    fi
else
    if which clang >/dev/null
    then
        CC="clang -Qunused-arguments -fcolor-diagnostics"
        CXX="clang++ -Qunused-arguments -fcolor-diagnostics"
        CPP="clang-cpp"
        clang_version=`clang --version | grep version | awk '{print $4;}'`
        clang_version_mjr=`echo $clang_version | awk -F. '{print $1;}'`
        clang_version_mnr=`echo $clang_version | awk -F. '{print $2;}'`
        if test "$clang_version_mjr" = "3" -a "$clang_version_mnr" -lt 3
        then
            AC_SUBST(CLANG_DEBUG, ['-ftrapv -fcatch-undefined-behavior'])
        else
            # we assume all others are 3.3+
            AC_SUBST(CLANG_DEBUG, ['-fsanitize=undefined-trap -fsanitize-undefined-trap-on-error'])
        fi
    else
        CC="cc"
        CXX="c++"
        CPP="cpp"
    fi
fi


AC_LANG(C)

AC_PROG_CC
AC_PROG_CXX
AM_PROG_AR
AC_PROG_INSTALL

LT_INIT
AM_INIT_AUTOMAKE([foreign subdir-objects])
AM_SILENT_RULES([yes])

dnl AM_MAINTAINER_MODE

AC_CHECK_HEADER_STDBOOL
AC_CHECK_HEADERS([syslog.h])
AC_CHECK_HEADERS([fcntl.h])
AC_CHECK_HEADERS([limits.h])
AC_CHECK_HEADERS([netdb.h])
AC_CHECK_HEADERS([netinet/in.h])
AC_CHECK_HEADERS([sys/socket.h])
AC_CHECK_HEADERS([sys/param.h])

AC_C_CONST
AC_C_INLINE
AC_TYPE_SIZE_T
AC_TYPE_OFF_T
AC_TYPE_SSIZE_T
AC_TYPE_UINT64_T
AC_CHECK_TYPES([ptrdiff_t])

AC_FUNC_MALLOC
AC_FUNC_REALLOC
AC_FUNC_LSTAT_FOLLOWS_SLASHED_SYMLINK

AC_CHECK_FUNCS([memset])
AC_CHECK_FUNCS([socket])
AC_CHECK_FUNCS([strchr])
AC_CHECK_FUNCS([strdup])
AC_CHECK_FUNCS([strstr])
AC_CHECK_FUNCS([strtol])
AC_CHECK_FUNCS([memmove])
AC_CHECK_FUNCS([mkdir])
AC_CHECK_FUNCS([rmdir])
AC_CHECK_FUNCS([ftruncate])
AC_CHECK_FUNCS([getcwd])
AC_CHECK_FUNCS([strrchr])
AC_CHECK_FUNCS([memchr])
AC_CHECK_FUNCS([strcasecmp])
AC_CHECK_FUNCS([strncasecmp])


AC_PRESERVE_HELP_ORDER

AC_ARG_ENABLE(debug,
              AC_HELP_STRING([--enable-debug],
                             [Enable debug build (default=no)]),
              [AM_CONDITIONAL([DEBUG], [enable_debug=yes])],
              [AM_CONDITIONAL([DEBUG], [test "$enable_debug" = "yes"])]
             )
AC_ARG_ENABLE(fcolor,
              AC_HELP_STRING([--enable-fcolor],
                             [Enable colored output (default=no)]),
              [AM_CONDITIONAL([FCOLOR], [enable_fcolor=yes])],
              [AM_CONDITIONAL([FCOLOR], [test "$enable_fcolor" = "yes"])],
             )
AC_CHECK_LIB(z, compress, [], [AC_MSG_FAILURE(libz.so is required.)]) 
AC_CHECK_LIB(crypto, MD5_Init, [], [AC_MSG_FAILURE(libcrypto.so is required.)]) 
AC_CHECK_LIB(ssl, OPENSSL_init_ssl, [], [AC_MSG_FAILURE(libssl.so is required.)]) 
AC_CHECK_LIB(bsdxml, XML_ParserCreateNS, [], [AC_MSG_FAILURE(libbsdxml.so is required.)]) 
# pass LDFLAGS=-L${LOCALBASE}/lib in the environment
AC_CHECK_LIB(mrkcommon, _fini, [], [AC_MSG_FAILURE(libmrkcommon.so is required. Please find it at git@github.com:mkushnir/mrkcommon.git)]) 
AC_OUTPUT
