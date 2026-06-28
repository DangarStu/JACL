#!/usr/bin/env python3
"""Hands-off App Store submission via the App Store Connect API.

Run by .github/workflows/ios-appstore-api.yml. For the marketing version in
APP_VERSION it, with NO deliver/fastlane and NO manual App Store Connect steps:

  1. finds (or creates) the editable App Store version,
  2. sets "What's New" on every localization from ios/whatsnew.txt,
  3. attaches the TestFlight build (BUILD_NUMBER, or the latest build for this
     version), and
  4. submits for review (unless SUBMIT != "true").

The release note is version-controlled in ios/whatsnew.txt -- update that file per
release and nothing needs typing into ASC. This supersedes the deliver-based
ios-appstore.yml, whose download_metadata step produced no localizations so the
required whatsNew came up empty; here we PATCH whatsNew on the version's own
localizations directly. The submit logic mirrors the proven ios-submit.yml.

Env: ASC_KEY_ID, ASC_ISSUER_ID, ASC_KEY_P8_BASE64, BUNDLE_ID, APP_VERSION,
     BUILD_NUMBER (optional), SUBMIT ("true"/"false").
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
APP_VERSION = os.environ["APP_VERSION"]
BUILD_NUMBER = os.environ.get("BUILD_NUMBER", "").strip()
SUBMIT = os.environ.get("SUBMIT", "true").strip().lower() == "true"
PLATFORM = "IOS"
BASE = "https://api.appstoreconnect.apple.com"

# "What's New", version-controlled in ios/whatsnew.txt (two dirs up from here).
HERE = os.path.dirname(os.path.abspath(__file__))
NOTES_PATH = os.path.normpath(os.path.join(HERE, "..", "..", "ios", "whatsnew.txt"))
with open(NOTES_PATH, encoding="utf-8") as fh:
    WHATS_NEW = fh.read().strip()
if not WHATS_NEW:
    sys.exit(f"{NOTES_PATH} is empty -- nothing to set as What's New")
print(f"What's New ({APP_VERSION}):\n{WHATS_NEW}\n")

now = int(time.time())
TOKEN = jwt.encode(
    {"iss": ISSUER_ID, "iat": now, "exp": now + 20 * 60, "aud": "appstoreconnect-v1"},
    PRIVATE_KEY,
    algorithm="ES256",
    headers={"kid": KEY_ID, "typ": "JWT"},
)
HEADERS = {"Authorization": f"Bearer {TOKEN}", "Content-Type": "application/json"}


def show(label, r):
    print(f"\n=== {label} -> HTTP {r.status_code} ===")
    try:
        print(json.dumps(r.json(), indent=2))
    except Exception:
        print(r.text)
    return r


# 1. App id by bundle id.
r = show("GET app", requests.get(f"{BASE}/v1/apps", headers=HEADERS,
         params={"filter[bundleId]": BUNDLE_ID}))
apps = r.json().get("data", [])
if not apps:
    sys.exit(f"No app found for bundle id {BUNDLE_ID}")
app_id = apps[0]["id"]
print(f"app_id = {app_id}")

# 2. Editable App Store version for APP_VERSION, or create one.
EDITABLE = {"PREPARE_FOR_SUBMISSION", "DEVELOPER_REJECTED", "REJECTED",
            "METADATA_REJECTED", "INVALID_BINARY", "READY_FOR_REVIEW"}
r = show("GET appStoreVersions",
         requests.get(f"{BASE}/v1/apps/{app_id}/appStoreVersions", headers=HEADERS,
                      params={"filter[versionString]": APP_VERSION}))
versions = r.json().get("data", [])
version = None
for v in versions:
    state = v["attributes"].get("appStoreState")
    print(f"  id={v['id']} state={state}")
    if version is None and state in EDITABLE:
        version = v
if version is None and versions:
    version = versions[0]
if version is None:
    r = show("POST appStoreVersions (create)",
             requests.post(f"{BASE}/v1/appStoreVersions", headers=HEADERS,
                           data=json.dumps({"data": {
                               "type": "appStoreVersions",
                               "attributes": {"platform": PLATFORM,
                                              "versionString": APP_VERSION},
                               "relationships": {"app": {"data": {
                                   "type": "apps", "id": app_id}}}}})))
    if r.status_code not in (200, 201):
        sys.exit(f"Failed to create App Store version {APP_VERSION}")
    version = r.json()["data"]
version_id = version["id"]
print(f"version {APP_VERSION} id = {version_id}")

# 3. Set What's New on every localization.
r = show("GET appStoreVersionLocalizations",
         requests.get(
             f"{BASE}/v1/appStoreVersions/{version_id}/appStoreVersionLocalizations",
             headers=HEADERS, params={"limit": 50}))
locs = r.json().get("data", [])
if not locs:
    sys.exit("Version has no localizations to set What's New on")
for loc in locs:
    lid = loc["id"]
    locale = loc["attributes"].get("locale")
    rr = show(f"PATCH whatsNew [{locale}]",
              requests.patch(f"{BASE}/v1/appStoreVersionLocalizations/{lid}",
                             headers=HEADERS,
                             data=json.dumps({"data": {
                                 "type": "appStoreVersionLocalizations", "id": lid,
                                 "attributes": {"whatsNew": WHATS_NEW}}})))
    if rr.status_code not in (200, 201):
        sys.exit(f"Failed to set What's New for {locale}")

# 4. Find the build to attach (input, else the latest for this version).
if BUILD_NUMBER:
    r = show("GET builds (filtered)",
             requests.get(f"{BASE}/v1/builds", headers=HEADERS,
                          params={"filter[app]": app_id,
                                  "filter[preReleaseVersion.version]": APP_VERSION,
                                  "filter[version]": BUILD_NUMBER, "limit": 200}))
    builds = r.json().get("data", [])
    if not builds:
        r = show("GET builds (fallback list)",
                 requests.get(f"{BASE}/v1/builds", headers=HEADERS,
                              params={"filter[app]": app_id, "limit": 200}))
        builds = [b for b in r.json().get("data", [])
                  if b["attributes"].get("version") == BUILD_NUMBER]
else:
    r = show("GET builds (latest for version)",
             requests.get(f"{BASE}/v1/builds", headers=HEADERS,
                          params={"filter[app]": app_id,
                                  "filter[preReleaseVersion.version]": APP_VERSION,
                                  "sort": "-version", "limit": 200}))
    builds = r.json().get("data", [])
if not builds:
    sys.exit(f"No build found for {APP_VERSION} (build_number='{BUILD_NUMBER}')")
build = builds[0]
build_id = build["id"]
build_ver = build["attributes"].get("version")
processing = build["attributes"].get("processingState")
print(f"build {build_ver} id = {build_id} processingState = {processing}")
if processing != "VALID":
    print(f"WARNING: build {build_ver} processingState is {processing}, not VALID")

r = show("PATCH version build relationship",
         requests.patch(
             f"{BASE}/v1/appStoreVersions/{version_id}/relationships/build",
             headers=HEADERS,
             data=json.dumps({"data": {"type": "builds", "id": build_id}})))
if r.status_code != 204:
    sys.exit(f"Failed to attach build {build_ver} (HTTP {r.status_code})")

if not SUBMIT:
    print("\nSUBMIT=false -> build attached + What's New set; not submitting.")
    sys.exit(0)

# 5. Submit for review: reuse an open reviewSubmission or create one, attach the
#    version as an item, then PATCH submitted:true (mirrors ios-submit.yml).
r = show("GET reviewSubmissions",
         requests.get(f"{BASE}/v1/reviewSubmissions", headers=HEADERS,
                      params={"filter[app]": app_id, "filter[platform]": PLATFORM,
                              "filter[state]": "READY_FOR_REVIEW"}))
submissions = r.json().get("data", [])


def items_of(sid):
    rr = requests.get(f"{BASE}/v1/reviewSubmissions/{sid}/items", headers=HEADERS,
                      params={"include": "appStoreVersion", "limit": 50})
    j = rr.json()
    return j.get("data", []), j.get("included", [])


def has_version(sid):
    its, inc = items_of(sid)
    for it in its:
        av = (it.get("relationships", {}).get("appStoreVersion", {}).get("data") or {})
        if av.get("id") == version_id:
            return True
    return any(x.get("type") == "appStoreVersions" and x.get("id") == version_id
               for x in inc)


sub_id, present = None, False
for s in submissions:
    if has_version(s["id"]):
        sub_id, present = s["id"], True
        break
if sub_id is None and submissions:
    sub_id = submissions[0]["id"]
if sub_id is None:
    r = show("POST reviewSubmissions (create)",
             requests.post(f"{BASE}/v1/reviewSubmissions", headers=HEADERS,
                           data=json.dumps({"data": {
                               "type": "reviewSubmissions",
                               "attributes": {"platform": PLATFORM},
                               "relationships": {"app": {"data": {
                                   "type": "apps", "id": app_id}}}}})))
    if r.status_code not in (200, 201):
        sys.exit("Failed to create reviewSubmission")
    sub_id = r.json()["data"]["id"]

if not present:
    r = show("POST reviewSubmissionItems (attach version)",
             requests.post(f"{BASE}/v1/reviewSubmissionItems", headers=HEADERS,
                           data=json.dumps({"data": {
                               "type": "reviewSubmissionItems",
                               "relationships": {
                                   "reviewSubmission": {"data": {
                                       "type": "reviewSubmissions", "id": sub_id}},
                                   "appStoreVersion": {"data": {
                                       "type": "appStoreVersions",
                                       "id": version_id}}}}})))
    if r.status_code not in (200, 201):
        # If the version already belongs to another submission, switch to it.
        other = None
        try:
            for err in r.json().get("errors", []):
                if err.get("code") == "STATE_ERROR.ITEM_PART_OF_ANOTHER_SUBMISSION":
                    other = (err.get("detail", "")
                             .rsplit("reviewSubmission with id ", 1)[-1]
                             .strip().strip("."))
                for ae in (err.get("meta", {})
                           .get("associatedErrors", {}).values()):
                    for inner in ae:
                        if inner.get("code") == \
                                "STATE_ERROR.ITEM_PART_OF_ANOTHER_SUBMISSION":
                            other = (inner.get("detail", "")
                                     .rsplit("reviewSubmission with id ", 1)[-1]
                                     .strip().strip("."))
        except Exception:
            pass
        if other:
            print(f"version already in reviewSubmission {other}; switching to it")
            sub_id = other
        else:
            sys.exit("Failed to attach appStoreVersion to the review submission")

r = show("PATCH reviewSubmissions submitted=true",
         requests.patch(f"{BASE}/v1/reviewSubmissions/{sub_id}", headers=HEADERS,
                        data=json.dumps({"data": {
                            "type": "reviewSubmissions", "id": sub_id,
                            "attributes": {"submitted": True}}})))
if r.status_code not in (200, 201):
    sys.exit(f"Submit failed (HTTP {r.status_code})")
final_state = r.json().get("data", {}).get("attributes", {}).get("state")
print(f"\n[OK] Submitted {APP_VERSION} (build {build_ver}) for App Store review "
      f"-- reviewSubmission {sub_id} state={final_state}")
