#!/usr/bin/env bash
# Cross-compile a native Windows cgijacl.exe (Winsock, NO Cygwin) with mingw-w64.
#
# Mirrors build-cgijacl.sh: same source list, same auth_stub.c (so there are NO
# jansson/openssl/curl deps), but targets x86_64-w64-mingw32 and links -lws2_32
# for Winsock. Every Windows-specific change in the C sources is behind
# #ifdef _WIN32, so this does not affect the POSIX build at all.
#
# Prereq: brew install mingw-w64  (provides x86_64-w64-mingw32-gcc).
# Output: desktop/bin/cgijacl.exe
set -euo pipefail
cd "$(dirname "$0")/../src"
mkdir -p ../desktop/bin

CROSS_PREFIX=x86_64-w64-mingw32
CROSS="${CROSS_PREFIX}-gcc"
CROSS_RANLIB="${CROSS_PREFIX}-ranlib"

if ! command -v "$CROSS" >/dev/null 2>&1; then
  echo "error: $CROSS not found. Run: brew install mingw-w64" >&2
  exit 1
fi

# cgijacl links -lcgihtml. Build cgihtml-1.69 directly with the cross toolchain
# rather than via its Makefile: that Makefile hardcodes `ar cr` (not $(AR)), so
# the host (macOS) ar archives the PE/COFF objects with a symbol index the mingw
# linker can't read -> "undefined reference to cgi_val" at link time. Using the
# cross ar + ranlib produces a valid PE archive. We do NOT pass -DWINDOWS:
# cgihtml's legacy WINDOWS branch references an undefined BINARY macro and is
# only reached by the multipart-upload path, which webjacl never uses (it
# answers POST with 501). The default (-DUNIX) branch cross-compiles cleanly.
CROSS_AR="${CROSS_PREFIX}-ar"
( cd cgihtml-1.69
  rm -f ./*.o libcgihtml.a
  for obj in string-lib cgi-llist cgi-lib html-lib; do
    "$CROSS" -O -Wall -DUNIX -c -o "$obj.o" "$obj.c"
  done
  "$CROSS_AR" cr libcgihtml.a string-lib.o cgi-llist.o cgi-lib.o html-lib.o
  "$CROSS_RANLIB" libcgihtml.a
)

# Same source list + auth_stub.c as build-cgijacl.sh. Link -lws2_32 for Winsock
# (socket/recv/send/WSAStartup) used by webjacl.c on Windows.
"$CROSS" -std=gnu2x -Wall -O2 -Wno-unused-result -Wno-unused-but-set-variable \
  -DNATIVE_LANGUAGE=1 -DWEBJACL \
  cgijacl.c auth_stub.c findroute.c interpreter.c loader.c logging.c parser.c \
  display.c utils.c jpp.c resolvers.c errors.c encapsulate.c libcsv.c saver.c webjacl.c \
  -Icgihtml-1.69 -Iwebjacl -Lcgihtml-1.69 -lcgihtml -lm -lws2_32 \
  -o ../desktop/bin/cgijacl.exe

echo "built desktop/bin/cgijacl.exe"
