#!/usr/bin/env bash
# Stage the data a packaged build bundles, into desktop/staging/jacl-data/:
#   games/        only PUBLISHED games (.j2) + games.json (title/language) + data/
#   include/ www/ images/ sounds/   the shared web assets cgijacl serves
# electron-builder copies this to the app's resources (see package.json). Run by
# the "prepare-bundle" npm script before packaging.
set -euo pipefail
cd "$(dirname "$0")/.."                      # repo root
STAGE="desktop/staging/jacl-data"
rm -rf desktop/staging
mkdir -p "$STAGE/games"

# A fresh checkout has no projects/temp/*.j2 (*.j2 is gitignored), so build the
# preprocessor and compile published games on demand below. jpp writes the
# release .j2 to projects/temp/<name>.j2. Build jpp directly (its src/Makefile is
# configure-generated and absent on a clean checkout); flags mirror the jpp target.
[ -x src/jpp ] || ( cd src && gcc -std=gnu2x -O2 -DNATIVE_LANGUAGE=1 -DGLK jppmain.c jpp.c -Iglkterm -o jpp )
mkdir -p projects/temp

cp -R projects/include "$STAGE/include"
cp -R projects/www     "$STAGE/www"
cp -R projects/images  "$STAGE/images"
cp -R projects/sounds  "$STAGE/sounds"
[ -d projects/temp/data ] && cp -R projects/temp/data "$STAGE/games/data" || true

prettify() { echo "$1" | sed 's/[_-]\{1,\}/ /g' | awk '{for(i=1;i<=NF;i++)$i=toupper(substr($i,1,1)) substr($i,2)}1'; }
language() {
  case "$1" in
    *indonesia*|*desa*)                       echo Indonesian ;;
    *german*|*deutsch*)                       echo German ;;
    *french*|*francais*|*paris*|*camion*|*fihnarga*) echo French ;;
    *spanish*|*espanol*|*montana*|*vida*)     echo Spanish ;;
    *)                                        echo English ;;
  esac
}

# Same rule as the website (etc/gen-landing.sh): a game ships iff its source
# declares `constant game_publish true`.
manifest="$STAGE/games/games.json"
echo "[" > "$manifest"; first=1; n=0
for src in projects/*.jacl; do
  s=$(basename "$src" .jacl)
  grep -qE '^constant[[:space:]]+game_publish[[:space:]]+true' "$src" || continue
  # Always recompile: the web CSS/JS (webinterface.css/.library) are #include'd
  # INTO the .j2 at compile time, so a stale projects/temp/<game>.j2 would ship
  # the old assets even after those source files change. Force a fresh build.
  rm -f "projects/temp/$s.j2"
  src/jpp "$src" -release >/dev/null 2>&1 || true
  [ -f "projects/temp/$s.j2" ] || { echo "  skip $s (no .j2 produced)"; continue; }
  cp "projects/temp/$s.j2" "$STAGE/games/$s.j2"
  [ $first -eq 1 ] || echo "," >> "$manifest"; first=0; n=$((n+1))
  printf '  {"file":"%s.j2","title":"%s","language":"%s"}' \
    "$s" "$(prettify "$s")" "$(language "$s")" >> "$manifest"
done
printf '\n]\n' >> "$manifest"
echo "staged $n published games + assets into $STAGE"
