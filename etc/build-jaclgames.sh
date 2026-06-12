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

# Pick a zip implementation up front. A .jaclgame is a flat ZIP the app's
# MiniZip reader unpacks. Prefer zip(1); fall back to python3's zipfile
# module (present on essentially every Ubuntu) so a minimal server without
# the 'zip' package can still build packages -- both write the same flat
# DEFLATE archive. Checking here turns a missing archiver into one clear
# message instead of a set -e abort partway through the first game.
#   mkzip ARCHIVE FILE...   (run from the directory holding FILEs)
# Paths are preserved (NOT junked): the .j2/.blorb are bare names at the root,
# but a bundled dictionary lives at data/<lang>_words.csv and must keep that
# prefix so the interpreter finds it under the game's data/ dir.
if command -v zip >/dev/null 2>&1; then
    mkzip() { zip -q -X "$@"; }
elif command -v python3 >/dev/null 2>&1; then
    mkzip() { python3 -m zipfile -c "$@"; }
else
    echo "build-jaclgames: need 'zip' or 'python3' to build .jaclgame packages." >&2
    echo "  install one, e.g.: apt-get install -y zip" >&2
    exit 1
fi

mkdir -p "$out"
n=0
for game in "$projects"/*.jacl; do
    [ -e "$game" ] || continue
    grep -qE '^constant[[:space:]]+game_publish[[:space:]]+true' "$game" || continue

    name=$(basename "$game" .jacl)

    # Skip web-only games (constant game_web_only true) -- they need the web
    # HTML interface (e.g. blackjacl renders its cards in HTML), so they don't
    # run on the Glk-based iPad/console interpreters. Don't build a package, and
    # drop any stale one so it disappears from the iPad downloads tab.
    if grep -qE '^constant[[:space:]]+game_web_only[[:space:]]+true' "$game"; then
        rm -f "$out/$name.jaclgame"
        echo "  skip $name (web-only)"
        continue
    fi

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
    files="$name.j2"
    if [ -f "$projects/$name.blorb" ]; then
        cp "$projects/$name.blorb" "$tmp/$name.blorb"
        files="$files $name.blorb"
    fi

    # Bundle the matching language dictionary for click-to-define. A game that
    # includes <lang>_verbs.library runs `iterate "<lang>_words.csv"`, which the
    # interpreter opens under the game's data/ dir -- so package the CSV at
    # data/<lang>_words.csv. (french / german / spanish / indonesian.)
    for lang in french german spanish indonesian; do
        if grep -qE "#include[[:space:]]+\"?${lang}_verbs\.library" "$game" \
           && [ -f "$projects/data/${lang}_words.csv" ]; then
            mkdir -p "$tmp/data"
            cp "$projects/data/${lang}_words.csv" "$tmp/data/${lang}_words.csv"
            files="$files data/${lang}_words.csv"
        fi
    done

    ( cd "$tmp" && mkzip "$name.jaclgame" $files )
    mv "$tmp/$name.jaclgame" "$pkg"
    rm -rf "$tmp"
    n=$((n + 1))
    echo "  $name.jaclgame${files#$name.j2}"
done
echo "build-jaclgames: $n package(s) in $out"
