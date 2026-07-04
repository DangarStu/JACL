#!/usr/bin/env python3
"""List and (optionally) revoke stale Apple **Development** certificates via the
App Store Connect API, to reclaim the account's certificate quota.

Why: the TestFlight lane signs with CODE_SIGN_STYLE=Automatic + -allowProvisioningUpdates,
so every fresh CI runner mints a new "Apple Development" certificate. They accumulate
until Apple returns "your account has reached the maximum number of certificates" and
the build fails. This revokes the old development certs (keeping the newest KEEP of
them), never touching DISTRIBUTION certs.

Dry-run by default: prints every certificate and what it *would* revoke. Set APPLY=true
to actually revoke.

Env: ASC_KEY_ID, ASC_ISSUER_ID, ASC_KEY_P8_BASE64, KEEP (opt, default 1), APPLY (opt).
"""
import os
import sys
import time
import json
import base64

import jwt
import requests

KEY_ID = os.environ["ASC_KEY_ID"]
ISSUER_ID = os.environ["ASC_ISSUER_ID"]
PRIVATE_KEY = base64.b64decode(os.environ["ASC_KEY_P8_BASE64"]).decode()
KEEP = int(os.environ.get("KEEP", "1").strip() or "1")
APPLY = os.environ.get("APPLY", "").strip().lower() in ("1", "true", "yes")
BASE = "https://api.appstoreconnect.apple.com"

# Development certificate types automatic signing creates (never DISTRIBUTION).
DEV_TYPES = {"DEVELOPMENT", "IOS_DEVELOPMENT", "MAC_APP_DEVELOPMENT"}

now = int(time.time())
TOKEN = jwt.encode(
    {"iss": ISSUER_ID, "iat": now, "exp": now + 20 * 60, "aud": "appstoreconnect-v1"},
    PRIVATE_KEY, algorithm="ES256", headers={"kid": KEY_ID, "typ": "JWT"})
H = {"Authorization": f"Bearer {TOKEN}", "Content-Type": "application/json"}

# Page through all certificates.
certs = []
url = f"{BASE}/v1/certificates?limit=200"
while url:
    r = requests.get(url, headers=H)
    if r.status_code != 200:
        sys.exit(f"GET certificates failed: HTTP {r.status_code}\n{r.text}")
    body = r.json()
    certs.extend(body.get("data", []))
    url = body.get("links", {}).get("next")

print(f"\n=== all certificates ({len(certs)}) ===")
for c in certs:
    a = c["attributes"]
    print(f"  {a.get('certificateType'):<22} {a.get('displayName')!r:<40} "
          f"exp={a.get('expirationDate')} id={c['id']}")

dev = [c for c in certs if c["attributes"].get("certificateType") in DEV_TYPES]
# Newest first, by expiration date (a fresh cert expires furthest out).
dev.sort(key=lambda c: c["attributes"].get("expirationDate") or "", reverse=True)
keep, revoke = dev[:KEEP], dev[KEEP:]

print(f"\n=== {len(dev)} development certs — keeping newest {len(keep)}, "
      f"{'REVOKING' if APPLY else 'would revoke'} {len(revoke)} ===")
for c in keep:
    print(f"  KEEP   {c['attributes'].get('displayName')!r} (exp {c['attributes'].get('expirationDate')})")
for c in revoke:
    print(f"  REVOKE {c['attributes'].get('displayName')!r} (exp {c['attributes'].get('expirationDate')})")

if not APPLY:
    print("\nDRY RUN — set APPLY=true to actually revoke. Distribution certs are never touched.")
    sys.exit(0)

failures = 0
for c in revoke:
    r = requests.delete(f"{BASE}/v1/certificates/{c['id']}", headers=H)
    ok = r.status_code in (200, 204)
    print(f"  {'revoked' if ok else 'FAILED ' + str(r.status_code)}: "
          f"{c['attributes'].get('displayName')!r}")
    if not ok:
        failures += 1
print(f"\nDone: revoked {len(revoke) - failures}/{len(revoke)} development certs.")
sys.exit(1 if failures else 0)
