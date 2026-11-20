# JACL on the Dangar Marine VM

How https://jacl.dangarmarine.com.au/ is served. The VM (134.199.154.142)
also hosts the Dangar Marine guide and Wryter, whose Apache configs live in
their own repos (`outboard/deploy/`, `wryter/etc/`).

| On the VM | In this repo |
|---|---|
| `conf-enabled/jacl.conf` (server-wide: `/jacl/` fcgi, `/www/`, `/include/`, `/images/`) | `etc/jacl-apache.conf` |
| `sites-enabled/000-default.conf` (port 80, redirects JACL to HTTPS) | `etc/dangarmarine/000-default.conf` |
| `sites-enabled/jacl-ssl.conf` (port 443) | `etc/dangarmarine/jacl-ssl.conf` |
| `/var/www/html/robots.txt` (keeps crawlers off `?command=` URLs) | not in git |
| `sites-enabled/offside.conf` (port 80 for offside.dangarmarine.com.au) | `etc/dangarmarine/offside.conf` |
| `sites-enabled/offside-ssl.conf` (OFFSIDE at the root of its own subdomain) | `etc/dangarmarine/offside-ssl.conf` |
| `sites-enabled/bastrd.conf` (port 80 for the old name, bastrd.dangarmarine.com.au) | `etc/dangarmarine/bastrd.conf` |
| `sites-enabled/bastrd-ssl.conf` (redirects the old name to OFFSIDE) | `etc/dangarmarine/bastrd-ssl.conf` |

The two files here are copies of the live ones, with a comment added at the
top, so they're a record rather than something to install unchanged. Copy
a changed one back with:

    scp etc/dangarmarine/jacl-ssl.conf stuart@jacl.dangarmarine.com.au:
    ssh -t stuart@jacl.dangarmarine.com.au 'sudo cp ~/jacl-ssl.conf /etc/apache2/sites-available/ && sudo apache2ctl configtest && sudo systemctl reload apache2'

Note that `000-default.conf` has no `ServerName`, so it's the catch-all for
port 80: any host no other vhost names lands on the JACL pages. Before
another site becomes the default, JACL needs its own port-80 vhost with
`ServerName jacl.dangarmarine.com.au`, and the server-wide aliases in
`jacl.conf` should move inside JACL's vhosts.

## OFFSIDE's subdomain

`offside.conf`, `offside-ssl.conf` and the `bastrd*.conf` pair are the
source for the live files, not just a record. `setup-offside-site.sh`
installs them, getting the certificate in between (the HTTPS site can't
load until it exists). It needs the DNS A record for `offside` pointing at
the VM first. To run it without a checkout on the VM, copy the files over
and run it there:

    scp etc/dangarmarine/offside*.conf etc/dangarmarine/bastrd-ssl.conf etc/dangarmarine/setup-offside-site.sh stuart@jacl.dangarmarine.com.au:
    ssh -t stuart@jacl.dangarmarine.com.au 'sudo sh ~/setup-offside-site.sh'

The app was called BASTRD and served from bastrd.dangarmarine.com.au until
it was renamed. That name keeps its certificate and port 80 site, and its
HTTPS site now redirects everything, query string included, to the same
place on offside.dangarmarine.com.au, so old invite links still work.
