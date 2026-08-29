# JACL — Google Play Submission

Companion to `ios/APP_STORE_SUBMISSION.md`. The app is built for Android
(`au.com.dangarmarine.jacl`; see `RELEASE_STATUS.md` for the current version/build and
target API — the target API floor moves every year, see §9).
Most listing copy and the privacy URL are reused from the iOS submission.

Status legend:  ✅ ready   ✏️ you must supply/confirm   ⏳ gated on account/test

---

## 0. Account
- **Google Play Console** registration: **US$25 one-time** (no annual renewal).
- **Personal** account is simplest (Organization needs a business + D-U-N-S).
- Complete **identity verification** (name, address, phone, ID).
- ⚠️ **Personal accounts must run a closed test with ≥20 testers for 14
  continuous days before production access** (see §6). Start this ASAP.

---

## 1. App details
| Field | Value |
|---|---|
| Package name | `au.com.dangarmarine.jacl` |
| versionName / versionCode | `1.0` / `1` |
| Min / Target SDK | 29 / **35** (Android 15 — Play requirement) |
| Format | **AAB** (App Bundle), signed; Play App Signing enabled |
| Price | Free |

---

## 2. Build the signed AAB
Needs your upload keystore (see `android/RELEASE.md`; keep it out of git):

```bash
cd android
./gradlew :app:bundleRelease
# output: app/build/outputs/bundle/release/app-release.aab
```

Once `android/keystore.properties` exists on this machine, I can run this for
you. Upload `app-release.aab` to the Play Console (Internal/Closed track first).

---

## 3. Store listing  (reuses iOS copy)

### App name
`JACL — Text Adventures`

### Short description  (max 80)
`Classic text adventures: type what you want to do, and the story responds.`

### Full description  (max 4000)
Reuse the iOS description verbatim from `ios/APP_STORE_SUBMISSION.md` §2
(the FEATURES / WHO IT'S FOR copy carries over unchanged).

### Graphics
| Asset | Spec | Status |
|---|---|---|
| App icon | 512×512 PNG | ✅ `PlayStore/graphics/icon-512.png` |
| Feature graphic | 1024×500 PNG | ✅ `PlayStore/graphics/feature-graphic.png` (source `.svg` alongside) |
| Phone screenshots | 1080×2160 (2:1 — Play caps phone at 2:1) | ✅ `PlayStore/screenshots-phone/` (bookshelf, map, settings) |
| Tablet screenshots | 2560×1600 | ✅ `PlayStore/screenshots/` (bookshelf, map, settings) |

### URLs
- Privacy Policy: `https://jacl.dangarmarine.com.au/privacy.html` ✅ live
- Website / contact: `https://jacl.dangarmarine.com.au/`

---

## 4. Data safety form  ✏️
- **Does your app collect or share any user data?** → **No.**
- No data types, no sharing, no collection. (Matches the published privacy
  policy: everything is on-device, no accounts, no network.)

---

## 5. Content rating (IARC questionnaire)  ✏️
Answer honestly — the only mature content is **Bloody Guns** (a WWII anti-
aircraft-gunner text adventure):
- Category: **Game**.
- **Violence:** references to war / shooting (text only, no graphics) → mild /
  infrequent. Not realistic gore.
- **Sexuality / nudity:** none to mild suggestive themes (depending on game).
- **Language:** occasional mild ("bloody").
- **Controlled substances, gambling, user-generated content, data sharing:** none.

Likely outcome: a **Teen / PEGI 12–16** style rating — the Android analogue of
your iOS 17+. Accept whatever the questionnaire computes.

---

## 6. Closed testing (the gating step)  ⏳
Personal accounts must satisfy this **before** production:
1. Create a **Closed testing** track → upload the AAB.
2. Add **≥20 testers** via an email list or Google Group.
3. Keep them **opted in for 14 continuous days**.
4. Then **apply for production access**.

**Who can test:** the interactive-fiction community (intfiction.org forum),
your iPad beta users, friends/family with Android devices, and the JACL website
audience. Testers only need to accept the opt-in link and install once — they
don't have to play daily, just stay opted in for the 14 days.

### 6a. Recruiting via a public Google Group  (the way Wryter does it)
For open, self-serve recruitment, use a **dedicated Google Group as the dynamic
tester list** — *not* the `jacl-discuss` archive (a different-purpose group; leave
it as its historical record). Mirrors Wryter's `wryter-testers` setup.

1. **Google Groups** (`groups.google.com`, signed in as the tester account):
   create **`jacl-testers@googlegroups.com`**. Set *Who can join* → **Anyone can
   join** (max volume — "the more testers the better early on"); *Who can post* →
   members, with **new-member first-post moderation** on (open join invites spam).
   Add yourself as an actual **member** (not a pending invite). The group doubles
   as the tester **feedback/discussion forum**.
2. **Play Console → JACL → Testing → Closed testing → (track) → Testers**: attach
   `jacl-testers@googlegroups.com` → **Save changes**. (Uploading a build to a
   track does *not* attach testers — this step is separate.)
3. Publish **two** links on the JACL site as a "Become a tester" page:
   - group join link (become a tester), then
   - opt-in URL `https://play.google.com/apps/testing/au.com.dangarmarine.jacl`
     (installs the app — only works once you're on the attached list).

**Gotchas (all hit during the Wryter setup):**
- A Google Group's **email address is permanent** — you can't rename it, only
  recreate. The *display name* is editable.
- A **brand-new group takes minutes-to-hours to propagate** before Play's tester
  check sees it; until then the opt-in URL returns **"App not available"** even
  for a confirmed member. To unblock one person instantly, add their email as a
  **direct email list** on the track (no propagation delay); keep the group as
  the long-term roster.
- `.../store/apps/details?id=au.com.dangarmarine.jacl` (the public store listing)
  **404s until a production/open-testing release** — don't put it on the site
  during closed testing; use the group-join + opt-in links instead.
- The Google Group **cannot** be reused for **iOS/TestFlight** — Apple has no
  Google integration. iOS uses a TestFlight external group + public link (see the
  `ios-testflight-link` workflow). The group *can* be the shared feedback forum
  for both platforms.

---

## 7. Production
- Promote the closed-test build to **Production** → submit.
- Google review is usually faster than Apple (hours to ~2 days).

---

## 8. Checklist
- [ ] ⏳ Play account registered ($25) + identity verified
- [ ] Create upload keystore (`keystore.properties`) → build signed AAB (§2)
- [ ] Create app in Play Console (package `au.com.dangarmarine.jacl`)
- [ ] Store listing: name, short + full description, graphics (§3)
- [ ] Phone screenshots captured (§3)
- [ ] Data safety = no data collected (§4)
- [ ] Content rating questionnaire (§5)
- [ ] Closed test: 20 testers × 14 days (§6)
- [ ] Promote to Production + submit (§7)
- [ ] **Annually before 31 Aug:** target-API bump landed, uploaded, **published**, and
      verified on every affected track (§9)

---

## 9. The annual target-API deadline (and the two traps that fake "done")

Google Play requires every app to target **within ~1 year of the latest Android**,
enforced by a hard **31 August** deadline each year: 2025 = API 35, **2026 = API 36**
(Android 16), 2027 = API 37. Miss it and you simply **cannot ship updates** — the app
already installed keeps working, but every upload is rejected.

### The code fix (≈30 min, both apps)
Raising `compileSdk`/`targetSdk` drags the toolchain with it — the old AGP literally
cannot compile against the new SDK. For **API 36** the floor was AGP **≥ 8.9.1** and
Gradle wrapper **≥ 8.11.1**. Three files per repo:

| File | Change |
|---|---|
| `android/app/build.gradle.kts` | `compileSdk` + `targetSdk` |
| `android/build.gradle.kts` | `com.android.application` plugin version (AGP) |
| `android/gradle/wrapper/gradle-wrapper.properties` | `distributionUrl` (Gradle) |

JACL has **native code** (NDK + CMake, ABIs arm64-v8a + x86_64). The compile/target
bump leaves the native side alone, but build both ABIs before believing it.

Verify locally — don't trust the diff:
```sh
./gradlew :app:assembleDebug :app:bundleRelease
$ANDROID_HOME/build-tools/<ver>/aapt2 dump badging <apk> | grep targetSdkVersion
```

### Then the part that actually counts
**Compiling against the new SDK satisfies nothing.** A build *targeting* the new level
must be **uploaded to Play and actually published on a track that serves users**. On
2026-08-30 — one day before the cutoff — both apps were believed done since 22 July.
Both were still non-compliant, for two different reasons. Neither was visible from the
repo, CI, or `RELEASE_STATUS.md`; all three said "shipped".

**Trap 1 — with managed publishing ON, a green CI upload is not a release.**
JACL's API-36 bundle 1011 uploaded fine on 21 Jul, then sat **five weeks** in the
managed-publishing queue as two unpublished changes ("1.3 — Start full rollout" +
"Track status — Resume track"). The closed track went on serving **1008, target SDK 35**
— precisely the bundle Play was complaining about. Fix: **Publishing overview →
Publish changes**. JACL has managed publishing **on**; Wryter has it **off** and
self-publishes. Check which before assuming an upload landed.

**Trap 2 — a stale *internal*-track build flags an otherwise-compliant app.**
Wryter's closed Alpha was already on API 36 (1017) yet the warning stayed live. The
culprit was bundle **1007 (v0.2, target SDK 35)** still active on the **internal** track
from two months earlier. Internal testing is exempt from *having to comply*, but a live
release there is still **reviewed and flagged**. The check looks at **every track with
an active release**, so sweep them all — internal included.

### Diagnose from Play, never from the repo
**Policy status → the target-API issue → "View app bundles"** names the offending
**version code and its track**. That single screen identifies the real culprit in about
a minute; it is the first thing to open, not the last. The issue page's generic advice
("publish a new version to production") is boilerplate — for a test-track-only app the
listed bundle is what must be superseded, and neither app needed a production release.

### Runbook
1. Open **Policy status → View app bundles** for each app. Note every flagged version
   code **and its track**.
2. Land the 3-file code fix; verify `assembleDebug` + `bundleRelease` and badging.
3. Ship a compliant build to **every track holding a flagged bundle** — closed *and*
   internal if both appear:
   - JACL: `gh workflow run android-release.yml -f play_track=alpha` (input is
     `play_track`, a Play track id: `internal`, `alpha`, …).
   - Wryter: `gh workflow run android-release.yml -f track=internal` (input is
     `track`, a **fastlane lane name**: `internal` or `closed`). Same flag name,
     different meaning — read the workflow before dispatching.
4. If managed publishing is on: **Publishing overview → Publish changes**.
5. Confirm the track's release summary shows the **new version code** — not just that
   CI was green — and that Policy status is clear.

Adding a build to an existing closed track does **not** reset the 12-tester/14-day
production gate, so it is safe to push mid-gate.
