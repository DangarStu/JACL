#!/usr/bin/env bash
# Build a current cgijacl for the desktop app into desktop/bin/ (cgijacl is the
# engine -- it serves the full web JACL via its built-in web server).
#
# It's compiled from src/, so each OS builds its own (the CI desktop workflow runs
# this on macOS and Linux runners). The repo's installed bin/cgijacl is root-owned
# and was stale (broken media serving), so the app always uses its own build.
#
# Flags/libs mirror src/Makefile's cgijacl target. Re-run if the C core, webjacl.c
# or cgihtml change.
set -euo pipefail
cd "$(dirname "$0")/../src"
mkdir -p ../desktop/bin

# cgijacl links -lcgihtml; cgihtml-1.69 has a self-contained Makefile (no configure).
make -C cgihtml-1.69 libcgihtml.a

# Locate jansson / libcurl / openssl. Linux: pkg-config. macOS: Homebrew Cellar.
if [ "$(uname)" = "Darwin" ]; then
  J=$(brew --prefix jansson); S=$(brew --prefix openssl@3)
  DEP_CFLAGS="-I$J/include -I$S/include"
  DEP_LIBS="-L$J/lib -ljansson -L$S/lib -lssl -lcrypto -lcurl"
else
  DEP_CFLAGS=$(pkg-config --cflags jansson libcurl openssl)
  DEP_LIBS=$(pkg-config --libs jansson libcurl openssl)
fi

gcc -std=gnu23 -Wall -O2 -Wno-unused-result -Wno-unused-but-set-variable \
  -DNATIVE_LANGUAGE=1 -DWEBJACL $DEP_CFLAGS \
  cgijacl.c auth.c findroute.c interpreter.c loader.c logging.c parser.c \
  display.c utils.c jpp.c resolvers.c errors.c encapsulate.c libcsv.c saver.c webjacl.c \
  -Icgihtml-1.69 -Iwebjacl -Lcgihtml-1.69 -lcgihtml $DEP_LIBS -lm \
  -o ../desktop/bin/cgijacl

echo "built desktop/bin/cgijacl"
