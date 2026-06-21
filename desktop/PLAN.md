# JACL Desktop (Electron) — Plan

Goal: a cross-platform **Mac / Linux / Windows** desktop app for JACL with a
**separate, resizable map window** that works like the native Mac (Catalyst)
app — a real second OS window, not a Glk pane. Built as background work while the
App Store / Google Play 1.2 releases ship.

## Decisions (made)

- **Shell: Electron** (over Tauri). Lectrote is a proven template on the exact
  stack, and Node makes driving the C interpreter trivial. Trade-off accepted:
  larger binary (~100 MB) for guaranteed-consistent Chromium rendering everywhere.
- **Engine stack: RemGlk + GlkOte** — the JSON-Glk + JS-UI pair JACL already
  speaks (the iOS/Android apps embed RemGlk; the web map is GlkOte). We reuse it
  rather than inventing a desktop UI.
- **Map = a second `BrowserWindow`**, reusing the *existing web map renderer*.

## Why this is tractable (not a core rewrite)

The library already emits the map data for native clients. `mapping.library`'s
`+map` has three branches:
- `interpreter == CGI` → inline web (Raphael) rendering
- `interpreter != CGI && ios == true` → emit the `<jacl-map>` data block (`+map_native`)
- else → "only available in the web interpreter"

So the desktop just needs to (a) make the engine report as a native client so the
`<jacl-map>` block is emitted, and (b) render that block in a window — **a frontend
+ renderer job, not a library/core rewrite.**

## Architecture

```
Electron main (Node)                 Renderer (Chromium)
  └─ spawn: jacl --remglk  ── stdio(JSON) ──>  GlkOte  → game window
       (RemGlk build of JACL)                    │
                                                 └─ on <jacl-map> → main: openWindow("map")
                                                                         │
                                          Map BrowserWindow ── web map renderer (reused)
```

- The engine is a **standalone RemGlk JACL CLI** speaking GlkOte/RemGlk JSON on
  stdin/stdout (same protocol the apps use, just a normal `main()` instead of the
  iOS embedded variant).
- The renderer intercepts the `<jacl-map>` block (as the iOS/Android bridges do)
  and asks main to open/refresh the map window.

## Roadmap (phased — each step independently testable)

1. **RemGlk JACL CLI.** Add a Makefile target linking `src/` core + vendored
   `remglk` + a normal `main()` → a binary that plays a `.j2` and speaks RemGlk
   JSON on stdio. Smoke-test with a scripted session. *(Critical path — do first.)*
2. **Electron skeleton.** `main.js` spawns the CLI; one `BrowserWindow` runs
   GlkOte wired to the process. Confirm a game is playable.
3. **Map window.** Detect `<jacl-map>` in the stream; open a second
   `BrowserWindow` that draws it with the **existing web map code**. Resizable,
   re-renders on each `map`.
4. **(Later) live refresh** — auto-request the map each turn (shared with the
   Catalyst app's pending live-map work).
5. **Packaging.** electron-builder for Mac / Linux / Windows; bundle the CLI per
   platform.

## To verify / open questions

- Does a RemGlk standalone build exist already, or is it new? (Apps embed RemGlk
  with `JACL_IOS_EMBED`; the CLI wants the plain `main()`.)
- Reuse the web map renderer from `cgijacl`'s output, or port the `<jacl-map>`
  parser the apps already have (`GameMap.swift`/`.kt`) to JS.
- How the engine signals "native client" so `+map_native` fires (an `ios`-style
  flag, or a new desktop flag in `mapping.library`).

## Status

- [ ] 1. RemGlk JACL CLI
- [ ] 2. Electron skeleton + GlkOte
- [ ] 3. Separate map window
- [ ] 4. Live refresh
- [ ] 5. Packaging
