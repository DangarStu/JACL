#!/bin/sh
# Build the Lulu cover wrap PDF for the print-on-demand upload.
#
# Source: cover.html (single-file HTML+CSS, lays out back cover,
# spine and front cover on a 14.864in x 10.25in landscape page sized
# to match Lulu's cover template for the Executive 7x10 trim at a
# 246-page spine width).
# Output: JACLGuide-cover.pdf, ready to upload to Lulu's cover
# uploader.
#
# If your interior PDF's page count changes, Lulu will regenerate
# the cover template with a different spine width and total document
# size. Update the @page rules and the spine-related coordinates in
# cover.html to match before rebuilding.
#
# Dependencies: weasyprint (brew/apt/pip).

set -e

weasyprint --version
weasyprint cover.html JACLGuide-cover.pdf
