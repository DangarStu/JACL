#!/bin/sh
# Pulls OFFSIDE's live data (the offside-*.csv files on the Dangar Marine VM)
# down to this Mac, one dated folder per day, keeping the last 30 days. Run
# nightly by launchd (au.com.dangarmarine.offside-backup.plist, next to this
# file); safe to run by hand at any time:
#
#   sh etc/dangarmarine/backup-offside.sh
#
# Uses the stuart account's SSH key, which can read the data folder (it's in
# the www-data group). A day's folder is replaced if it's run again that day.
set -e

HOST=stuart@dmsyd1.dangarmarine.com.au
DATA=/usr/local/jacl/projects/data
DEST="$HOME/Backups/OFFSIDE"
KEEP_DAYS=30

DAY=$(date +%Y-%m-%d)
mkdir -p "$DEST"
TMP=$(mktemp -d "$DEST/.incoming.XXXXXX")
trap 'rm -rf "$TMP"' EXIT

# BatchMode: FAIL RATHER THAN WAIT FOR A PASSWORD THAT NOBODY WILL TYPE
rsync -rt -e "ssh -o BatchMode=yes -o ConnectTimeout=20" \
    "$HOST:$DATA/offside-*.csv" "$TMP/"

# A COPY WITHOUT THE COMPETITIONS FILE MEANS SOMETHING IS WRONG; DON'T LET IT
# REPLACE A GOOD DAY OR COUNT TOWARDS THE 30
[ -s "$TMP/offside-competitions.csv" ] || { echo "$(date): no competitions file in the copy, not kept" >&2; exit 1; }

rm -rf "${DEST:?}/$DAY"
mv "$TMP" "$DEST/$DAY"
trap - EXIT

# DROP DAYS OLDER THAN KEEP_DAYS (THE FOLDER NAMES ARE THE DATES)
CUTOFF=$(date -v-${KEEP_DAYS}d +%Y-%m-%d)
for d in "$DEST"/20??-??-??; do
    [ -d "$d" ] || continue
    [ "$(basename "$d")" \< "$CUTOFF" ] && rm -rf "$d"
done

echo "$(date): backed up $(ls "$DEST/$DAY" | wc -l | tr -d ' ') files to $DEST/$DAY"
