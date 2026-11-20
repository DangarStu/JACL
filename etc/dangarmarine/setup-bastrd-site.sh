#!/bin/sh
# Puts the BASTRD app at https://bastrd.dangarmarine.com.au/ on the Dangar
# Marine VM. Run as root from the folder holding bastrd.conf and
# bastrd-ssl.conf (this one in the repo, or wherever they were copied):
#
#   sudo sh setup-bastrd-site.sh
#
# Needs the DNS A record for bastrd.dangarmarine.com.au pointing at the VM
# first. Safe to run again: it skips the certificate if it already exists.
set -e

HOST=bastrd.dangarmarine.com.au
VM_IP=134.199.154.142
HERE=$(cd "$(dirname "$0")" && pwd)
SITES=/etc/apache2/sites-available

# CERTBOT CAN'T ISSUE A CERTIFICATE UNTIL THE NAME RESOLVES TO THIS VM
if ! getent hosts "$HOST" | grep -q "$VM_IP"; then
    echo "$HOST doesn't resolve to $VM_IP yet."
    echo "Add the A record in DigitalOcean, wait a minute, then run this again."
    exit 1
fi

echo "=== Port 80 site"
cp "$HERE/bastrd.conf" "$SITES/"
a2ensite -q bastrd
apache2ctl configtest
systemctl reload apache2

echo "=== Certificate"
if [ -d "/etc/letsencrypt/live/$HOST" ]; then
    echo "Already have one for $HOST"
else
    # certonly: issue the certificate but leave the Apache config to us,
    # so certbot doesn't generate its own SSL copy of the port 80 site
    certbot certonly --apache --non-interactive -d "$HOST"
fi

echo "=== HTTPS site"
cp "$HERE/bastrd-ssl.conf" "$SITES/"
a2ensite -q bastrd-ssl
apache2ctl configtest
systemctl reload apache2

echo "=== Checking"
curl -s -o /dev/null -w "http  %{http_code} -> %{redirect_url}\n" "http://$HOST/"
curl -s "https://$HOST/" | grep -o '<title>[^<]*' | sed 's/<title>/https page title: /'
curl -s -o /dev/null -w "tab link %{http_code} -> %{redirect_url}\n" "https://$HOST/jacl/bastrd.fcgi?mode=4"
