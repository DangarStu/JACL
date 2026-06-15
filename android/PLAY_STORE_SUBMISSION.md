# JACL — Google Play Submission

Companion to `ios/APP_STORE_SUBMISSION.md`. The app is built for Android
(`au.com.dangarmarine.jacl`, versionName 1.0 / versionCode 1, target API 35).
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
| Phone screenshots | ≥2, 16:9-ish | ✏️ capture on a phone emulator (Medium_Phone_API_35 is installed) |
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
