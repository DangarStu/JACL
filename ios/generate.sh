#!/usr/bin/env bash
# Generate JACL.xcodeproj from project.yml. The .xcodeproj is gitignored —
# project.yml is the source of truth — so run this after pulling, after changing
# project.yml, or after adding/removing source files. (Mirrors Wryter's
# ios/generate.sh; JACL has no MacBridge, so it's a plain xcodegen generate.)
set -euo pipefail
cd "$(dirname "$0")"
xcodegen generate
