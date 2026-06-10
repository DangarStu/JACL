#!/bin/sh
# build-jaclgames.sh -- build .jaclgame download packages for the iPad app,
# one per published game (constant game_publish true).
#
#   build-jaclgames.sh [PROJECTS_DIR] [OUT_DIR] [CJACL]
#
# Defaults: PROJECTS_DIR=../projects  OUT_DIR=$PROJECTS_DIR/jaclgames
#           CJACL=../bin/cjacl
#
# Each package is a zip (flat entries) of the game's release .j2 plus its
# .blorb, if any. The app imports it via "Open in JACL" and unpacks it.

set -e

projects="${1:-../projects}"
out="${2:-$projects/jaclgames}"
cjacl="${3:-../bin/cjacl}"

if [ ! -x "$cjacl" ]; then
    echo "build-jaclgames: interpreter '$cjacl' not found/executable" >&2
    exit 1
fi

mkdir -p "$out"
n=0
for game in "$projects"/*.jacl; do
    [ -e "$game" ] || continue
    grep -qE '^constant[[:space:]]+game_publish[[:space:]]+true' "$game" || continue

    name=$(basename "$game" .jacl)

    # Build the release (debug-free, obfuscated) .j2 -- written to the temp
    # dir beside the game source.
    "$cjacl" "$game" -release </dev/null >/dev/null 2>&1 || true
    j2="$projects/temp/$name.j2"
    if [ ! -f "$j2" ]; then
        echo "  skip $name (no .j2 produced)"
        continue
    fi

    pkg="$out/$name.jaclgame"
    rm -f "$pkg"
    tmp=$(mktemp -d)
    cp "$j2" "$tmp/$name.j2"
    if [ -f "$projects/$name.blorb" ]; then
        cp "$projects/$name.blorb" "$tmp/$name.blorb"
        ( cd "$tmp" && zip -j -q "$name.jaclgame" "$name.j2" "$name.blorb" )
    else
        ( cd "$tmp" && zip -j -q "$name.jaclgame" "$name.j2" )
    fi
    mv "$tmp/$name.jaclgame" "$pkg"
    rm -rf "$tmp"
    n=$((n + 1))
    echo "  $name.jaclgame"
done
echo "build-jaclgames: $n package(s) in $out"
