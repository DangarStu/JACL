# JACL Desktop (Electron) — Plan

Goal: a cross-platform **Mac / Linux / Windows** desktop app for JACL with a
**separate, resizable map window** like the native Mac (Catalyst) app — and,
because it's a real browser, **full web-feature parity** (HTML forms, JavaScript,
hyperlinks, graphics, the rich map) that the native apps *can't* do. Games like
**Blackjacl** that use HTML forms would run here. Background work while the App
Store / Google Play 1.2 releases ship.

## Decisions (made)

- **Shell: Electron** (over Tauri). Proven (Lectrote), and Node makes hosting the
  engine trivial. Larger binary accepted for consistent Chromium rendering.
- **Engine: `cgijacl`** — the existing **web** interpreter (raw HTML output),
  **not** RemGlk/GlkOte. This is the key call: only `cgijacl` renders the HTML the
  web-flavoured games (forms, JS, map JS) emit. RemGlk → a custom renderer with no
  HTML; `cgijacl` → a real web page. We want the web page. Already built in `bin/`.
- **Map = a second `BrowserWindow`**, reusing the existing web map renderer.

## Why this beats the native apps

`mapping.library`'s `+map` has three branches: `interpreter == CGI` (inline web
HTML/Raphael), `ios == true` (a custom `<jacl-map>` data block the native apps
draw), else "web only". The native apps take the **data** path and render it with
custom code — they never run arbitrary HTML. By hosting **`cgijacl`** the desktop
takes the **web** path and gets the full HTML experience: forms, JS, hyperlinks,
graphics, the Raphael map — a superset of the native apps.

## Architecture

```
Electron main (Node)                         Renderer (Chromium)
  ├─ local HTTP server  ── http://127.0.0.1 ──>  BrowserWindow (game)
  │    ├─ runs cgijacl as a CGI per request          │  full web JACL: text, forms, JS
  │    └─ serves the web frontend assets             └─ map → main: openWindow("map")
  └─ manages one local game session                        │
                                              Map BrowserWindow ── web map (reused)
```

The whole web app runs locally; Electron is just the browser + window manager.
The map opens in its own `BrowserWindow` for a real, resizable second window.

## Roadmap (phased — each step testable)

1. **Local `cgijacl` host.** A Node HTTP server (in Electron main) that runs
   `bin/cgijacl` as a CGI (env, stdin/stdout, query/POST) and serves the web
   frontend, so a game is playable at `127.0.0.1` in a normal browser. *(Critical
   path + riskiest: CGI env, `cgijacl.conf`, temp dirs, one-session state.)*
2. **Electron shell.** Point a `BrowserWindow` at the local server; confirm a game
   (incl. an HTML-form game like Blackjacl) plays.
3. **Map window.** Route the map into a second `BrowserWindow` (reuse the web map
   renderer), resizable, re-rendered each `map`.
4. **(Later) live refresh** — auto-request the map each turn.
5. **Packaging.** electron-builder for Mac / Linux / Windows; bundle `cgijacl` +
   frontend + the game-state/config setup per platform.

## To verify / open questions

- `cgijacl`'s required environment: `cgijacl.conf`, a writable **temp dir** (the
  known "missing temp dir" failure), and how it scopes one local session.
- Where the web **frontend assets** live (the play page + map JS) and how to bundle
  them — `etc/`, the `*.library` HTML/JS emitters, CSS.
- Reuse the inline web map, or pull it into the dedicated map window (it currently
  renders into `#maintext`).
- Superseded: the RemGlk-CLI path (Glk-only, no HTML) — not pursued.

## Status

- [ ] 1. Local cgijacl host (HTTP+CGI, game playable at 127.0.0.1)
- [ ] 2. Electron shell
- [ ] 3. Separate map window
- [ ] 4. Live refresh
- [ ] 5. Packaging
