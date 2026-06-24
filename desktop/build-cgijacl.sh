#!/usr/bin/env bash
# Build a current cgijacl for the desktop app into desktop/bin/ (cgijacl is the
# engine -- it serves the full web JACL via its built-in web server).
#
# It's compiled from src/, so each OS builds its own (the CI desktop workflow runs
# this on macOS and Linux runners). The repo's installed bin/cgijacl is root-owned
# and was stale (broken media serving), so the app always uses its own build.
#
# Like src/Makefile's cgijacl target but with auth_stub.c (no jansson/openssl/curl),
# so the binary has no Homebrew dylib deps. Re-run if the C core, webjacl.c or
# cgihtml change.
set -euo pipefail
cd "$(dirname "$0")/../src"
mkdir -p ../desktop/bin

# Universal (arm64 + x86_64) on macOS so the app ships ONE fat cgijacl that runs on
# both Apple Silicon and Intel Macs; native single-arch elsewhere (Linux CI).
ARCHFLAGS=""
[ "$(uname)" = "Darwin" ] && ARCHFLAGS="-arch arm64 -arch x86_64"

# cgijacl links -lcgihtml; cgihtml-1.69 has a self-contained Makefile (no configure).
# Rebuild it with the SAME arch flags -- a single-arch .a breaks the universal link
# and the @electron/universal merge.
make -C cgihtml-1.69 clean >/dev/null 2>&1 || true
make -C cgihtml-1.69 libcgihtml.a CC="cc $ARCHFLAGS"

# Build with auth_stub.c instead of auth.c. The desktop never uses Google Sign-In,
# and auth.c is the ONLY thing that needs jansson/openssl/curl -- which hard-link
# Homebrew dylibs (/opt/homebrew/opt/jansson, openssl@3) and stop the bundled binary
# from launching on a Mac without Homebrew. With the stub the desktop cgijacl links
# only the static -lcgihtml + libc/libm, so it runs on any clean machine.
gcc $ARCHFLAGS -std=gnu2x -Wall -O2 -Wno-unused-result -Wno-unused-but-set-variable \
  -DNATIVE_LANGUAGE=1 -DWEBJACL \
  cgijacl.c auth_stub.c findroute.c interpreter.c loader.c logging.c parser.c \
  display.c utils.c jpp.c resolvers.c errors.c encapsulate.c libcsv.c saver.c webjacl.c \
  -Icgihtml-1.69 -Iwebjacl -Lcgihtml-1.69 -lcgihtml -lm \
  -o ../desktop/bin/cgijacl

echo "built desktop/bin/cgijacl"
