#!/bin/sh
# mkjaclgame.sh -- bundle a JACL game into a one-file .jaclgame package for the
# iPad app: a zip of the .j2 plus its .blorb (if any), with flat entry names.
#
#   ./mkjaclgame.sh path/to/grail.j2   ->   path/to/grail.jaclgame
#
# The package is an ordinary zip (so `unzip` opens it too); the app unpacks it
# into the .j2 + .blorb on import. Distribute the single .jaclgame -- it opens
# in the app via the Files picker, "Open in JACL", AirDrop, Mail or Safari.

set -e

j2="$1"
if [ -z "$j2" ] || [ ! -f "$j2" ]; then
    echo "usage: $0 GAME.j2   (bundles a sibling GAME.blorb if present)" >&2
    exit 1
fi
case "$j2" in
    *.j2) ;;
    *) echo "$0: expected a .j2 file, got '$j2'" >&2; exit 1 ;;
esac

base="${j2%.j2}"
out="$base.jaclgame"
rm -f "$out"

if [ -f "$base.blorb" ]; then
    zip -j -q "$out" "$j2" "$base.blorb"
    echo "Created $out  (.j2 + .blorb)"
else
    zip -j -q "$out" "$j2"
    echo "Created $out  (.j2 only)"
fi
