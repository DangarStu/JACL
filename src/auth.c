/* auth.c --- Google Sign-In implementation for fcgijacl.
 *
 * Pure C, depends on libcurl + jansson + OpenSSL. Verifies Google ID
 * tokens against the live JWKS at oauth2.googleapis.com and signs
 * its own session cookie so the rest of the codebase can just read
 * the resolved sub.
 *
 * Threading: fcgijacl is single-process / single-thread per request
 * (mod_fcgid spawns multiple workers but each runs sequentially).
 * The JWKS cache is therefore plain globals -- if you ever switch to
 * a threaded server, wrap it in a mutex.
 */

#include "auth.h"

/* Pin the OpenSSL API to the 1.1 surface so RSA_new/RSA_set0_key/
 * RSA_free are not flagged as deprecated under OpenSSL 3. The 3.x
 * replacement (EVP_PKEY_fromdata + OSSL_PARAMs) is verbose and the
 * legacy path is still fully supported. */
#ifndef OPENSSL_API_COMPAT
#define OPENSSL_API_COMPAT 0x10100000L
#endif
#define OPENSSL_SUPPRESS_DEPRECATED 1

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <curl/curl.h>
#include <jansson.h>
#include <openssl/bn.h>
#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <openssl/rsa.h>
#include <openssl/sha.h>

#define GOOGLE_JWKS_URL    "https://www.googleapis.com/oauth2/v3/certs"
#define GOOGLE_ISSUER_1    "https://accounts.google.com"
#define GOOGLE_ISSUER_2    "accounts.google.com"
#define JWKS_CACHE_TTL     3600          /* refresh hourly anyway */
#define JWKS_MAX_KEYS      8
#define MAX_TOKEN_BYTES    8192

/* ---------- Configuration -------------------------------------- */

static char  cfg_client_id[256]       = "";
static char  cfg_session_secret[256]  = "";
static long  cfg_session_max_age      = 30L * 24L * 3600L; /* 30 days */

void
auth_configure(const char *google_client_id,
               const char *session_secret,
               long session_max_age_seconds)
{
    if (google_client_id != NULL) {
        strncpy(cfg_client_id, google_client_id, sizeof(cfg_client_id) - 1);
        cfg_client_id[sizeof(cfg_client_id) - 1] = 0;
    } else {
        cfg_client_id[0] = 0;
    }
    if (session_secret != NULL) {
        strncpy(cfg_session_secret, session_secret, sizeof(cfg_session_secret) - 1);
        cfg_session_secret[sizeof(cfg_session_secret) - 1] = 0;
    } else {
        cfg_session_secret[0] = 0;
    }
    if (session_max_age_seconds > 0) {
        cfg_session_max_age = session_max_age_seconds;
    }
}

int
auth_is_enabled(void)
{
    return cfg_client_id[0] != 0 && cfg_session_secret[0] != 0;
}

const char *
auth_google_client_id(void)
{
    return cfg_client_id;
}

long
auth_session_max_age(void)
{
    return cfg_session_max_age;
}

/* ---------- Base64url ------------------------------------------ */

/* RFC 7515 base64url decode -- '+' '/' '=' replaced by '-' '_' and
 * padding stripped. Writes raw bytes to out, returns length or -1. */
static int
b64url_decode(const char *in, size_t in_len,
              unsigned char *out, size_t out_size)
{
    static const int tbl[256] = {
        ['A']= 0,['B']= 1,['C']= 2,['D']= 3,['E']= 4,['F']= 5,['G']= 6,
        ['H']= 7,['I']= 8,['J']= 9,['K']=10,['L']=11,['M']=12,['N']=13,
        ['O']=14,['P']=15,['Q']=16,['R']=17,['S']=18,['T']=19,['U']=20,
        ['V']=21,['W']=22,['X']=23,['Y']=24,['Z']=25,
        ['a']=26,['b']=27,['c']=28,['d']=29,['e']=30,['f']=31,['g']=32,
        ['h']=33,['i']=34,['j']=35,['k']=36,['l']=37,['m']=38,['n']=39,
        ['o']=40,['p']=41,['q']=42,['r']=43,['s']=44,['t']=45,['u']=46,
        ['v']=47,['w']=48,['x']=49,['y']=50,['z']=51,
        ['0']=52,['1']=53,['2']=54,['3']=55,['4']=56,['5']=57,['6']=58,
        ['7']=59,['8']=60,['9']=61,
        ['-']=62,['_']=63,
    };
    /* Sentinel for "not in alphabet" (zero is 'A'); use 0xFF table */
    unsigned char alphabet[256];
    size_t i;
    int v[4];
    int outlen = 0;
    int padded;
    size_t len = in_len;

    for (i = 0; i < 256; i++) alphabet[i] = 0xFF;
    for (i = 0; i < 256; i++) {
        if (tbl[i] != 0 || i == 'A') alphabet[i] = (unsigned char) tbl[i];
    }

    /* Strip stray '=' padding if present. */
    while (len > 0 && in[len - 1] == '=') len--;

    for (i = 0; i + 3 < len; i += 4) {
        v[0] = alphabet[(unsigned char) in[i+0]];
        v[1] = alphabet[(unsigned char) in[i+1]];
        v[2] = alphabet[(unsigned char) in[i+2]];
        v[3] = alphabet[(unsigned char) in[i+3]];
        if (v[0] == 0xFF || v[1] == 0xFF || v[2] == 0xFF || v[3] == 0xFF) return -1;
        if ((size_t)(outlen + 3) > out_size) return -1;
        out[outlen++] = (unsigned char) ((v[0] << 2) | (v[1] >> 4));
        out[outlen++] = (unsigned char) ((v[1] << 4) | (v[2] >> 2));
        out[outlen++] = (unsigned char) ((v[2] << 6) | v[3]);
    }
    padded = (int) (len - i);
    if (padded == 2) {
        v[0] = alphabet[(unsigned char) in[i+0]];
        v[1] = alphabet[(unsigned char) in[i+1]];
        if (v[0] == 0xFF || v[1] == 0xFF) return -1;
        if ((size_t)(outlen + 1) > out_size) return -1;
        out[outlen++] = (unsigned char) ((v[0] << 2) | (v[1] >> 4));
    } else if (padded == 3) {
        v[0] = alphabet[(unsigned char) in[i+0]];
        v[1] = alphabet[(unsigned char) in[i+1]];
        v[2] = alphabet[(unsigned char) in[i+2]];
        if (v[0] == 0xFF || v[1] == 0xFF || v[2] == 0xFF) return -1;
        if ((size_t)(outlen + 2) > out_size) return -1;
        out[outlen++] = (unsigned char) ((v[0] << 2) | (v[1] >> 4));
        out[outlen++] = (unsigned char) ((v[1] << 4) | (v[2] >> 2));
    } else if (padded != 0) {
        return -1;
    }
    return outlen;
}

/* ---------- JWKS fetch + cache --------------------------------- */

struct jwk_entry {
    char         kid[128];
    EVP_PKEY    *pkey;
};

static struct jwk_entry jwks_cache[JWKS_MAX_KEYS];
static int    jwks_cache_count = 0;
static time_t jwks_cache_ts    = 0;

struct curl_buffer {
    char  *data;
    size_t len;
    size_t cap;
};

static size_t
curl_write_cb(void *ptr, size_t size, size_t nmemb, void *userdata)
{
    struct curl_buffer *buf = (struct curl_buffer *) userdata;
    size_t n = size * nmemb;
    if (buf->len + n + 1 > buf->cap) {
        size_t new_cap = buf->cap ? buf->cap * 2 : 4096;
        while (new_cap < buf->len + n + 1) new_cap *= 2;
        char *p = (char *) realloc(buf->data, new_cap);
        if (p == NULL) return 0;
        buf->data = p;
        buf->cap = new_cap;
    }
    memcpy(buf->data + buf->len, ptr, n);
    buf->len += n;
    buf->data[buf->len] = 0;
    return n;
}

/* Build an EVP_PKEY for an RS256 verifier from base64url(n) and
 * base64url(e). Returns NULL on failure. */
static EVP_PKEY *
rsa_pkey_from_jwk(const char *n_b64, const char *e_b64)
{
    unsigned char n_buf[1024];
    unsigned char e_buf[16];
    int n_len = b64url_decode(n_b64, strlen(n_b64), n_buf, sizeof(n_buf));
    int e_len = b64url_decode(e_b64, strlen(e_b64), e_buf, sizeof(e_buf));
    if (n_len <= 0 || e_len <= 0) return NULL;

    BIGNUM *n_bn = BN_bin2bn(n_buf, n_len, NULL);
    BIGNUM *e_bn = BN_bin2bn(e_buf, e_len, NULL);
    if (n_bn == NULL || e_bn == NULL) {
        if (n_bn) BN_free(n_bn);
        if (e_bn) BN_free(e_bn);
        return NULL;
    }

    /* OpenSSL >= 3 deprecated RSA_set0_key in favor of OSSL_PARAM/EVP
     * builders. Use the legacy path via low-level RSA for portability;
     * with -DOPENSSL_API_COMPAT=10100 it still compiles cleanly. */
    RSA *rsa = RSA_new();
    if (rsa == NULL) {
        BN_free(n_bn);
        BN_free(e_bn);
        return NULL;
    }
    if (RSA_set0_key(rsa, n_bn, e_bn, NULL) != 1) {
        BN_free(n_bn);
        BN_free(e_bn);
        RSA_free(rsa);
        return NULL;
    }
    /* RSA_set0_key takes ownership of the BIGNUMs on success. */

    EVP_PKEY *pkey = EVP_PKEY_new();
    if (pkey == NULL || EVP_PKEY_assign_RSA(pkey, rsa) != 1) {
        if (pkey) EVP_PKEY_free(pkey);
        else RSA_free(rsa);
        return NULL;
    }
    /* EVP_PKEY_assign_RSA takes ownership of rsa on success. */
    return pkey;
}

static void
jwks_cache_clear(void)
{
    int i;
    for (i = 0; i < jwks_cache_count; i++) {
        if (jwks_cache[i].pkey) EVP_PKEY_free(jwks_cache[i].pkey);
        jwks_cache[i].pkey = NULL;
        jwks_cache[i].kid[0] = 0;
    }
    jwks_cache_count = 0;
}

/* Fetch Google's JWKS and rebuild the cache. Returns 0 on success. */
static int
jwks_fetch(void)
{
    CURL *curl = curl_easy_init();
    struct curl_buffer buf = { NULL, 0, 0 };
    int rc = -1;
    if (curl == NULL) return -1;

    curl_easy_setopt(curl, CURLOPT_URL, GOOGLE_JWKS_URL);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &buf);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "fcgijacl/1.0");

    if (curl_easy_perform(curl) != CURLE_OK) goto out;

    json_error_t jerr;
    json_t *root = json_loads(buf.data, 0, &jerr);
    if (root == NULL) goto out;
    json_t *keys = json_object_get(root, "keys");
    if (!json_is_array(keys)) {
        json_decref(root);
        goto out;
    }

    jwks_cache_clear();
    size_t i;
    size_t n = json_array_size(keys);
    if (n > JWKS_MAX_KEYS) n = JWKS_MAX_KEYS;
    for (i = 0; i < n; i++) {
        json_t *k = json_array_get(keys, i);
        const char *kty = json_string_value(json_object_get(k, "kty"));
        const char *alg = json_string_value(json_object_get(k, "alg"));
        const char *kid = json_string_value(json_object_get(k, "kid"));
        const char *n_s = json_string_value(json_object_get(k, "n"));
        const char *e_s = json_string_value(json_object_get(k, "e"));
        if (kty == NULL || strcmp(kty, "RSA") != 0) continue;
        if (alg != NULL && strcmp(alg, "RS256") != 0) continue;
        if (kid == NULL || n_s == NULL || e_s == NULL) continue;

        EVP_PKEY *pkey = rsa_pkey_from_jwk(n_s, e_s);
        if (pkey == NULL) continue;

        strncpy(jwks_cache[jwks_cache_count].kid, kid,
                sizeof(jwks_cache[0].kid) - 1);
        jwks_cache[jwks_cache_count].kid[sizeof(jwks_cache[0].kid) - 1] = 0;
        jwks_cache[jwks_cache_count].pkey = pkey;
        jwks_cache_count++;
    }
    json_decref(root);
    jwks_cache_ts = time(NULL);
    rc = jwks_cache_count > 0 ? 0 : -1;

out:
    free(buf.data);
    curl_easy_cleanup(curl);
    return rc;
}

static EVP_PKEY *
jwks_lookup(const char *kid)
{
    int i;
    for (i = 0; i < jwks_cache_count; i++) {
        if (strcmp(jwks_cache[i].kid, kid) == 0) return jwks_cache[i].pkey;
    }
    return NULL;
}

/* ---------- ID token verify ------------------------------------ */

/* Split "header.payload.signature" into three views into a mutable
 * copy of the input. Returns 0 on success. */
static int
jwt_split(char *token, char **h, char **p, char **s)
{
    char *dot1 = strchr(token, '.');
    if (dot1 == NULL) return -1;
    char *dot2 = strchr(dot1 + 1, '.');
    if (dot2 == NULL) return -1;
    *dot1 = 0;
    *dot2 = 0;
    *h = token;
    *p = dot1 + 1;
    *s = dot2 + 1;
    return 0;
}

static int
verify_signature(EVP_PKEY *pkey,
                 const char *signing_input, size_t signing_input_len,
                 const unsigned char *sig, size_t sig_len)
{
    EVP_MD_CTX *ctx = EVP_MD_CTX_new();
    int rc = -1;
    if (ctx == NULL) return -1;
    if (EVP_DigestVerifyInit(ctx, NULL, EVP_sha256(), NULL, pkey) != 1) goto out;
    if (EVP_DigestVerifyUpdate(ctx, signing_input, signing_input_len) != 1) goto out;
    if (EVP_DigestVerifyFinal(ctx, sig, sig_len) == 1) rc = 0;
out:
    EVP_MD_CTX_free(ctx);
    return rc;
}

int
auth_verify_id_token(const char *id_token,
                     char *sub_out, size_t sub_size,
                     char *err_out, size_t err_size)
{
    char buf[MAX_TOKEN_BYTES];
    char *h, *p, *s;
    int rc = -1;

    if (!auth_is_enabled()) {
        if (err_out) snprintf(err_out, err_size, "auth disabled");
        return -1;
    }
    if (id_token == NULL || strlen(id_token) >= sizeof(buf)) {
        if (err_out) snprintf(err_out, err_size, "token too long");
        return -1;
    }
    strcpy(buf, id_token);

    if (jwt_split(buf, &h, &p, &s) != 0) {
        if (err_out) snprintf(err_out, err_size, "malformed JWT");
        return -1;
    }

    /* The signed input is "header.payload" in the *original* token. */
    char signing_input[MAX_TOKEN_BYTES];
    snprintf(signing_input, sizeof(signing_input), "%s.%s", h, p);
    size_t signing_input_len = strlen(signing_input);

    /* Decode header to find kid + alg. */
    unsigned char header_buf[1024];
    int header_len = b64url_decode(h, strlen(h), header_buf, sizeof(header_buf) - 1);
    if (header_len <= 0) {
        if (err_out) snprintf(err_out, err_size, "bad header b64");
        return -1;
    }
    header_buf[header_len] = 0;

    json_error_t jerr;
    json_t *header = json_loads((const char *) header_buf, 0, &jerr);
    if (header == NULL) {
        if (err_out) snprintf(err_out, err_size, "bad header json");
        return -1;
    }
    const char *alg = json_string_value(json_object_get(header, "alg"));
    const char *kid = json_string_value(json_object_get(header, "kid"));
    if (alg == NULL || strcmp(alg, "RS256") != 0 || kid == NULL) {
        json_decref(header);
        if (err_out) snprintf(err_out, err_size, "alg/kid missing");
        return -1;
    }
    char kid_local[128];
    strncpy(kid_local, kid, sizeof(kid_local) - 1);
    kid_local[sizeof(kid_local) - 1] = 0;
    json_decref(header);

    /* Decode signature. */
    unsigned char sig_buf[512];
    int sig_len = b64url_decode(s, strlen(s), sig_buf, sizeof(sig_buf));
    if (sig_len <= 0) {
        if (err_out) snprintf(err_out, err_size, "bad sig b64");
        return -1;
    }

    /* Find the signing key, refreshing JWKS once if it's a miss or
     * stale. */
    EVP_PKEY *pkey = jwks_lookup(kid_local);
    time_t now = time(NULL);
    if (pkey == NULL || (now - jwks_cache_ts) > JWKS_CACHE_TTL) {
        jwks_fetch();
        pkey = jwks_lookup(kid_local);
    }
    if (pkey == NULL) {
        if (err_out) snprintf(err_out, err_size, "kid not in JWKS");
        return -1;
    }

    if (verify_signature(pkey, signing_input, signing_input_len,
                         sig_buf, sig_len) != 0) {
        if (err_out) snprintf(err_out, err_size, "bad signature");
        return -1;
    }

    /* Decode payload, validate iss / aud / exp / sub. */
    unsigned char payload_buf[4096];
    int payload_len = b64url_decode(p, strlen(p), payload_buf,
                                    sizeof(payload_buf) - 1);
    if (payload_len <= 0) {
        if (err_out) snprintf(err_out, err_size, "bad payload b64");
        return -1;
    }
    payload_buf[payload_len] = 0;

    json_t *payload = json_loads((const char *) payload_buf, 0, &jerr);
    if (payload == NULL) {
        if (err_out) snprintf(err_out, err_size, "bad payload json");
        return -1;
    }

    const char *iss = json_string_value(json_object_get(payload, "iss"));
    const char *aud = json_string_value(json_object_get(payload, "aud"));
    const char *sub = json_string_value(json_object_get(payload, "sub"));
    json_int_t exp = json_integer_value(json_object_get(payload, "exp"));
    json_int_t iat = json_integer_value(json_object_get(payload, "iat"));

    if (iss == NULL ||
        (strcmp(iss, GOOGLE_ISSUER_1) != 0 && strcmp(iss, GOOGLE_ISSUER_2) != 0)) {
        if (err_out) snprintf(err_out, err_size, "bad issuer");
        json_decref(payload);
        return -1;
    }
    if (aud == NULL || strcmp(aud, cfg_client_id) != 0) {
        if (err_out) snprintf(err_out, err_size, "audience mismatch");
        json_decref(payload);
        return -1;
    }
    if (exp == 0 || (json_int_t) now >= exp) {
        if (err_out) snprintf(err_out, err_size, "token expired");
        json_decref(payload);
        return -1;
    }
    /* 5 minutes of clock skew tolerance for iat. */
    if (iat != 0 && iat > (json_int_t)(now + 300)) {
        if (err_out) snprintf(err_out, err_size, "token from future");
        json_decref(payload);
        return -1;
    }
    if (sub == NULL || strlen(sub) == 0 || strlen(sub) >= sub_size) {
        if (err_out) snprintf(err_out, err_size, "missing sub");
        json_decref(payload);
        return -1;
    }
    strcpy(sub_out, sub);
    rc = 0;
    json_decref(payload);
    return rc;
}

/* ---------- Session cookie sign / verify ----------------------- */

static void
hex_encode(const unsigned char *bytes, size_t len, char *out)
{
    static const char *hex = "0123456789abcdef";
    size_t i;
    for (i = 0; i < len; i++) {
        out[i * 2] = hex[(bytes[i] >> 4) & 0xF];
        out[i * 2 + 1] = hex[bytes[i] & 0xF];
    }
    out[len * 2] = 0;
}

static int
hex_decode(const char *in, unsigned char *out, size_t out_size)
{
    size_t len = strlen(in);
    size_t i;
    if (len % 2 != 0 || len / 2 > out_size) return -1;
    for (i = 0; i < len; i += 2) {
        unsigned int hi, lo;
        char c1 = in[i], c2 = in[i + 1];
        if (c1 >= '0' && c1 <= '9') hi = c1 - '0';
        else if (c1 >= 'a' && c1 <= 'f') hi = c1 - 'a' + 10;
        else if (c1 >= 'A' && c1 <= 'F') hi = c1 - 'A' + 10;
        else return -1;
        if (c2 >= '0' && c2 <= '9') lo = c2 - '0';
        else if (c2 >= 'a' && c2 <= 'f') lo = c2 - 'a' + 10;
        else if (c2 >= 'A' && c2 <= 'F') lo = c2 - 'A' + 10;
        else return -1;
        out[i / 2] = (unsigned char) ((hi << 4) | lo);
    }
    return (int) (len / 2);
}

static void
hmac_sha256(const char *secret, const char *data, unsigned char *out)
{
    unsigned int outlen = 0;
    HMAC(EVP_sha256(),
         (const unsigned char *) secret, (int) strlen(secret),
         (const unsigned char *) data, strlen(data),
         out, &outlen);
}

int
auth_make_session_cookie(const char *sub,
                         char *cookie_out, size_t cookie_size)
{
    if (!auth_is_enabled() || sub == NULL) return -1;
    long expiry = (long) time(NULL) + cfg_session_max_age;
    char body[AUTH_SUB_MAX + 32];
    int n = snprintf(body, sizeof(body), "%s.%ld", sub, expiry);
    if (n < 0 || (size_t) n >= sizeof(body)) return -1;
    unsigned char mac[32];
    char mac_hex[65];
    hmac_sha256(cfg_session_secret, body, mac);
    hex_encode(mac, sizeof(mac), mac_hex);
    n = snprintf(cookie_out, cookie_size, "%s.%s", body, mac_hex);
    if (n < 0 || (size_t) n >= cookie_size) return -1;
    return 0;
}

int
auth_verify_session_cookie(const char *cookie,
                           char *sub_out, size_t sub_size)
{
    if (!auth_is_enabled() || cookie == NULL) return -1;
    char buf[AUTH_COOKIE_MAX];
    if (strlen(cookie) >= sizeof(buf)) return -1;
    strcpy(buf, cookie);

    /* Cookie is "<sub>.<expiry>.<hex_hmac>". Split from the right
     * since sub may itself contain digits but won't contain '.'. */
    char *last = strrchr(buf, '.');
    if (last == NULL) return -1;
    *last = 0;
    const char *mac_hex = last + 1;
    /* "<sub>.<expiry>" remains; we don't need to split it further to
     * verify the MAC, just to read expiry. */
    char *body = buf;
    char *mid = strrchr(body, '.');
    if (mid == NULL) return -1;
    char *expiry_str = mid + 1;
    long expiry = strtol(expiry_str, NULL, 10);
    if (expiry <= (long) time(NULL)) return -1;

    unsigned char expected[32];
    unsigned char given[32];
    hmac_sha256(cfg_session_secret, body, expected);
    if (hex_decode(mac_hex, given, sizeof(given)) != 32) return -1;

    /* Constant-time compare. */
    unsigned char diff = 0;
    int i;
    for (i = 0; i < 32; i++) diff |= expected[i] ^ given[i];
    if (diff != 0) return -1;

    /* Now the sub claim is everything before the last '.' in body. */
    *mid = 0;
    if (strlen(body) >= sub_size) return -1;
    strcpy(sub_out, body);
    return 0;
}
