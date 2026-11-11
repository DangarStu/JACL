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
