/* auth.h --- Optional Google Sign-In for fcgijacl.
 *
 * Verifies Google ID tokens (RS256, JWKS at oauth2.googleapis.com)
 * and issues an HMAC-signed session cookie that fcgijacl reads to
 * resolve a stable user_id of the form "google_<sub>". When auth is
 * not configured, every entry point degrades to a no-op so the
 * existing anonymous flow is unaffected.
 */

#ifndef JACL_AUTH_H
#define JACL_AUTH_H

#include <stddef.h>

/* Maximum length of a Google subject claim. Google sub values are
 * 21-digit decimals today; we leave room for future growth. */
#define AUTH_SUB_MAX 64

/* Maximum length of the signed session cookie value. The cookie is
 * "<sub>.<expiry>.<hex_hmac>" so 64 + 1 + 20 + 1 + 64 + slack. */
#define AUTH_COOKIE_MAX 256

/* Configure the auth module. May be called multiple times (e.g. on
 * config reload). Both arguments are copied; pass NULL/empty to
 * disable. session_secret should be at least 32 bytes of entropy. */
void auth_configure(const char *google_client_id,
                    const char *session_secret,
                    long session_max_age_seconds);

/* Returns non-zero if auth_configure has been called with both a
 * client_id and a session_secret. Callers use this to decide whether
 * to render the Sign-In button or the auth callback path. */
int auth_is_enabled(void);

/* Accessor for the configured Google OAuth client_id. Returns an
 * empty string when auth is disabled, never NULL. */
const char *auth_google_client_id(void);

/* Verify a Google ID token (the credential string from GIS). On
 * success writes the sub claim to sub_out and returns 0. On any
 * failure (signature, claim, expiry, audience) writes a short reason
 * to err_out (may be NULL) and returns -1. */
int auth_verify_id_token(const char *id_token,
                         char *sub_out, size_t sub_size,
                         char *err_out, size_t err_size);

/* Build a session-cookie value binding the given sub for the
 * configured max age. Returns 0 on success. */
int auth_make_session_cookie(const char *sub,
                             char *cookie_out, size_t cookie_size);

/* Verify a session-cookie value previously built by
 * auth_make_session_cookie. On success writes the sub to sub_out and
 * returns 0. Returns -1 on bad signature or expired cookie. */
int auth_verify_session_cookie(const char *cookie,
                               char *sub_out, size_t sub_size);

/* Configured cookie max-age in seconds (default 2592000 = 30 days). */
long auth_session_max_age(void);

#endif /* JACL_AUTH_H */
