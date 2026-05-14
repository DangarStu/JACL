#!/bin/sh
# Build the print-on-demand edition of the JACL Author's Guide.
#
# Produces JACLGuide-print.pdf at a 7"x9.25" technical-book trim size
# with asymmetric (binding-side) margins, smaller body and code fonts,
# and chapters opening on right-hand pages. Suitable as-is for upload
# to Amazon KDP and most other print-on-demand services.
#
# Implementation: the print-edition overrides live in the
# `@media print-book { ... }` block at the bottom of styles.css.
# Passing `--media-type print-book` to weasyprint activates that
# block; the default screen-PDF build (generate.sh) uses the default
# media type "print" and ignores it.
#
# Dependencies: perl (onepage.perl) and weasyprint (brew/apt/pip).

set -e

../bin/onepage.perl chapters.list JACLGuide.html

weasyprint --version
weasyprint --media-type print-book JACLGuide.html JACLGuide-print.pdf
