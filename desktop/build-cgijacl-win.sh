#!/usr/bin/env bash
# Cross-compile a native Windows cgijacl.exe (Winsock, NO Cygwin) with mingw-w64.
#
# Mirrors build-cgijacl.sh: same source list, same auth_stub.c (so there are NO
# jansson/openssl/curl deps), but targets mingw and links -lws2_32 for Winsock.
# Every Windows-specific change in the C sources is behind #ifdef _WIN32, so this
# does not affect the POSIX build at all.
#
# Works both cross-compiling on macOS/Linux (brew install mingw-w64) and natively
# on a Windows runner whose gcc targets mingw.
# Output: desktop/bin/cgijacl.exe
set -euo pipefail
cd "$(dirname "$0")/../src"
mkdir -p ../desktop/bin

# Prefer the cross driver (x86_64-w64-mingw32-gcc) when cross-compiling; on a
# native Windows/MinGW runner fall back to a plain gcc that targets mingw.
if command -v x86_64-w64-mingw32-gcc >/dev/null 2>&1; then
  CC=x86_64-w64-mingw32-gcc
elif command -v gcc >/dev/null 2>&1 && gcc -dumpmachine 2>/dev/null | grep -q mingw; then
  CC=gcc
else
  echo "error: no mingw compiler (x86_64-w64-mingw32-gcc, or a mingw-targeting gcc)." >&2
  echo "       On macOS: brew install mingw-w64" >&2
  exit 1
fi

# cgijacl links the cgihtml CGI helpers. Compile cgihtml's objects (they need
# -DUNIX) and link them directly as object files -- NOT a static archive -- so we
# never need a mingw ar/ranlib (the windows-latest mingw ships the gcc driver but
# not x86_64-w64-mingw32-ar). We do NOT pass -DWINDOWS: cgihtml's legacy WINDOWS
# branch references an undefined BINARY macro and is only reached by the
# multipart-upload path, which webjacl never uses (it answers POST with 501).
CGIHTML_OBJS=""
for obj in string-lib cgi-llist cgi-lib html-lib; do
  "$CC" -O -Wall -DUNIX -c -o "cgihtml-1.69/$obj.o" "cgihtml-1.69/$obj.c"
  CGIHTML_OBJS="$CGIHTML_OBJS cgihtml-1.69/$obj.o"
done

# Same source list + auth_stub.c as build-cgijacl.sh. Link -lws2_32 for Winsock
# (socket/recv/send/WSAStartup) used by webjacl.c on Windows.
"$CC" -std=gnu2x -Wall -O2 -Wno-unused-result -Wno-unused-but-set-variable \
  -DNATIVE_LANGUAGE=1 -DWEBJACL \
  cgijacl.c auth_stub.c findroute.c interpreter.c loader.c logging.c parser.c \
  display.c utils.c jpp.c resolvers.c errors.c encapsulate.c libcsv.c saver.c webjacl.c \
  $CGIHTML_OBJS \
  -Icgihtml-1.69 -Iwebjacl -lm -lws2_32 \
  -o ../desktop/bin/cgijacl.exe

echo "built desktop/bin/cgijacl.exe"
