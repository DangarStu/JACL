#!/bin/sh
# Build the JACL Chordal Keyboard reference sheet.
#
# Source: ChordalKeyboard.html (single-file HTML with inline CSS and
# inline SVG for each D-Pad cluster). The legacy LibreOffice Draw
# source (.odg) is no longer used.
# Output: ChordalKeyboard.pdf.
#
# Dependencies: weasyprint (brew/apt/pip).

set -e

weasyprint --version
weasyprint ChordalKeyboard.html ChordalKeyboard.pdf
