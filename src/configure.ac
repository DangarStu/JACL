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

# -----------------------------------------------------------
# Language selection
# -----------------------------------------------------------
AC_ARG_WITH([language],
    [AS_HELP_STRING([--with-language=LANG],
        [set interpreter language: english (default), french, german, indonesian])],
    [jacl_language=$withval],
    [jacl_language=english])

case "$jacl_language" in
    english)     LANGUAGE_FLAG="-DNATIVE_LANGUAGE=1" ;;
    german)      LANGUAGE_FLAG="-DNATIVE_LANGUAGE=2" ;;
    french)      LANGUAGE_FLAG="-DNATIVE_LANGUAGE=3" ;;
    indonesian)  LANGUAGE_FLAG="-DNATIVE_LANGUAGE=4" ;;
    *)  AC_MSG_ERROR([unknown language "$jacl_language" (choose: english, french, german, indonesian)]) ;;
esac

CFLAGS="$CFLAGS $LANGUAGE_FLAG"
AC_MSG_NOTICE([interpreter language: $jacl_language])

# Directories for bundled libraries
CGIHTML_DIR=cgihtml-1.69
GLKTERM_DIR=glkterm
CHEAPGLK_DIR=CheapGlk
AC_SUBST([CGIHTML_DIR])
AC_SUBST([GLKTERM_DIR])
AC_SUBST([CHEAPGLK_DIR])

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

for fcgi_dir in /opt/homebrew /usr/local /usr ""; do
    if test -n "$fcgi_dir"; then
        test_cflags="-I$fcgi_dir/include"
        test_ldflags="-L$fcgi_dir/lib"
    else
        test_cflags=""
        test_ldflags=""
    fi

    save_CFLAGS="$CFLAGS"
    save_LDFLAGS="$LDFLAGS"
    CFLAGS="$CFLAGS $test_cflags"
    LDFLAGS="$LDFLAGS $test_ldflags"

    AC_CHECK_LIB([fcgi], [FCGI_Accept], [
        HAVE_FCGI=yes
        if test -n "$fcgi_dir" && test "$fcgi_dir" != "/usr"; then
            FCGI_CFLAGS="$test_cflags"
            FCGI_LIBS="$test_ldflags -lfcgi"
        else
            FCGI_LIBS="-lfcgi"
        fi
    ], [], [])

    CFLAGS="$save_CFLAGS"
    LDFLAGS="$save_LDFLAGS"

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
# Build the target list
# -----------------------------------------------------------
BUILD_TARGETS="bjorb cjacl cgijacl"
INSTALL_CMDS="cp bjorb cjacl cgijacl ../bin/; install bjorb cjacl cgijacl /usr/local/bin/"

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
AC_CONFIG_FILES([Makefile glkterm/Makefile CheapGlk/Makefile])
AC_OUTPUT

# -----------------------------------------------------------
# Summary
# -----------------------------------------------------------
echo ""
echo "JACL $PACKAGE_VERSION configured."
echo ""
echo "  Interpreters to build:"
echo "    bjorb         yes (utility)"
echo "    cjacl         yes (CheapGlk console)"
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
