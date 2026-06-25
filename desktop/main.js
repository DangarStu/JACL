// JACL desktop (Electron) — main process.
//
// Hosts the full web JACL locally: cgijacl has a built-in web server
// (`cgijacl -p <port> <game.j2>`), so we spawn it and load the URL in a
// BrowserWindow. One cgijacl serves one game, so picking a game (re)starts it on
// a fresh port (avoids rebinding a port still in TIME_WAIT). The map opens in its
// own resizable BrowserWindow — the web's "map window" popup, honoured by
// setWindowOpenHandler.
//
// Paths differ dev vs packaged (see resolvePaths): dev runs against the repo in
// place; a packaged app bundles cgijacl + a `jacl-data` tree (published games +
// include/www/images/sounds + games.json) as resources, and seeds a writable copy
// into userData so cgijacl can write per-game .media manifests and temp files.

const { app, BrowserWindow, Menu, ipcMain } = require('electron')
const { spawn } = require('child_process')
const path = require('path')
const fs = require('fs')

// Resolved by resolvePaths() once the app is ready.
let CGIJACL, GAMES_DIR, SOURCES_DIR, INCLUDE_DIR, WWW_DIR, IMAGES_DIR, SOUNDS_DIR, RUN, GAMES_JSON

let win = null
let server = null            // the current cgijacl child process
let activePort = 8098        // pre-incremented on each game launch
let gameRetries = 0          // retry budget for loading the game URL while cgijacl binds
let reading = { columns: 64, margin: 'normal' }   // iPad-style reading prefs (persisted)

// --- paths ----------------------------------------------------------------
// Copy the bundled (read-only) jacl-data into a writable userData dir on first
// launch / after an upgrade, so cgijacl can write .media manifests next to games.
function seedData (src, dst) {
  const marker = path.join(dst, '.seeded-' + app.getVersion())
  if (fs.existsSync(marker)) return
  fs.rmSync(dst, { recursive: true, force: true })
  fs.cpSync(src, dst, { recursive: true })
  fs.writeFileSync(marker, '')
}

function resolvePaths () {
  if (!app.isPackaged) {
    const REPO = path.resolve(__dirname, '..')
    // Prefer the app's own freshly-built cgijacl (desktop/build-cgijacl.sh) -- the
    // repo's bin/cgijacl is root-owned and was stale (broken media serving).
    const local = path.join(__dirname, 'bin', 'cgijacl')
    CGIJACL = fs.existsSync(local) ? local : path.join(REPO, 'bin', 'cgijacl')
    GAMES_DIR = path.join(REPO, 'projects', 'temp')
    SOURCES_DIR = path.join(REPO, 'projects')        // dev reads game_publish from here
    INCLUDE_DIR = path.join(REPO, 'projects', 'include')
    WWW_DIR = path.join(REPO, 'projects', 'www')
    IMAGES_DIR = path.join(REPO, 'projects', 'images')
    SOUNDS_DIR = path.join(REPO, 'projects', 'sounds')
    RUN = path.join(__dirname, 'run')
    GAMES_JSON = null                                // dev scans sources instead
  } else {
    const res = path.join(process.resourcesPath, 'jacl-data')
    const base = path.join(app.getPath('userData'), 'jacl-data')
    seedData(res, base)
    const exe = process.platform === 'win32' ? 'cgijacl.exe' : 'cgijacl'
    CGIJACL = path.join(process.resourcesPath, 'bin', exe)
    GAMES_DIR = path.join(base, 'games')
    SOURCES_DIR = null
    INCLUDE_DIR = path.join(base, 'include')
    WWW_DIR = path.join(base, 'www')
    IMAGES_DIR = path.join(base, 'images')
    SOUNDS_DIR = path.join(base, 'sounds')
    RUN = path.join(app.getPath('userData'), 'run')
    GAMES_JSON = path.join(GAMES_DIR, 'games.json')  // prebuilt manifest of published games
  }
}

// --- games ----------------------------------------------------------------
function prettify (stem) {
  return stem.replace(/[_-]+/g, ' ').replace(/\b\w/g, c => c.toUpperCase())
}
// Dev (no games.json) shows the game's own `constant game_title "..."` -- the
// same value the packaged games.json, the website and the apps use -- so the
// bookshelf matches everywhere; prettified filename as the fallback.
function gameTitle (stem) {
  try {
    const m = fs.readFileSync(path.join(SOURCES_DIR, stem + '.jacl'), 'utf8')
      .match(/^constant\s+game_title\s+"([^"]+)"/m)
    if (m) return m[1]
  } catch (e) { /* fall back to the prettified stem */ }
  return prettify(stem)
}
// Best-effort language tag from the filename (the .j2 is obfuscated, so reading
// its game_language constant isn't cheap). Defaults to English.
const LANG = [
  [/indonesia|desa/i, 'Indonesian'], [/german|deutsch/i, 'German'],
  [/french|francais|paris|camion|fihnarga/i, 'French'], [/spanish|espanol|montana|vida/i, 'Spanish']
]
function detectLanguage (stem) {
  for (const [re, lang] of LANG) if (re.test(stem)) return lang
  return 'English'
}
// A game is "published" iff its source declares `constant game_publish true` near
// the top -- the same rule the website (etc/gen-landing.sh, build-jaclgames.sh)
// uses. Only consulted in dev; packaged builds bundle a games.json of already-
// published games (prepare-bundle.sh).
function isPublished (stem) {
  try {
    return /^constant\s+game_publish\s+true\b/m.test(
      fs.readFileSync(path.join(SOURCES_DIR, stem + '.jacl'), 'utf8'))
  } catch (e) { return true }
}
function listGames () {
  if (GAMES_JSON && fs.existsSync(GAMES_JSON)) {
    try {
      return JSON.parse(fs.readFileSync(GAMES_JSON, 'utf8'))
        .map(g => ({ ...g, path: path.join(GAMES_DIR, g.file) }))
    } catch (e) { /* fall through to a directory scan */ }
  }
  let files = []
  try { files = fs.readdirSync(GAMES_DIR) } catch (e) { return [] }
  return files.filter(f => f.endsWith('.j2')).map(f => f.replace(/\.j2$/, ''))
    .filter(isPublished).sort()
    .map(stem => ({ title: gameTitle(stem), language: detectLanguage(stem), path: path.join(GAMES_DIR, stem + '.j2') }))
}

// --- cgijacl config + media ----------------------------------------------
function writeConfig () {
  fs.mkdirSync(path.join(RUN, 'temp'), { recursive: true })
  fs.mkdirSync(path.join(RUN, 'logs'), { recursive: true })
  const conf = [
    `access_log\t"${path.join(RUN, 'logs', 'access.log')}"`,
    `error_log\t"${path.join(RUN, 'logs', 'error.log')}"`,
    `include\t\t"${INCLUDE_DIR}/"`,
    `temp\t\t"${path.join(RUN, 'temp')}/"`,
    'cookie_expiry\t20000',
    ''
  ].join('\n')
  fs.writeFileSync(path.join(RUN, 'cgijacl.conf'), conf)
}

// webjacl serves ALL static files (raphael.min.js, images, the header banner)
// from a "<gamecore>.media" manifest next to the game file -- without it, every
// /include/* and /images/* request 404s (empty map + missing header). Generate
// one listing www/*.js and images/* with paths relative to the game's directory.
const MIME = {
  '.js': 'application/javascript', '.png': 'image/png', '.jpg': 'image/jpeg',
  '.jpeg': 'image/jpeg', '.gif': 'image/gif', '.ico': 'image/x-icon',
  '.svg': 'image/svg+xml', '.ogg': 'audio/ogg', '.mp3': 'audio/mpeg',
  '.woff2': 'font/woff2', '.woff': 'font/woff'
}
function writeMediaManifest (gamePath) {
  const gameDir = path.dirname(gamePath)
  const base = path.basename(gamePath).replace(/\.[^.]+$/, '')
  const lines = []
  const add = (urlPrefix, dir) => {
    let files = []
    try { files = fs.readdirSync(dir) } catch (e) { return }
    for (const name of files) {
      const mime = MIME[path.extname(name).toLowerCase()]
      if (!mime) continue
      lines.push(`${urlPrefix}${name} ${mime} ${path.relative(gameDir, path.join(dir, name))}`)
    }
  }
  add('/include/', WWW_DIR)
  add('/include/fonts/', path.join(WWW_DIR, 'fonts'))   // self-hosted Merriweather woff2
  add('/images/', IMAGES_DIR)
  add('/sounds/', SOUNDS_DIR)
  fs.writeFileSync(path.join(gameDir, base + '.media'), lines.join('\n') + '\n')
}

// --- server --------------------------------------------------------------
// `-p` = server mode; the game file is the last arg; cwd=RUN so it finds
// ./cgijacl.conf. Each call kills the previous server and uses a fresh port.
function startServer (gamePath) {
  if (server) { server.removeAllListeners(); try { server.kill() } catch (e) {} server = null }
  const port = ++activePort
  server = spawn(CGIJACL, ['-p', String(port), gamePath], { cwd: RUN })
  const log = d => process.stdout.write('[cgijacl] ' + d)
  server.stdout.on('data', log)
  server.stderr.on('data', log)
  server.on('exit', code => console.log('[cgijacl] exited', code))
  return port
}

// --- windows -------------------------------------------------------------
function loadGame () {
  gameRetries = 60
  win.loadURL(`http://127.0.0.1:${activePort}/`)
}

function showPicker () {
  gameRetries = 0   // stop any in-flight game-load retry loop
  win.loadFile(path.join(__dirname, 'picker.html'))
}

function createWindow () {
  win = new BrowserWindow({
    width: 1024, height: 800, title: 'JACL',
    webPreferences: { preload: path.join(__dirname, 'preload.js') }
  })
  // The web JACL's "map window" / "map open" command pops the map via
  // window.open('', 'jacl_map', ...). Honour it as a real, resizable window.
  win.webContents.setWindowOpenHandler(({ frameName }) => ({
    action: 'allow',
    overrideBrowserWindowOptions: {
      width: 720, height: 720, resizable: true,
      title: frameName === 'jacl_map' ? 'Map' : 'JACL'
    }
  }))
  // cgijacl may not be bound yet (or briefly refuse). Retry the game URL rather
  // than probing the port -- a bare probe makes webjacl log a NULL request.
  win.webContents.on('did-fail-load', () => {
    if (gameRetries-- > 0) setTimeout(() => win.loadURL(`http://127.0.0.1:${activePort}/`), 250)
  })
  // On a game page (not the picker), add the "‹ Library" button + apply reading prefs.
  win.webContents.on('did-finish-load', () => {
    if (win.webContents.getURL().startsWith('http://127.0.0.1')) { injectLibraryButton(); applyReading() }
  })
  showPicker()
}

// --- reading prefs (columns + margins -> font size, iPad-style) -----------
const MARGINS = { narrow: 0.04, normal: 0.10, wide: 0.16 }   // matches iOS MarginWidth
const COLS_MIN = 40, COLS_MAX = 110, COLS_STEP = 6
function readingFile () { return path.join(app.getPath('userData'), 'reading.json') }
function loadReading () { try { Object.assign(reading, JSON.parse(fs.readFileSync(readingFile(), 'utf8'))) } catch (e) {} }
function saveReading () { try { fs.writeFileSync(readingFile(), JSON.stringify(reading)) } catch (e) {} }

// A small fixed "‹ Library" button injected into the game page, so returning to
// the bookshelf doesn't depend on finding the menu.
function injectLibraryButton () {
  win.webContents.executeJavaScript(`(function(){
    if(document.getElementById('jaclLibraryBtn'))return;
    var b=document.createElement('button');b.id='jaclLibraryBtn';b.textContent='‹ Library';
    b.style.cssText='position:fixed;top:7px;left:7px;z-index:2147483647;font:13px Georgia,serif;color:#e8e2d6;background:rgba(20,18,15,.72);border:1px solid rgba(255,255,255,.18);border-radius:7px;padding:4px 10px;cursor:pointer';
    b.onmouseenter=function(){b.style.background='rgba(45,40,33,.9)'};
    b.onmouseleave=function(){b.style.background='rgba(20,18,15,.72)'};
    b.onclick=function(){window.jacl&&window.jacl.library&&window.jacl.library()};
    document.body.appendChild(b);
  })();`).catch(() => {})
}

// Size the transcript so `columns` characters fill the usable width (window minus
// margins) -- the same idea as the iPad's applyColumns, but measuring the actual
// proportional char width. The status bar follows for free: the page re-measures
// jaclMeasureStatusCols against the now-padded #statuswin and sends &status_cols=
// next turn. Re-applies on window resize so it rescales live.
function applyReading () {
  if (!win || win.webContents.getURL().startsWith('file://')) return
  const m = MARGINS[reading.margin] ?? 0.10
  win.webContents.executeJavaScript(`(function(){
    window.jaclReading={c:${reading.columns},m:${m}};
    function apply(){
      var r=window.jaclReading;if(!r)return;
      var mt=document.getElementById('maintext');if(!mt)return;
      var w=document.documentElement.clientWidth||window.innerWidth;
      var side=Math.round(w*r.m),usable=Math.max(w-2*side,120);
      var p=document.createElement('span');
      p.style.cssText='position:absolute;visibility:hidden;white-space:pre;font-size:200px;font-family:'+getComputedStyle(mt).fontFamily;
      p.textContent='abcdefghijklmnopqrstuvwxyz abcdefghijklmnopqrstuvwxyz';
      document.body.appendChild(p);
      var cw=p.getBoundingClientRect().width/p.textContent.length/200;
      document.body.removeChild(p);
      var fs=Math.max(9,Math.min(usable/(r.c*cw),48));
      var st=document.getElementById('jaclReadingStyle');
      if(!st){st=document.createElement('style');st.id='jaclReadingStyle';document.head.appendChild(st);}
      // #statuswin is position:absolute with left:0;right:0, so width is auto and
      // its left/right padding is absorbed within the fixed full width (no
      // overflow) -- so it must NOT get box-sizing:border-box, which would pull the
      // 2px vertical padding inside its height, shrink the bar ~4px and open a gap
      // above #main (whose top is calc'd for the content-box height). Only the flow
      // blocks (#maintext, .directions) need border-box.
      var pad='padding-left:'+side+'px !important;padding-right:'+side+'px !important';
      var bb=';box-sizing:border-box !important';
      st.textContent='#maintext{font-size:'+fs.toFixed(2)+'px !important;'+pad+bb+'}#statuswin{'+pad+'}.directions{'+pad+bb+'}';
    }
    if(!window.__jaclReadingHook){window.__jaclReadingHook=true;window.addEventListener('resize',apply);}
    apply();
  })();`).catch(() => {})
}

// --- menu ----------------------------------------------------------------
function buildMenu () {
  const isMac = process.platform === 'darwin'
  const setMargin = (mm) => { reading.margin = mm; saveReading(); applyReading(); buildMenu() }
  const bumpCols = (d) => {
    reading.columns = Math.max(COLS_MIN, Math.min(COLS_MAX, reading.columns + d))
    saveReading(); applyReading()
  }
  Menu.setApplicationMenu(Menu.buildFromTemplate([
    ...(isMac ? [{ role: 'appMenu' }] : []),
    {
      label: 'Game',
      submenu: [
        { label: 'Library', accelerator: 'CmdOrCtrl+L', click: showPicker },
        { type: 'separator' },
        isMac ? { role: 'close' } : { role: 'quit' }
      ]
    },
    {
      label: 'Reading',
      submenu: [
        // Fewer columns = bigger text (matches the iPad's text-size control).
        { label: 'Bigger Text', accelerator: 'CmdOrCtrl+Plus', click: () => bumpCols(-COLS_STEP) },
        { label: 'Smaller Text', accelerator: 'CmdOrCtrl+-', click: () => bumpCols(COLS_STEP) },
        { type: 'separator' },
        {
          label: 'Margins',
          submenu: ['narrow', 'normal', 'wide'].map(mm => ({
            label: mm[0].toUpperCase() + mm.slice(1), type: 'radio',
            checked: reading.margin === mm, click: () => setMargin(mm)
          }))
        }
      ]
    },
    { role: 'editMenu' },
    { role: 'viewMenu' },
    { role: 'windowMenu' }
  ]))
}

// --- ipc -----------------------------------------------------------------
ipcMain.handle('list-games', () => listGames())
ipcMain.handle('play-game', (e, gamePath) => {
  writeMediaManifest(gamePath)
  startServer(gamePath)
  loadGame()
})
ipcMain.handle('show-picker', () => showPicker())

app.whenReady().then(() => {
  resolvePaths()
  loadReading()
  writeConfig()
  buildMenu()
  createWindow()
})

app.on('window-all-closed', () => { if (server) server.kill(); app.quit() })
app.on('quit', () => { if (server) server.kill() })
