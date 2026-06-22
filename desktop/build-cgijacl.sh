#!/bin/bash
# Build a current cgijacl for the desktop app into desktop/bin/.
#
# The repo's bin/cgijacl is root-owned and can be stale; its built-in web
# server's media matching (raphael, images) was broken in the installed copy.
# Rather than require sudo to reinstall, the Electron app uses its own freshly
# built binary. This mirrors what packaging (step 5) will do anyway.
#
# Flags/libs are derived from src/Makefile's cgijacl target. Re-run if the C
# core or webjacl.c changes.
set -e
cd "$(dirname "$0")/../src"
mkdir -p ../desktop/bin

JANSSON_INC=$(ls -d /opt/homebrew/Cellar/jansson/*/include 2>/dev/null | head -1)
JANSSON_LIB=$(ls -d /opt/homebrew/Cellar/jansson/*/lib 2>/dev/null | head -1)
SSL_INC=$(ls -d /opt/homebrew/Cellar/openssl@3/*/include 2>/dev/null | head -1)
SSL_LIB=$(ls -d /opt/homebrew/Cellar/openssl@3/*/lib 2>/dev/null | head -1)

gcc -std=gnu23 -Wall -O2 -Wno-unused-result -Wno-unused-but-set-variable \
  -DNATIVE_LANGUAGE=1 -DWEBJACL \
  -I"$JANSSON_INC" -I"$SSL_INC" \
  cgijacl.c auth.c findroute.c interpreter.c loader.c logging.c parser.c \
  display.c utils.c jpp.c resolvers.c errors.c encapsulate.c libcsv.c saver.c webjacl.c \
  -Icgihtml-1.69 -Iwebjacl -Lcgihtml-1.69 -lcgihtml -lcurl \
  -L"$JANSSON_LIB" -ljansson -L"$SSL_LIB" -lssl -lcrypto -lm \
  -o ../desktop/bin/cgijacl

echo "built desktop/bin/cgijacl"
