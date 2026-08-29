# JACL — Release & Install Status

A single place to answer: *what's submitted to the stores, what's installed on
which device (and how), and what's changed since the store versions.* Update it
whenever you submit a build or sideload a device.

Structured version log: [`android/release-tracker.csv`](android/release-tracker.csv).

_Last updated: 2026-08-30_

---

## 1. Store status (what the public / testers can get)

| Store | Version | Build | Status | Notes |
|---|---|---|---|---|
| **App Store** | **1.3** | **1013** | **Live** (as of 2026-07-05) | Current public release — universal iPhone + iPad. Confirmed `READY_FOR_SALE` via ASC API. 1.0–1.2 released and superseded. |
| **Google Play** | **1.3** | **1011** | **Closed test** — rollout complete | On the `"1.0 Testing"` (alpha) track; self-service via the `jacl-testers` Google Group. Recruiting 20 testers × 14 days before production. Build **1011** **submitted to the `"1.0 Testing"` track 2026-08-30** (in review) — **retargeted to API 36 (Android 16)** for Play's **31 Aug 2026** deadline (`compileSdk`/`targetSdk` 35→36; AGP 8.9.1 + Gradle 8.11.1; native untouched). A smoke build **1010** also went to the exempt *internal* track. **Two traps here, both hit on 2026-08-30.** (1) 1011 was *uploaded* 21 Jul but sat **unpublished in the managed-publishing queue for 5 weeks** — managed publishing is **on** for JACL, so a CI upload is *not* a release until you press Publish. (2) **JACL has TWO closed tracks** — `"1.0 Testing"` (the real tester channel, `jacl-testers` group) and a separate **`Alpha`** track. Fastlane's `play_track=alpha` targets **Alpha**, *not* the tester channel, so publishing the queue activated Alpha while `"1.0 Testing"` went on serving **1008** (target SDK 35) — the only bundle Play ever flagged. Fixed by adding bundle 1011 to `"1.0 Testing"` in the Console and submitting for review. |

> The newest **public** release is **iOS 1.3** (App Store, live). **Android 1.3** is
> in **closed testing** — the store gate is 20 testers × 14 continuous days before
> production. Everything is shipped and in sync (see §3); nothing is unreleased.
> The Android closed (alpha) build now targets **API 36 (Android 16)**, satisfying
> Play's **31 Aug 2026** target-API deadline; adding it to the same track does not
> reset the 20-tester/14-day gate. **Submitted to `"1.0 Testing"` 2026-08-30**, one
> day before the deadline, after it was found that the July upload had gone to the
> *other* closed track (`Alpha`) and had never been published at all.

### Desktop & web

| Channel | Platforms | Distribution | Feature level |
|---|---|---|---|
| **Desktop app (Electron) — DEFAULT** | Mac, Windows, Linux (incl. arm64) | **GitHub Releases** (`desktop-v*` tags → `desktop.yml`, all four targets build); current **`desktop-v0.2.15`** (mac signed + notarized) | **Full web parity:** HTML forms, the **live map window**, the iPad-style bookshelf, Library button, reading controls. Hosts `cgijacl` locally. **This is now the primary desktop build.** |
| Desktop (Gargoyle/Glk) — *legacy* | Mac, Linux, Windows | prebuilt in `bin/` | Classic two-window Glk (text + status, inline images). **No** map window / reading polish. Superseded by the Electron app. |
| Web interpreter | Browser — jacl.dangarmarine.com.au | `cgijacl`/`fcgijacl`, deployed to the site | Full modern UI — the same engine the Electron app hosts. |
| Nintendo DS | DS | `jacl.nds` (legacy) | Historical. |

> Parity, high → low: **Electron desktop ≈ web ≈ apps** (map, rich UI) > the
> legacy **Gargoyle** text build. The web-enabled Electron app is the default
> desktop going forward; the Gargoyle binaries stay available but are no longer
> the recommended download.

---

## Versioning (two independent numbers, deliberately)

- **App marketing version** (App Store / Google Play): **iOS and Android kept in
  sync — both currently 1.3.** They drifted early (iOS 1.1 vs Android 1.0.1), then
  reconciled at 1.2 and have stayed in lockstep since.
- **Interpreter core** (`src/version.h`, shown in-app as "JACL v4.7.0"): **4.7.0**,
  shared by every platform (apps, web, desktop). Independent of the app version.

---

## 2. Local installs (your devices — keep this honest)

Method = **Store** (downloaded) or **Sideload** (Xcode/devicectl/adb dev build).
**As of 2026-07-05 the stores have caught up** — iOS 1.3 is live, Android 1.3 is in
closed testing, desktop is 0.2.15 — so the June dev sideloads are now *behind* the
store builds, not ahead; prefer reinstalling from the store/current release. Rows
marked ✓ were probed on 2026-07-05; *(unverified)* rows couldn't be read (device
offline, or the app inventory didn't enumerate) — confirm on the device.

| Device | OS | Build on it | Method | As of | Notes |
|---|---|---|---|---|---|
| Mac (this machine, M5 Pro) | macOS | **Desktop 0.2.15** (`/Applications/JACL.app`) ✓ | Store/release | 2026-07-05 | Electron app, current. Catalyst native Mac app also builds locally (`build_mac`). |
| iPad "Vicki's iPad" (Air 4) | iPadOS | *(unverified)* — App Store **1.3** available | — | 2026-07-05 | Paired at probe but app inventory didn't enumerate; recommend installing App Store 1.3. |
| iPhone "WOPR" (15 Pro) | iOS | *(unverified)* — last known dev @ `edd452c` | Sideload | 2026-06-21 | Offline at probe; App Store 1.3 now available. |
| Android tablet (Samsung, R5GYB4191FF) | Android | *(unverified)* — last known dev @ `edd452c` | Sideload (adb) | 2026-06-21 | Not connected at probe. See ‡ — a physical Android got Play 1.3 today. |
| Android emulator (Medium_Phone_API_35) | Android | **1.0.1 (vc 2)** — stale ✓ | Sideload (adb) | 2026-07-05 | Old closed-test build; update to Play 1.3 or a current dev build. |

‡ You installed **Play 1.3 (vc 1008, build stamp `9b932e1`)** on a physical Android
on 2026-07-05 via the closed-test opt-in — confirm which device (likely the Samsung
tablet) and set its row to **Store / 1.3**.

---

## 3. Unreleased — changed since the store versions

**Nothing — every platform is shipped and in sync (as of 2026-07-05).** Verified by
diffing each platform's release point against `master`: the only commits since are
CI, docs, metadata and store screenshots — no app-binary or `src/` interpreter-core
change.

| Platform | Shipped | Release point | Newer app code on `master`? |
|---|---|---|---|
| iOS / iPadOS | **1.3** (build 1013) — App Store live | tag `ios-v1.3` | none |
| Android | **1.3** (vc 1008) — closed test | tag `android-v1.3` (@ `9b932e1`) | none |
| Desktop (Electron) | **0.2.15** — GitHub Releases | tag `desktop-v0.2.15` | none |
| Interpreter core | **4.7.0** (`INTERPRETER_VERSION 470`) | — | unchanged |

The `mac-native` branch (the old home of this changelog) is **merged into `master`**;
the 1.2/1.3 App Store work and the new Mac Catalyst platform once listed here have all
shipped. When you next change app code, rebuild this section from the diff above.

> **Note:** Android 1.3 shipped via a manual `android-release.yml` dispatch (built
> @ `9b932e1`), not a tag push, so the `android-v1.3` tag was added **retroactively
> on 2026-07-05** → `9b932e1`, keeping the `ios-v* / android-v* / desktop-v*` scheme
> complete. `ios-v1.3` sits one commit earlier at `defab1c` (the version bump); the
> only diff to `9b932e1` is the android-tracks diagnostic — no app code.

---

## How to update this
- **Submitted a build?** Add/adjust the row in §1 and `release-tracker.csv`, and move shipped items out of §3.
- **Sideloaded a device?** Update its row in §2 (build + date).
- The §3 changelog ≈ `git log <last-submitted-tag>..HEAD` — see `release-tracker.csv` for the exact commits if needed.
