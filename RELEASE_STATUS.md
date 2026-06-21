# JACL — Release & Install Status

A single place to answer: *what's submitted to the stores, what's installed on
which device (and how), and what's changed since the store versions.* Update it
whenever you submit a build or sideload a device.

Structured version log: [`android/release-tracker.csv`](android/release-tracker.csv).

_Last updated: 2026-06-21_

---

## 1. Store status (what the public / testers can get)

| Store | Version | Build | Status | Notes |
|---|---|---|---|---|
| **App Store** | 1.0 | 1 | Live | iPad-only — original release |
| **App Store** | **1.1** | **2** | **Waiting for Review** (submitted 2026-06-20) | First universal (iPhone + iPad) |
| **Google Play** | 1.0 | 1 | Closed test | First closed-test build |
| **Google Play** | **1.0.1** | **2** | **In review** | Landscape reading-margins fix |

> The newest thing the public can get is **iOS 1.1 / Android 1.0.1**. Everything
> in §3 below is **newer than that and not submitted anywhere yet.**

### Desktop & web (the original interpreter, core **v4.7.0**)

| Channel | Platforms | Distribution | Feature level |
|---|---|---|---|
| Desktop (Gargoyle/Glk) | Mac, Linux, Windows | Website downloads / prebuilt in `bin/` | Classic two-window Glk (text + status, inline images). **No** map window, reading margins, or in-app settings. |
| Web interpreter | Browser — jacl.dangarmarine.com.au | `cgijacl`/`fcgijacl`, deployed to the site | Full modern UI: graphical map, hyperlinks, graphics. |
| Nintendo DS | DS | `jacl.nds` (legacy) | Historical. |

> Parity, high → low: **web ≈ apps** (map, rich UI) > **desktop** (classic Glk
> text). Desktop builds are mature *as text interpreters* but don't share the
> apps' map/reading polish — by design (Gargoyle is a standard Glk UI).

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
