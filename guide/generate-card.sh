#!/bin/sh
# Build the JACL Reference Card.
#
# Source: JACLReferenceCard.html (single-file HTML with inline CSS;
# laid out as a three-column A4 portrait fold-out card).
# Output: JACLReferenceCard.pdf.
#
# The legacy LibreOffice version (.odt) is no longer the source of
# truth and is kept only for historical reference.
#
# Dependencies: weasyprint (brew/apt/pip).

set -e

weasyprint --version
weasyprint JACLReferenceCard.html JACLReferenceCard.pdf
