#!/bin/sh
# Build the JACL Author's Guide.
#
# - JACLGuide.html (web version) is the chapters concatenated together via
#   onepage.perl's chapters.list. This file is also the input to the PDF
#   build, so title.html ends up on page 1 the same way it does on the web.
# - JACLGuide.pdf is rendered by WeasyPrint, which honours real CSS
#   (including @page rules in styles.css for margins, headers, and page
#   numbers). The legacy htmldoc pipeline (which ignored most CSS) was
#   retired; see guide.book / guide.list in git history if you need to
#   reconstruct it.
#
# Dependencies: perl (for onepage.perl) and weasyprint (brew/apt/pip).

set -e

../bin/onepage.perl chapters.list JACLGuide.html

weasyprint --version
weasyprint JACLGuide.html JACLGuide.pdf
