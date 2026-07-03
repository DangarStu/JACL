#!/usr/bin/env python3
"""Enable (and report) a TestFlight public link for an external beta group.

Run by .github/workflows/ios-testflight-link.yml, via the App Store Connect API.
Idempotent: reuses an existing external beta group if there is one, else creates
"Public Testers". Enables the group's public link and prints the URL to hand out.

Optionally attaches build BUILD_NUMBER so external testers have something to
install. External testing requires the build to pass Beta App Review and the app's
Test Information to be filled in; if that isn't ready the attach is reported but
the public link is still returned -- the link starts working once an approved
build is in the group.

Env: ASC_KEY_ID, ASC_ISSUER_ID, ASC_KEY_P8_BASE64, BUNDLE_ID, BUILD_NUMBER (opt).
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
BUNDLE_ID = os.environ["BUNDLE_ID"]
BUILD_NUMBER = os.environ.get("BUILD_NUMBER", "").strip()
BASE = "https://api.appstoreconnect.apple.com"

now = int(time.time())
TOKEN = jwt.encode(
    {"iss": ISSUER_ID, "iat": now, "exp": now + 20 * 60, "aud": "appstoreconnect-v1"},
    PRIVATE_KEY, algorithm="ES256", headers={"kid": KEY_ID, "typ": "JWT"})
H = {"Authorization": f"Bearer {TOKEN}", "Content-Type": "application/json"}


def show(label, r):
    print(f"\n=== {label} -> HTTP {r.status_code} ===")
    try:
        print(json.dumps(r.json(), indent=2))
    except Exception:
        print(r.text)
    return r


# 1. App id.
r = show("GET app", requests.get(f"{BASE}/v1/apps", headers=H,
         params={"filter[bundleId]": BUNDLE_ID}))
apps = r.json().get("data", [])
if not apps:
    sys.exit(f"No app found for bundle id {BUNDLE_ID}")
app_id = apps[0]["id"]
print("app_id =", app_id)

# 2. Existing beta groups.
r = show("GET betaGroups", requests.get(f"{BASE}/v1/apps/{app_id}/betaGroups",
         headers=H, params={"limit": 200}))
groups = r.json().get("data", [])
external = [g for g in groups if not g["attributes"].get("isInternalGroup")]
print("\nExternal groups:")
for g in external:
    a = g["attributes"]
    print(f"  id={g['id']} name={a.get('name')!r} "
          f"publicLinkEnabled={a.get('publicLinkEnabled')} link={a.get('publicLink')}")

# Prefer an external group that already has the link on; else the first external
# group; else create one.
group = next((g for g in external if g["attributes"].get("publicLinkEnabled")), None)
if group is None and external:
    group = external[0]
if group is None:
    r = show("POST betaGroups (create 'Public Testers')",
             requests.post(f"{BASE}/v1/betaGroups", headers=H, data=json.dumps({
                 "data": {"type": "betaGroups",
                          "attributes": {"name": "Public Testers",
                                         "publicLinkEnabled": True},
                          "relationships": {"app": {"data": {
                              "type": "apps", "id": app_id}}}}})))
    if r.status_code not in (200, 201):
        sys.exit("Failed to create external beta group")
    group = r.json()["data"]
gid = group["id"]

# 3. Ensure the public link is enabled.
if not group["attributes"].get("publicLinkEnabled"):
    show("PATCH betaGroups publicLinkEnabled=true",
         requests.patch(f"{BASE}/v1/betaGroups/{gid}", headers=H, data=json.dumps({
             "data": {"type": "betaGroups", "id": gid,
                      "attributes": {"publicLinkEnabled": True}}})))

# 4. Optionally attach the build so testers have something to install.
if BUILD_NUMBER:
    rb = requests.get(f"{BASE}/v1/builds", headers=H,
                      params={"filter[app]": app_id,
                              "filter[version]": BUILD_NUMBER, "limit": 200})
    builds = rb.json().get("data", [])
    if builds:
        bid = builds[0]["id"]
        rr = show(f"POST betaGroups/{gid}/relationships/builds (attach {BUILD_NUMBER})",
                  requests.post(f"{BASE}/v1/betaGroups/{gid}/relationships/builds",
                                headers=H,
                                data=json.dumps({"data": [{"type": "builds", "id": bid}]})))
        if rr.status_code not in (200, 201, 204):
            print(f"\nNOTE: couldn't attach build {BUILD_NUMBER} to the external "
                  "group. External testing needs the build to pass Beta App Review "
                  "and the app's Test Information (contact + 'what to test') to be "
                  "filled in. The public link below is still valid; it goes live "
                  "for installs once an approved build is in the group.")
    else:
        print(f"\nbuild {BUILD_NUMBER} not found for this app; skipping attach")

# 5. Report the link.
r = show("GET betaGroup (final)", requests.get(f"{BASE}/v1/betaGroups/{gid}", headers=H))
a = r.json()["data"]["attributes"]
link = a.get("publicLink")
print("\n================ RESULT ================")
print("group name         :", a.get("name"))
print("publicLinkEnabled  :", a.get("publicLinkEnabled"))
print("PUBLIC LINK        :", link)
print("=======================================")
if not link:
    sys.exit("Link enabled but no URL returned yet -- re-run in a minute to read it.")
