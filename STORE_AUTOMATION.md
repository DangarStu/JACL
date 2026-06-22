# Store automation (TestFlight + Google Play)

GitHub Actions + fastlane pipelines that build a signed release and upload it to a
beta track — iOS → **TestFlight**, Android → **Play internal**.

**This is identical to the Wryter app's setup** (`~/git/wryter/STORE_AUTOMATION.md`)
— same files, secret names, workflow shape, and tag scheme — so you can work on
both apps without relearning anything. Only the app-specific values differ
(bundle id, scheme, keystore filename).

**Status: scaffolded.** Secrets are already set in the repo. Both workflows are
**tag-triggered** (and manually dispatchable). Real uploads also need the store
**app records** to exist (App Store Connect already has JACL; Play already has
JACL too).

| Workflow | Builds | Uploads to | Lane |
|---|---|---|---|
| `.github/workflows/ios-release.yml` | `.ipa` (xcodegen → archive) | TestFlight | `ios/fastlane` `beta` |
| `.github/workflows/android-release.yml` | signed `.aab` | Play **internal** track | `android/fastlane` `internal` |

Build number = **`1000 + the GitHub run number`** (unique, always increasing, and
above JACL's earlier manual store builds). Marketing version stays whatever's in
`ios/project.yml` / `android/app/build.gradle.kts`.

---

## Running it
Two tag namespaces (separate from any other repo's tags):

| Tag | Triggers |
|---|---|
| `ios-v*` (e.g. `ios-v1.3`) | iOS → TestFlight |
| `android-v*` (e.g. `android-v1.3`) | Android → Play internal |

- **On a tag**: `git tag ios-v1.3 && git push origin ios-v1.3` (or `android-v1.3`).
- **Manually**: Actions tab → pick the workflow → **Run workflow**.
- `gh` must be active as `DangarStu` (`gh auth switch --user DangarStu`).

After an upload, note it in `RELEASE_STATUS.md` / `android/release-tracker.csv`.

---

## Secrets (already set in this repo)
Account-level Apple credentials are **shared with Wryter** (one API key + the team
distribution certificate cover every app on the account). The Android keystore is
JACL's own (`jacl-release.jks`), and the Play service account is JACL-specific
(`jacl-play-ci@dangarmarine.iam.gserviceaccount.com`).

**iOS**
| Secret | What |
|---|---|
| `ASC_KEY_ID` / `ASC_ISSUER_ID` / `ASC_KEY_P8_BASE64` | App Store Connect API key (shared) |
| `IOS_DIST_CERT_P12_BASE64` / `IOS_DIST_CERT_PASSWORD` | Apple Distribution cert (.p12) |

**Android**
| Secret | What |
|---|---|
| `ANDROID_KEYSTORE_BASE64` | upload keystore `jacl-release.jks`, base64 |
| `ANDROID_KEYSTORE_PASSWORD` / `ANDROID_KEY_ALIAS` / `ANDROID_KEY_PASSWORD` | keystore creds |
| `PLAY_SERVICE_ACCOUNT_JSON` | Play service-account JSON |

To rotate or re-create any of these, follow Wryter's `STORE_AUTOMATION.md` (the
one-time Apple/Google setup is documented there in full).

---

## One remaining manual step (Google Play)
Grant the JACL Play service account permission to upload:
1. Play Console → **Users and permissions** → **Invite new users**.
2. Email: **`jacl-play-ci@dangarmarine.iam.gserviceaccount.com`**.
3. Under **Release**, tick **"Release apps to testing tracks"** (+ "Manage testing
   tracks…", and "View app information"). Scope it to the **JACL** app.

## Notes
- First real runs of iOS signing often need a small tweak (export method/profile).
- The two distribution certs in the account: `664U3KB5TA` (Wryter) and `232DHH2434`
  (JACL). Both are valid team Apple Distribution certs.
