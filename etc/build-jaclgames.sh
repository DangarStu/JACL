#!/bin/sh
# build-jaclgames.sh -- build .jaclgame download packages for the iPad app,
# one per published game (constant game_publish true).
#
#   build-jaclgames.sh [PROJECTS_DIR] [OUT_DIR] [JPP]
#
# Defaults: PROJECTS_DIR=../projects  OUT_DIR=$PROJECTS_DIR/jaclgames
#           JPP=jpp  (the standalone preprocessor; resolved from PATH)
#
# JPP is the jpp(1) preprocessor utility, not a full interpreter: it just
# writes the release .j2 and exits, so there is no Glk/ncurses/tty to hang
# on and no game is loaded. It is a first-class build target (configure +
# make), so it is always rebuilt from the current core -- the #processed
# version stamp can never go stale the way a prebuilt cjacl does.
#
# Each package is a zip (flat entries) of the game's release .j2 plus its
# .blorb, if any. The app imports it via "Open in JACL" and unpacks it.

set -e

projects="${1:-../projects}"
out="${2:-$projects/jaclgames}"
jpp="${3:-jpp}"   # resolved from PATH (make install puts it in /usr/local/bin)

if ! command -v "$jpp" >/dev/null 2>&1; then
    echo "build-jaclgames: preprocessor '$jpp' not found in PATH." >&2
    echo "  It writes the release .j2 files; run 'make jpp' / 'make install'" >&2
    echo "  or pass the path to the jpp binary as arg 3." >&2
    exit 1
fi

mkdir -p "$out"
n=0
for game in "$projects"/*.jacl; do
    [ -e "$game" ] || continue
    grep -qE '^constant[[:space:]]+game_publish[[:space:]]+true' "$game" || continue

    name=$(basename "$game" .jacl)

    # Preprocess the release (debug-free, obfuscated) .j2 -- written to the
    # temp dir beside the game source. jpp prints the output path on stdout;
    # let its stderr through so a real failure is visible (the no-.j2 check
    # below then skips the game cleanly).
    "$jpp" "$game" -release >/dev/null || true
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
