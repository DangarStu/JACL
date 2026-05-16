#!/bin/sh
# Build the print-on-demand interior PDF (no cover image).
#
# Produces JACLGuide-interior.pdf at the same 7"x9.25" trim size as
# the standard print build, but uses title-interior.html in place of
# title.html so the front.png cover image is omitted -- print-on-
# demand services expect the interior and the wrap cover as separate
# uploads. The interior still includes the title text, "Seventh
# Edition" subtitle and the full colophon page.
#
# The cover wrap (front + spine + back) should be supplied separately
# to the POD service using their cover-template generator. The
# original front.png lives in this directory.
#
# Dependencies: perl (onepage.perl) and weasyprint (brew/apt/pip).

set -e

../bin/onepage.perl chapters-interior.list JACLGuide-interior-bundle.html

weasyprint --version
weasyprint --media-type print-book JACLGuide-interior-bundle.html JACLGuide-interior.pdf

# Clean up the intermediate bundle so it doesn't sit alongside the
# main JACLGuide.html (which is the screen build).
rm -f JACLGuide-interior-bundle.html
