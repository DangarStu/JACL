# Google Sign-In setup for fcgijacl

Optional. Lets returning players resume their saved game by signing
in with Google. Anonymous play continues to work unchanged when
this is not configured.

## 1. Build dependencies

The `auth.c` module needs libcurl, jansson, and OpenSSL. On
Debian/Ubuntu (the `make apache` target):

```sh
sudo apt install libcurl4-openssl-dev libjansson-dev libssl-dev
```

On macOS:

```sh
brew install curl jansson openssl@3
```

If `./configure` can't find all three, fcgijacl is built against
`auth_stub.c` instead and Sign-In is silently disabled at runtime.
You'll see `Google Sign-In disabled (need libcurl, jansson, openssl)`
in the configure summary.

## 2. Create a Google OAuth Client ID

1. Go to <https://console.cloud.google.com/apis/credentials>.
2. Create or select a project.
3. **Create Credentials -> OAuth client ID -> Web application**.
4. Under **Authorised JavaScript origins** add the exact origin
   players will load the game from, e.g. `https://jacl.example.com`.
   You don't need a redirect URI -- Google Identity Services posts
   the ID token directly to JS, which fcgijacl then verifies.
5. Copy the resulting Client ID -- it looks like
   `1234567890-abcdef.apps.googleusercontent.com`.

## 3. Generate a session secret

This is the HMAC key fcgijacl uses to sign session cookies. Treat it
like a private key: anyone who knows it can mint cookies for any
Google account.

```sh
openssl rand -hex 32
```

## 4. Configure cgijacl.conf

Add (or uncomment) these lines in `/etc/cgijacl.conf`:

```
google_client_id  "1234567890-abcdef.apps.googleusercontent.com"
session_secret    "<the openssl-rand output from above>"
session_max_age   2592000
```

`session_max_age` is the cookie lifetime in seconds; 2592000 = 30 days.

## 5. Serve over HTTPS

Google Identity Services only runs on HTTPS origins (the sole
exception is `http://localhost` for local testing). The origin you
configured in step 2 must match your Apache vhost **exactly** -- same
scheme, same hostname, no port, no trailing slash. So if Apache is
serving `https://jacl.example.com`, the Google Console origin must be
`https://jacl.example.com` and nothing else.

`etc/jacl-apache.conf` ships with a commented-out `:443` vhost
template you can adapt. fcgijacl automatically adds the `Secure` flag
to the session cookie when Apache sets `HTTPS=on`, so once the vhost
is live there's nothing further to do on the cookie side.

## 6. Restart Apache / fcgijacl

```sh
sudo systemctl restart apache2
```

mod_fcgid will re-spawn fcgijacl workers and they'll pick up the new
config. The header should now show a Google button next to the Hint
link; clicking it runs the GIS flow and reloads the page with the
player signed in.

## What it actually does

* On a successful Google Sign-In the frontend POSTs the ID token to
  `?auth=google&credential=<jwt>`.
* fcgijacl verifies the token (RS256, against the live Google JWKS),
  checks `iss`/`aud`/`exp`, and extracts the `sub` claim.
* It returns a `Set-Cookie: jacl_session=<sub>.<expiry>.<hmac>`
  cookie (`HttpOnly; SameSite=Lax`).
* On every subsequent request, the cookie is verified and `user_id`
  is set to `google_<sub>`, which is what save files key off. So a
  player who signs in on a different device, or after a year, gets
  their game back.
* `?auth=logout` clears the cookie.

If `google_client_id` is empty or unset, none of this code path
runs, no auth-related JS is emitted, and the game behaves exactly
as before.

## Game-side hooks

These JACL constants are populated on every request:

* `google_client_id` -- the configured client ID, or empty string
  when auth is off. Use this to gate UI/behaviour:

  ```jacl
  ifstring google_client_id != ""
      write "Sign in to save your progress.^"
  endif
  ```

* `google_signed_in` -- 1 when the request has a valid session
  cookie, 0 otherwise.

* `google_sub` -- the Google subject claim for the signed-in user
  (empty when anonymous). Useful for per-player branching, but
  remember it's an opaque identifier, not an email address.
