# JACL — Release & Install Status

A single place to answer: *what's submitted to the stores, what's installed on
which device (and how), and what's changed since the store versions.* Update it
whenever you submit a build or sideload a device.

Structured version log: [`android/release-tracker.csv`](android/release-tracker.csv).

_Last updated: 2026-07-05_

---

## 1. Store status (what the public / testers can get)

| Store | Version | Build | Status | Notes |
|---|---|---|---|---|
| **App Store** | **1.3** | **1013** | **Live** (as of 2026-07-05) | Current public release — universal iPhone + iPad. Confirmed `READY_FOR_SALE` via ASC API. 1.0–1.2 released and superseded. |
| **Google Play** | **1.3** | **1008** | **Closed test** — rollout complete | On the `"1.0 Testing"` track; self-service via the `jacl-testers` Google Group. Recruiting 20 testers × 14 days before production. |

> The newest **public** release is **iOS 1.3** (App Store, live). **Android 1.3** is
> in **closed testing** — the store gate is 20 testers × 14 continuous days before
> production. §3 below is stale (1.2 and 1.3 have since shipped).

### Desktop & web

| Channel | Platforms | Distribution | Feature level |
|---|---|---|---|
| **Desktop app (Electron) — DEFAULT** | Mac, Linux *(Windows pending the `cgijacl` POSIX port)* | **GitHub Releases** (`desktop-v*` tags → `desktop.yml`); first cut **`desktop-v0.1`** (unsigned) | **Full web parity:** HTML forms, the **live map window**, the iPad-style bookshelf, Library button, reading controls. Hosts `cgijacl` locally. **This is now the primary desktop build.** |
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
  sync.** They drifted (iOS 1.1, Android 1.0.1); reconciled forward to **1.2** for
  the next release — see §3.
- **Interpreter core** (`src/version.h`, shown in-app as "JACL v4.7.0"): **4.7.0**,
  shared by every platform (apps, web, desktop). Independent of the app version.

---

## 2. Local installs (your devices — keep this honest)

Method = **Store** (downloaded) or **Sideload** (Xcode/devicectl/adb dev build).
Sideloaded builds are dev builds off the current branch, *ahead of the stores*.

| Device | OS | Build on it | Method | As of | Notes |
|---|---|---|---|---|---|
| iPhone "WOPR" (15 Pro) | iOS | dev `mac-native` @ edd452c | Sideload (devicectl) | 2026-06-21 | keyboard fix, margins |
| iPad "Vicki's iPad" (Air 4) | iPadOS | ~1.1-era sideload *(verify)* | Sideload | ~2026-06-20 | confirm current build |
| Mac (this machine, M5 Pro) | macOS (Catalyst) | dev `mac-native` | Run locally (`build_mac`) | 2026-06-21 | menu/map-window work |
| Android tablet (Samsung, R5GYB4191FF) | Android | dev `mac-native` @ edd452c | Sideload (adb) | 2026-06-21 | keyboard fix |
| Android emulator (Medium_Phone_API_35) | Android | debug build | Sideload (adb) | 2026-06-21 | |

---

## 3. Unreleased — changed since the store versions (NOT yet submitted)

> ⚠️ **Stale (2026-07-05):** the 1.2 and 1.3 releases described below have since
> shipped — iOS **1.3** is live on the App Store and Android **1.3** is in closed
> testing. This section needs reconciling against `git log` for whatever is
> genuinely unreleased now.

All on branch **`mac-native`**, now bumped to **1.2** in the working tree (iOS
build 3, Android vc 3) — iOS and Android **reconciled to the same marketing
version**. Ready for the next submission once merged (let 1.1 / 1.0.1 clear review
first).

### iOS / iPadOS (next App Store update)
- Banner & in-game images sized to the **text-view width** (window-relative), not the whole screen.
- **Wryter-style margins** (Narrow / Normal / Wide) + the **scroll bar out in the margin**; phones default to Narrow.
- Status bar spans the full window; reading column centred.
- **Map zoom** controls (pinch / scroll-wheel / +− buttons).
- **Keyboard holds steady** between turns (no drop-and-reraise flicker).
- **Restart** no longer leaves "The game has ended." stuck (stale-reader race fixed).

### Mac (brand-new platform — Catalyst; never shipped)
- Native Mac build: menu-bar **Game** commands (text size / map / restart), a **bottom control bar**, a **live, separate map window**, and a **settings window**.
- Crash-safe on macOS (the modal-over-keyboard crashes are gone).

### Android (next Google Play update)
- **Keyboard holds steady** between turns (no flicker).
- **Restart** no longer leaves "The game has ended." stuck.

---

## How to update this
- **Submitted a build?** Add/adjust the row in §1 and `release-tracker.csv`, and move shipped items out of §3.
- **Sideloaded a device?** Update its row in §2 (build + date).
- The §3 changelog ≈ `git log <last-submitted-tag>..HEAD` — see `release-tracker.csv` for the exact commits if needed.
