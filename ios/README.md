# JACL for iPad

A native iPad app that plays JACL games stored on the device — the user
imports a game file (not games baked into the binary) and plays it, the way
Frotz imports Z-machine story files.

## Decisions made

- **iPad only.** JACL wants the screen real estate and a Bluetooth keyboard.
- **Ship pre-processed `.j2` files**, not raw `.jacl`. (See "The `.j2` model".)
- **Backend: RemGlk + SwiftUI.** JACL's Glk output is turned into JSON by
  RemGlk; a native SwiftUI front-end renders it. Modern, fully owned, no
  legacy Objective-C. (RemGlk is vendored under `remglk/`.)

## Design principle

The iOS app is a **fully isolated build that reuses the existing portable
interpreter core unchanged.** It touches none of the existing targets
(`jacl`, `cjacl`, `cgijacl`, `fcgijacl`, `bjorb`). The only interpreter-side
addition is `ios_startup.c`, which replaces `glk_startup.c` exactly the way
`winglk_startup.c` does for Windows.

```
        existing core (unchanged)              iOS-only
 ┌───────────────────────────────────────┐  ┌──────────────┐
 │ interpreter.c loader.c parser.c …      │  │ ios_startup.c│
 │ jacl.c  glk_saver.c  jpp.c  libcsv.c   │  │ jacl_ios.h   │
 └───────────────────────────────────────┘  └──────────────┘
                     │                              │
                     └───────────── link ───────────┘
                                    │
        ┌───────────────────────────┼───────────────────────────┐
   desktop sims (this dir):   device build (Xcode):
   GlkTerm / CheapGlk         libremglk.a  ◀── JSON ──▶  SwiftUI
```

## Status — what is proven today

The C foundation is **done and validated on the desktop**:

- `ios_startup.c` + the unmodified core **build and run** against GlkTerm
  (`make sim`), CheapGlk (`make cheap`), and **RemGlk** (`make remglk`).
- `make remglk-test G=…` runs any `.j2` and prints the JSON RemGlk emits —
  window layout, styled text, and the input request. JACL drives RemGlk
  correctly end to end (status grid + buffer + line input all verified).

What remains is the **SwiftUI app shell** — almost all iOS app code, not JACL
code. See "Remaining work".

## Build & test the desktop sims

```sh
cd ios
make remglk                                   # builds ./iosjacl-remglk (JSON)
make remglk-test G=../projects/temp/life.j2   # see the JSON for a game
make sim && ./iosjacl-sim ../projects/grail.jacl   # interactive terminal
```

`remglk/` is vendored RemGlk (Plotkin); `make remglk` builds `libremglk.a`
from it. Its `glkstart.c` sample is **not** compiled — `ios_startup.c`
supplies `glkunix_startup_code` instead.

## The `.j2` distribution model

Games ship **pre-processed** (`.j2`). This is the key iOS decision, because a
`.j2`:

- **is self-contained** — `jpp`'s `process_file()` recursively inlines every
  `#include`, so the standard library, language files and CSV data are baked
  in. One file = one game.
- **needs no preprocessing on device** — `jpp()` sees `#processed` in the
  first ten lines and returns the file untouched (`jpp.c:75`); no writable
  `temp/` or `includes/` dir is needed.
- **leaves saves as the only thing written**, and those go through the Glk
  fileref API (`glk_saver.c`) straight into the app sandbox.

Author workflow: build the game on desktop, ship the `temp/<game>.j2` it
produces (`-release` for the encrypted, debug-free form). Media games also
ship the sibling `<game>.blorb`.

> **Version gate:** `jpp.c:79` refuses a `.j2` whose `#processed:VERSION` is
> newer than the interpreter. Keep the app's `INTERPRETER_VERSION` current.

## The RemGlk JSON contract (what SwiftUI parses)

One `init` event in → one `update` out, then it blocks for the next input
event. A real first update (trimmed):

```jsonc
{ "type":"update", "gen":1,
  "windows":[                       // window geometry, in grid cells
    {"id":22,"type":"grid","gridwidth":80,"gridheight":1,"left":0,"top":0,"width":80,"height":1},
    {"id":19,"type":"buffer","left":0,"top":1,"width":80,"height":39}],
  "content":[
    {"id":22,"lines":[ {"line":0,"content":[{"style":"user1","text":" The boardroom … Score: 0 "}]} ]},
    {"id":19,"text":[ {"content":[{"style":"normal","text":"You are in a room."}]},
                      {"content":[{"style":"normal","text":"> "}]} ]}],
  "input":[ {"id":19,"type":"line","maxlen":255} ] }
```

- **`windows`** — a `grid` window (fixed character grid; JACL's status line /
  game board) and a `buffer` window (the scrolling transcript). Render the
  grid monospaced; the buffer as a paragraph stream.
- **`content`** — grid windows send `lines` (absolute, by row); buffer
  windows send `text` (append/new paragraphs). Each run carries a `style`
  (`normal`, `user1`, `alert`, `header`, …) → map to SwiftUI text styling.
- **`input`** — `line` (show a text field on that window id) or `char`. Send
  back `{"type":"line","gen":N,"window":19,"value":"…"}`.

Outgoing events: `init` (with display metrics), `line`, `char`, `arrange`
(on rotation/resize), `redraw`, `specialresponse` (file prompts), `hyperlink`.

## iOS embedding architecture (the one genuinely new piece)

RemGlk normally runs as a subprocess talking JSON over stdin/stdout. iOS
forbids subprocesses, so we embed it:

1. **Thread.** Run the terp (`libremglk.a`'s `glk_main` loop) on a background
   thread; the SwiftUI main thread stays responsive.
2. **Pipe.** A `socketpair()` bridges the two: `dup2` one end onto the terp
   thread's stdin/stdout (the app makes no other use of them), and the
   SwiftUI side reads/writes JSON on the other end. RemGlk needs **no change**
   for this.
3. **Clean exit.** `glk_exit()` ends with `exit()`, which would kill the app.
   The single required RemGlk patch: under a `JACL_IOS_EMBED` macro (set only
   in the Xcode target, never in these sims), make `glk_exit` `pthread_exit`
   instead, so quitting a game just ends the terp thread.

`GlkBridge.swift` owns this; `jacl_bridge.{h,c}` is the thin C entry it calls.

## Content caveat: Glk games vs. web games

JACL games come in two presentation styles, and this app is a **Glk**
interpreter:

- **Console / Glk games** — plain text, a Glk status grid, Glk graphics/sound
  via blorb, Glk hyperlinks. These render natively and well.
- **Web games** — authored for `cgijacl`, they emit raw HTML/CSS/JS (e.g.
  `football.j2` prints `<div class="pitch"><img …>`). Glk has no concept of
  HTML, so that markup would appear as literal text.

Not an architecture problem, but it bounds which games are iPad-ready. Open
question for later: have the SwiftUI buffer interpret a small HTML subset, or
treat web-only games as out of scope and rely on games branching on
interpreter mode. Default for v1: **pure Glk presentation.**

## Remaining work (staged)

- [x] Isolated build reusing the core
- [x] `ios_startup.c` start-up shim (path from app, not argv)
- [x] Desktop sims (GlkTerm / CheapGlk / RemGlk) proving the `.j2` path
- [x] RemGlk JSON contract captured
- [x] `jacl_bridge.{h,c}` + `glk_exit`→`pthread_exit` & `main`→`remglk_main`
      patches (`JACL_IOS_EMBED`, guarded) — all syntax-checked
- [x] Codable models for the JSON (`JACL/RemGlkProtocol.swift`) — from the
      captured output
- [x] `JACL/GlkBridge.swift` — terp thread + socketpair + JSON pump
- [x] SwiftUI buffer/grid/input views + Files import + game shelf
- [x] **XcodeGen `project.yml`; builds & runs on the iPad simulator (iOS 26.5)**
- [x] Game titles on the shelf (plain & encrypted `.j2`)
- [x] Input round-trip verified (scripted commands advance the game)
- [x] Graphics: blorb images render (bridge fetches pixels via giblorb;
      needs `<prefix>.blorb` imported alongside the `.j2`)
- [ ] ~~Sound~~ — descoped: RemGlk has no sound support, and games' audio is
      web-oriented. The `ios` flag skips the sound question; `sound_enabled`
      stays off.
- [ ] Measure the real monospace cell for exact grid (status-line) metrics
- [x] `.jaclgame` package (zip of `.j2`+`.blorb`) — `mkjaclgame.sh` packs it;
      import unpacks via `MiniZip` (no zip dependency); exported UTI registered
- [x] "Open in JACL" — `.onOpenURL` imports `.jaclgame`/`.j2` from Files /
      AirDrop / Mail / Safari (and the picker accepts all three types)
- [x] Bundled starter games — Grail + The Down Dragon ship in the app bundle
      (release `.jaclgame`s in `StarterGames/`), seeded into `Documents` on
      first launch by `installBundledStarters()`
- [ ] Autosave / restore on background (core save path is ready)
- [ ] Optional in-app catalog (download more games from a URL)
- [ ] App Store review hardening (data-not-code framing; bundled samples)

> The SwiftUI front-end builds and runs on the iPad simulator: a real game
> renders (status grid + transcript), takes line input that advances the game,
> and lists on the shelf by title (plain & encrypted `.j2`). Remaining
> front-end work is graphics/sound, autosave, and polish.

## Xcode assembly (next session, on the Mac)

1. New Xcode project → iOS App, SwiftUI, **iPad only** (`UIDeviceFamily=[2]`),
   `LSSupportsOpeningDocumentsInPlace`, `UIFileSharingEnabled`.
2. Add the C core + `ios_startup.c` + `jacl_bridge.c` + `remglk/*.c` (minus
   `glkstart.c`) to the target. Set `-DGLK -DNATIVE_LANGUAGE=1 -DJACL_IOS_EMBED`,
   header search paths `../src` and `../src/glkterm`.
3. Bridging header imports `jacl_bridge.h` + `jacl_ios.h`.
4. Drop in the `JACL/*.swift` files.
5. Build & run on an iPad simulator; import a `.j2` via Files.

## Run on a real iPad

The simulator needs no code signing; a device does. One-time, with a **free**
Apple ID (good enough for your own iPad — the signature expires after 7 days,
just re-run from Xcode to refresh; games in Documents survive):

1. `cd ios && xcodegen generate && open JACL.xcodeproj`
2. JACL target → **Signing & Capabilities** → tick **Automatically manage
   signing** → **Team** → add/select your Apple ID. Bundle id
   `au.com.famecapital.JACL` is a domain you own, so there's no collision.
3. iPad: plug in, **Trust This Computer**, then enable **Settings → Privacy &
   Security → Developer Mode** (iPadOS 16+; the toggle appears once the device
   has talked to Xcode) and restart.
4. Pick the iPad in Xcode's destination menu → **⌘R**.
5. First launch is blocked as "Untrusted Developer": on the iPad,
   **Settings → General → VPN & Device Management → [your Apple ID] → Trust**,
   then reopen the app.

Needs **iPadOS 17.0+** (the deployment target). For no expiry, sharing,
TestFlight, or the App Store you need the paid **Apple Developer Program**
($99/yr) — see below.

## Submitting to the App Store

Requires the paid **Apple Developer Program** ($99/yr). The build is in decent
shape already: the bundled games clear the minimum-functionality bar (4.2) and
let a reviewer test offline; no data is collected; the terp ends with
`pthread_exit`, not `exit()`. Open items before a first submission:

- [x] **App icon** — final art in `JACL/Assets.xcassets/AppIcon.appiconset/icon-1024.png`
  (1024², no alpha, full-bleed; gold "JACL" + compass rose on navy leather),
  wired via `ASSETCATALOG_COMPILER_APPICON_NAME`. Verified with `actool`.
- [x] **`ITSAppUsesNonExemptEncryption = false`** — set in `project.yml` (the
  `.j2` XOR-obfuscation isn't non-exempt crypto); skips the export prompt.
- [x] **Privacy policy URL** — live at
  <https://jacl.dangarmarine.com.au/privacy.html> (source `etc/privacy.html`,
  deployed by `make apache`). Still to do *in App Store Connect*: set the
  **App Privacy** label to *Data Not Collected* (a form, not a URL).
- [ ] **Store name** — "JACL" alone is cryptic; e.g. "JACL Interactive Fiction"
  (the bundle id stays the same).
- [ ] **Screenshots** at the required iPad size (currently 13-inch, 2048×2732).
- [ ] **Age rating** questionnaire — assess the bundled games' content.
- [ ] Bump `CFBundleShortVersionString` to `1.0`.
- [x] **In-app privacy link** — a Settings sheet (gear on the shelf) links to
  the hosted policy. Not required here (no accounts, no data collection), but
  it's wired (`JACL/SettingsView.swift`) and builds.

### App Review notes (paste into the submission's "Notes" field)

> JACL is an interpreter for interactive fiction (text adventures) written in
> the open JACL language — the same kind of app as Frotz (Z-machine) or Hunky
> Punk (TADS), which interpret story files.
>
> • Games are data/content files (.j2 / .jaclgame) interpreted by the app. They
>   are not native or executable code and do not change the app's behaviour or
>   functionality.
> • The app has no accounts and no sign-in of any kind — it does not use Sign in
>   with Google, Sign in with Apple, or any other login service.
> • The app collects no data and makes no network connections; everything runs
>   on-device.
> • Two complete games ship bundled (The Unholy Grail, The Down Dragon), so the
>   full experience can be tested offline immediately — no download or account
>   needed.
>
> Privacy policy: https://jacl.dangarmarine.com.au/privacy.html

That one note pre-empts both the 2.5.2 / 4.7 (downloadable content) and 4.8
(Sign in with Apple) questions. IF interpreters (Frotz, Hunky Punk) have long
App Store precedent.

## Source manifest (what the Xcode target compiles)

`ios_startup.c` + `jacl_bridge.c` + `../src/jacl.c` + the shared core
(`findroute interpreter loader logging parser display utils jpp resolvers
errors encapsulate libcsv`) + `../src/glk_saver.c` + `remglk/*.c` (excluding
`glkstart.c`), with `-DGLK -DNATIVE_LANGUAGE=1 -DJACL_IOS_EMBED`, **without**
`-DGARGLK` / `-DWINGLK`. See `Makefile` for the exact desktop-sim flags.
