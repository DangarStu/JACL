AC_INIT([JACL], [4.0], [https://github.com/DangarStu/JACL/issues])
AC_CONFIG_SRCDIR([jacl.c])

# Check for programs
AC_PROG_CC
AC_PROG_RANLIB
AC_CHECK_TOOL([AR], [ar], [ar])

# Common flags - use user's CFLAGS if set, otherwise default
if test "x$USER_CFLAGS_SET" = "x"; then
    CFLAGS="-Wall -O2"
fi

# Suppress warnings that are false positives or from third-party code.
# These flags are gcc-specific so check before adding them. The test
# uses -Werror so clang's "unknown warning option" notice -- which is
# only a warning by default and would slip past AC_COMPILE_IFELSE --
# becomes a hard failure and we drop the unsupported flag.
for wflag in -Wno-unused-result -Wno-stringop-truncation -Wno-unused-but-set-variable; do
    save_CFLAGS="$CFLAGS"
    CFLAGS="$CFLAGS $wflag -Werror"
    AC_MSG_CHECKING([whether $CC supports $wflag])
    AC_COMPILE_IFELSE([AC_LANG_PROGRAM()],
        [AC_MSG_RESULT([yes]); CFLAGS="$save_CFLAGS $wflag"],
        [AC_MSG_RESULT([no]); CFLAGS="$save_CFLAGS"])
done

# -----------------------------------------------------------
# Language selection
# -----------------------------------------------------------
AC_ARG_WITH([language],
    [AS_HELP_STRING([--with-language=LANG],
        [set interpreter language: english (default), french, german, indonesian, spanish])],
    [jacl_language=$withval],
    [jacl_language=english])

case "$jacl_language" in
    english)     LANGUAGE_FLAG="-DNATIVE_LANGUAGE=1" ;;
    german)      LANGUAGE_FLAG="-DNATIVE_LANGUAGE=2" ;;
    french)      LANGUAGE_FLAG="-DNATIVE_LANGUAGE=3" ;;
    indonesian)  LANGUAGE_FLAG="-DNATIVE_LANGUAGE=4" ;;
    spanish)     LANGUAGE_FLAG="-DNATIVE_LANGUAGE=5" ;;
    *)  AC_MSG_ERROR([unknown language "$jacl_language" (choose: english, french, german, indonesian, spanish)]) ;;
esac

CFLAGS="$CFLAGS $LANGUAGE_FLAG"
AC_MSG_NOTICE([interpreter language: $jacl_language])

# Directories for bundled libraries
CGIHTML_DIR=cgihtml-1.69
GLKTERM_DIR=glkterm
AC_SUBST([CGIHTML_DIR])
AC_SUBST([GLKTERM_DIR])

# Common source files
COMMON_SRC="findroute.c interpreter.c loader.c logging.c parser.c display.c utils.c jpp.c resolvers.c errors.c encapsulate.c libcsv.c"
AC_SUBST([COMMON_SRC])

# -----------------------------------------------------------
# Check for math library (always required)
# -----------------------------------------------------------
AC_SEARCH_LIBS([floor], [m], [], [AC_MSG_ERROR([math library is required])])

# -----------------------------------------------------------
# Check for ncurses (needed for jacl / glkterm)
# -----------------------------------------------------------
NCURSES_CFLAGS=""
NCURSES_LIBS=""
HAVE_NCURSES=no

for ncurses_dir in /opt/homebrew /usr/local /usr ""; do
    if test -n "$ncurses_dir"; then
        test_cflags="-I$ncurses_dir/include"
        test_ldflags="-L$ncurses_dir/lib"
    else
        test_cflags=""
        test_ldflags=""
    fi

    save_CFLAGS="$CFLAGS"
    save_LDFLAGS="$LDFLAGS"
    CFLAGS="$CFLAGS $test_cflags"
    LDFLAGS="$LDFLAGS $test_ldflags"

    AC_CHECK_LIB([ncurses], [initscr], [
        HAVE_NCURSES=yes
        if test -n "$ncurses_dir" && test "$ncurses_dir" != "/usr"; then
            NCURSES_CFLAGS="$test_cflags"
            NCURSES_LIBS="$test_ldflags -lncurses"
        else
            NCURSES_LIBS="-lncurses"
        fi
    ], [], [])

    CFLAGS="$save_CFLAGS"
    LDFLAGS="$save_LDFLAGS"

    if test "$HAVE_NCURSES" = "yes"; then
        break
    fi
done

AC_SUBST([NCURSES_CFLAGS])
AC_SUBST([NCURSES_LIBS])

if test "$HAVE_NCURSES" = "yes"; then
    AC_MSG_NOTICE([ncurses found -- will build jacl])
else
    AC_MSG_WARN([ncurses not found -- jacl will not be built])
fi

# -----------------------------------------------------------
# Check for libfcgi (needed for fcgijacl)
# -----------------------------------------------------------
FCGI_CFLAGS=""
FCGI_LIBS=""
HAVE_FCGI=no

# Try common include and lib path combinations. On Debian/Ubuntu the
# headers from libfcgi-dev are installed to /usr/include/fastcgi/
# rather than /usr/include/.
for fcgi_inc in /opt/homebrew/include /usr/local/include /usr/include/fastcgi /usr/include ""; do
    for fcgi_lib in /opt/homebrew/lib /usr/local/lib ""; do
        if test -n "$fcgi_inc"; then
            test_cflags="-I$fcgi_inc"
        else
            test_cflags=""
        fi
        if test -n "$fcgi_lib"; then
            test_ldflags="-L$fcgi_lib"
        else
            test_ldflags=""
        fi

        save_CFLAGS="$CFLAGS"
        save_LDFLAGS="$LDFLAGS"
        save_LIBS="$LIBS"
        CFLAGS="$CFLAGS $test_cflags"
        LDFLAGS="$LDFLAGS $test_ldflags"
        LIBS="-lfcgi $LIBS"

        AC_MSG_CHECKING([for fcgi_stdio.h and libfcgi in ($test_cflags $test_ldflags)])
        AC_LINK_IFELSE([AC_LANG_PROGRAM([[#include <fcgi_stdio.h>]],
                                        [[FCGI_Accept();]])],
            [HAVE_FCGI=yes
             AC_MSG_RESULT([yes])
             FCGI_CFLAGS="$test_cflags"
             if test -n "$test_ldflags"; then
                 FCGI_LIBS="$test_ldflags -lfcgi"
             else
                 FCGI_LIBS="-lfcgi"
             fi],
            [AC_MSG_RESULT([no])])

        CFLAGS="$save_CFLAGS"
        LDFLAGS="$save_LDFLAGS"
        LIBS="$save_LIBS"

        if test "$HAVE_FCGI" = "yes"; then
            break
        fi
    done
    if test "$HAVE_FCGI" = "yes"; then
        break
    fi
done

AC_SUBST([FCGI_CFLAGS])
AC_SUBST([FCGI_LIBS])

if test "$HAVE_FCGI" = "yes"; then
    AC_MSG_NOTICE([libfcgi found -- will build fcgijacl])
else
    AC_MSG_WARN([libfcgi not found -- fcgijacl will not be built])
fi

# -----------------------------------------------------------
# Optional Google Sign-In dependencies for fcgijacl / cgijacl.
# Requires libcurl + jansson + openssl. Without all three present
# fcgijacl still builds, but auth_stub.c provides no-op auth_*
# entry points and the anonymous flow is the only one available.
# -----------------------------------------------------------
AUTH_CFLAGS=""
AUTH_LIBS=""
AUTH_SRC="auth_stub.c"
HAVE_GOOGLE_AUTH=no

AC_CHECK_PROG([HAVE_PKG_CONFIG], [pkg-config], [yes], [no])
if test "$HAVE_PKG_CONFIG" = "yes"; then
    if pkg-config --exists libcurl jansson openssl 2>/dev/null; then
        save_CFLAGS="$CFLAGS"
        save_LIBS="$LIBS"
        AUTH_CFLAGS=`pkg-config --cflags libcurl jansson openssl`
        AUTH_LIBS=`pkg-config --libs   libcurl jansson openssl`
        CFLAGS="$CFLAGS $AUTH_CFLAGS"
        LIBS="$AUTH_LIBS $LIBS"
        AC_MSG_CHECKING([whether auth.c builds against libcurl/jansson/openssl])
        AC_LINK_IFELSE([AC_LANG_PROGRAM([[
            #include <curl/curl.h>
            #include <jansson.h>
            #include <openssl/evp.h>
            #include <openssl/hmac.h>
        ]], [[
            curl_easy_init(); json_loads("",0,0); EVP_sha256();
        ]])],
        [HAVE_GOOGLE_AUTH=yes; AUTH_SRC="auth.c"; AC_MSG_RESULT([yes])],
        [AC_MSG_RESULT([no])])
        CFLAGS="$save_CFLAGS"
        LIBS="$save_LIBS"
    fi
fi

if test "$HAVE_GOOGLE_AUTH" != "yes"; then
    AUTH_CFLAGS=""
    AUTH_LIBS=""
fi

AC_SUBST([AUTH_CFLAGS])
AC_SUBST([AUTH_LIBS])
AC_SUBST([AUTH_SRC])

if test "$HAVE_GOOGLE_AUTH" = "yes"; then
    AC_MSG_NOTICE([libcurl + jansson + openssl found -- Google Sign-In enabled in fcgijacl])
else
    AC_MSG_NOTICE([Google Sign-In disabled (need libcurl, jansson, openssl)])
    AC_MSG_NOTICE([  Debian/Ubuntu: sudo apt install libcurl4-openssl-dev libjansson-dev libssl-dev])
fi

# -----------------------------------------------------------
# Build the target list
# -----------------------------------------------------------
BUILD_TARGETS="bjorb jpp cgijacl"
INSTALL_CMDS="cp bjorb jpp cgijacl ../bin/; install bjorb jpp cgijacl /usr/local/bin/"

if test "$HAVE_NCURSES" = "yes"; then
    BUILD_TARGETS="$BUILD_TARGETS jacl"
    INSTALL_CMDS="$INSTALL_CMDS; cp jacl ../bin/; install jacl /usr/local/bin/"
fi

if test "$HAVE_FCGI" = "yes"; then
    BUILD_TARGETS="$BUILD_TARGETS fcgijacl"
    INSTALL_CMDS="$INSTALL_CMDS; cp fcgijacl ../bin/; install fcgijacl /usr/local/bin/"
fi

AC_SUBST([BUILD_TARGETS])
AC_SUBST([INSTALL_CMDS])

# -----------------------------------------------------------
# Generate output files
# -----------------------------------------------------------
AC_CONFIG_FILES([Makefile glkterm/Makefile])
AC_OUTPUT

# -----------------------------------------------------------
# Summary
# -----------------------------------------------------------
echo ""
echo "JACL $PACKAGE_VERSION configured."
echo ""
echo "  Interpreters to build:"
echo "    bjorb         yes (utility)"
echo "    jpp           yes (preprocessor utility)"
echo "    cgijacl       yes (CGI web)"
if test "$HAVE_NCURSES" = "yes"; then
echo "    jacl          yes (GlkTerm console)"
else
echo "    jacl          no  (ncurses not found)"
fi
if test "$HAVE_FCGI" = "yes"; then
echo "    fcgijacl      yes (FastCGI web)"
else
echo "    fcgijacl      no  (libfcgi not found)"
fi
echo ""
echo "  Language: $jacl_language"
echo ""
echo "  Type 'make' to build, 'make install' to install to ../bin"
echo ""
