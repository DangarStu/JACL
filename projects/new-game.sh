#!/usr/bin/env bash
# Bootstrap a new JACL game.
#
# Creates <name>.jacl and <name>.media alongside this script with a
# minimal functional skeleton: one starting location, web interface,
# standard verbs, and mapping support wired up.
#
# Usage: ./new-game.sh <game-name>
#   e.g. ./new-game.sh treasure-hunt
#
# Afterwards, edit the generated files:
#   - Fill in game_title, game_author, ifid at the top of <name>.jacl
#   - Flesh out the starting location's description and add more
#     locations with x/y grid coordinates
#   - Add any game-specific images/assets to <name>.media

set -euo pipefail

if [[ $# -lt 1 ]]; then
    echo "Usage: $0 <game-name>" >&2
    exit 1
fi

name="$1"
dir="$(cd "$(dirname "$0")" && pwd)"
jacl_file="$dir/$name.jacl"
media_file="$dir/$name.media"

if [[ -e "$jacl_file" || -e "$media_file" ]]; then
    echo "Refusing to overwrite existing file(s):" >&2
    [[ -e "$jacl_file" ]] && echo "  $jacl_file" >&2
    [[ -e "$media_file" ]] && echo "  $media_file" >&2
    exit 1
fi

title_cased="$(echo "$name" | tr '-' ' ' | awk '{for(i=1;i<=NF;i++) $i=toupper(substr($i,1,1)) substr($i,2)}1')"
ifid="JACL-$(date +%Y%m%d%H%M)"

cat > "$jacl_file" <<EOF
#!../bin/jacl
# =============================================================================
# $title_cased
# =============================================================================

constant game_title     "$title_cased"
constant game_author    "YOUR NAME"
constant game_release   1
constant game_build     1
constant ifid           "$ifid"

# Web interface styling (see webinterface.library for defaults).
constant title_image    "none"
constant footer_image   "none"
constant header_colour  "#42596d"
constant linkbar_colour "#6a7a87"
constant maintext_colour "#eeeeee"

# -----------------------------------------------------------------------------
# LOCATIONS
# -----------------------------------------------------------------------------
# Each location gets x/y grid coordinates so the map command can render
# it. Adjacent rooms should differ by 1 unit along a single axis for
# clean orthogonal exit lines; use diagonals for NE/NW/SE/SW exits.

location start : start
 short     name "Start"
 has       KNOWN
 x         0
 y         0

{look
write "You are at the starting location of your new game. "
write "Edit this description and add more locations around it.^"
}

# -----------------------------------------------------------------------------
# PLAYER
# -----------------------------------------------------------------------------

object player : player yourself me
 short    name "yourself"
 parent   start
 has      ANIMATE

# -----------------------------------------------------------------------------
# BOOTSTRAP
# -----------------------------------------------------------------------------

{+bootstrap
execute "+intro"
execute "+display_location"
}

{+intro
style note
write game_title
style normal
write ", a game by " game_author ".^^"
}

# -----------------------------------------------------------------------------
# INCLUDES
# -----------------------------------------------------------------------------

#include "webinterface.library"
#include "webinterface.css"
#include "verbs.library"
#include "mapping.library"
EOF

cat > "$media_file" <<'EOF'
/include/jquery-3.6.0.min.js application/javascript www/jquery-3.6.0.min.js
/include/raphael.min.js application/javascript www/raphael.min.js
/images/favicon.ico image/x-icon images/favicon.ico
EOF

echo "Created:"
echo "  $jacl_file"
echo "  $media_file"
echo
echo "Next steps:"
echo "  1. Edit $name.jacl -- fill in game_author, flesh out the starting"
echo "     location, add more rooms with x/y coordinates for the map."
echo "  2. Add any game-specific images/assets to $name.media."
echo "  3. Run the game with: cgijacl (or whichever interpreter you use)."
