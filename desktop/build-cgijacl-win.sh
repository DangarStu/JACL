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

# cgijacl links -lcgihtml; cgihtml-1.69 has a self-contained Makefile (no
# configure). Build it with the cross compiler (and the matching cross ranlib,
# so the host ranlib doesn't choke on the PE/COFF archive). We do NOT pass
# -DWINDOWS: cgihtml's legacy WINDOWS branch references an undefined BINARY
# macro and is only reached by the multipart-upload path, which webjacl never
# uses (it answers POST with 501). The default branch cross-compiles cleanly.
make -C cgihtml-1.69 clean >/dev/null 2>&1 || true
make -C cgihtml-1.69 libcgihtml.a CC="$CROSS" RANLIB="$CROSS_RANLIB"

# Same source list + auth_stub.c as build-cgijacl.sh. Link -lws2_32 for Winsock
# (socket/recv/send/WSAStartup) used by webjacl.c on Windows.
"$CROSS" -std=gnu2x -Wall -O2 -Wno-unused-result -Wno-unused-but-set-variable \
  -DNATIVE_LANGUAGE=1 -DWEBJACL \
  cgijacl.c auth_stub.c findroute.c interpreter.c loader.c logging.c parser.c \
  display.c utils.c jpp.c resolvers.c errors.c encapsulate.c libcsv.c saver.c webjacl.c \
  -Icgihtml-1.69 -Iwebjacl -Lcgihtml-1.69 -lcgihtml -lm -lws2_32 \
  -o ../desktop/bin/cgijacl.exe

echo "built desktop/bin/cgijacl.exe"
