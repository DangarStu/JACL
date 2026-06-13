#!/bin/sh
# mkjaclgame.sh -- one-stop packager: turn a JACL game into a .jaclgame for the
# iPad app, preprocessing the source for you.
#
#   ./mkjaclgame.sh GAME.jacl [-release] [-o OUT.jaclgame]
#   ./mkjaclgame.sh GAME.j2   [-o OUT.jaclgame]     # already preprocessed
#
# Given a .jacl source it runs jpp(1) to produce the .j2, then bundles into
# GAME.jaclgame:
#   * the preprocessed GAME.j2;
#   * a sibling GAME.blorb, if present (images / sounds);
#   * for a non-English game, the matching <lang>_words.csv dictionary at
#     data/<lang>_words.csv -- the file the iPad's long-press "Define" reads.
#
# The language is detected from the game's own <lang>_verbs.library #include
# lines, so a new language just works once its words.csv exists. The CSV is
# looked for beside the game (its data/ subdir) and then in the repository's
# projects/data. Pass -release for an obfuscated, #debug-free build (the
# default is a debug build, which is easier to test with).
#
# The package is an ordinary zip (so `unzip` opens it too); the app unpacks it
# on import. Distribute the single .jaclgame -- it opens in the app via the
# Files picker, "Open in JACL", AirDrop, Mail or Safari.
#
# Needs jpp on PATH (or set $JPP), and `zip` or python3 to build the archive.

set -e

here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)

src=""
out=""
relflag=""
while [ $# -gt 0 ]; do
    case "$1" in
        -release)        relflag="-release" ;;
        -o)              shift; out="$1" ;;
        -h|--help)       sed -n '2,24p' "$0"; exit 0 ;;
        -*)              echo "$0: unknown option '$1'" >&2; exit 1 ;;
        *)               if [ -n "$src" ]; then
                             echo "$0: only one game file may be given" >&2; exit 1
                         fi
                         src="$1" ;;
    esac
    shift
done

if [ -z "$src" ] || [ ! -f "$src" ]; then
    echo "usage: $0 GAME.jacl [-release] [-o OUT.jaclgame]" >&2
    exit 1
fi

# Pick a zip implementation. A .jaclgame is a zip whose entries KEEP their
# paths (NOT junked): the .j2/.blorb sit at the root but a bundled dictionary
# must stay at data/<lang>_words.csv so the interpreter finds it under the
# game's data/ dir. zip(1) is preferred; python3's zipfile is the fallback.
if command -v zip >/dev/null 2>&1; then
    mkzip() { zip -q -X "$@"; }
elif command -v python3 >/dev/null 2>&1; then
    mkzip() { python3 -m zipfile -c "$@"; }
else
    echo "$0: need 'zip' or 'python3' to build the package." >&2
    exit 1
fi

case "$src" in
    *.j2)
        # Already preprocessed: package as-is. The matching .jacl source (if it
        # sits beside the .j2) is used only to detect the dictionary language.
        j2="$src"
        game="${src%.j2}.jacl"
        ;;
    *.jacl)
        # Preprocess with jpp. jpp prints the path of the .j2 it wrote on
        # stdout (temp/ beside the game, or the cwd as a fallback), so capture
        # that rather than guessing where it landed.
        JPP="${JPP:-}"
        if [ -z "$JPP" ]; then
            for cand in jpp "$here/../bin/jpp" "$here/../src/jpp"; do
                if command -v "$cand" >/dev/null 2>&1 || [ -x "$cand" ]; then
                    JPP="$cand"; break
                fi
            done
        fi
        if [ -z "$JPP" ] && [ -f "$here/../src/jppmain.c" ]; then
            # No prebuilt jpp: build it straight from src/. jpp links no Glk
            # library (just the two preprocessor units), so this is a quick,
            # dependency-free compile -- keeping this a true one-step tool even
            # on a fresh checkout that hasn't run `make install` yet.
            echo "jpp not found; building it from src/ ..." >&2
            if ( cd "$here/../src" && ${CC:-cc} ${CFLAGS:--O2} -DGLK \
                   jppmain.c jpp.c -Iglkterm -o jpp ) >&2; then
                JPP="$here/../src/jpp"
            fi
        fi
        if [ -z "$JPP" ]; then
            echo "$0: jpp not found and could not be built. Run 'make install' or set \$JPP." >&2
            exit 1
        fi
        game="$src"
        j2=$("$JPP" "$src" $relflag) || {
            echo "$0: preprocessing failed." >&2; exit 1
        }
        if [ ! -f "$j2" ]; then
            echo "$0: jpp reported '$j2' but it does not exist." >&2; exit 1
        fi
        ;;
    *)
        echo "$0: expected a .jacl or .j2 file, got '$src'" >&2
        exit 1
        ;;
esac

name=$(basename "$j2" .j2)
gamedir=$(dirname "$game")
out="${out:-$gamedir/$name.jaclgame}"

tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

cp "$j2" "$tmp/$name.j2"
files="$name.j2"
extras=""

# A sibling .blorb (named after the game) carries the game's images and sounds.
blorb="$gamedir/$name.blorb"
if [ -f "$blorb" ]; then
    cp "$blorb" "$tmp/$name.blorb"
    files="$files $name.blorb"
    extras="$extras + blorb"
fi

# Bundle the matching language dictionary for long-press "Define". Derive the
# language list from the game's own <lang>_verbs.library #include lines, then
# take the CSV from beside the game (its data/ dir) or the repo's projects/data.
if [ -f "$game" ]; then
    langs=$(grep -oE '#include[[:space:]]+"?[a-z]+_verbs\.library' "$game" 2>/dev/null \
            | grep -oE '[a-z]+_verbs\.library' | sed 's/_verbs\.library$//' | sort -u)
    for lang in $langs; do
        for csv in "$gamedir/data/${lang}_words.csv" "$here/../projects/data/${lang}_words.csv"; do
            if [ -f "$csv" ]; then
                mkdir -p "$tmp/data"
                cp "$csv" "$tmp/data/${lang}_words.csv"
                files="$files data/${lang}_words.csv"
                extras="$extras + ${lang} dictionary"
                break
            fi
        done
    done
fi

rm -f "$out"
( cd "$tmp" && mkzip "$name.jaclgame" $files )
mv "$tmp/$name.jaclgame" "$out"

echo "Created $out  (.j2$extras)"
