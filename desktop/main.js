// JACL desktop (Electron) — main process.
//
// Hosts the full web JACL locally: cgijacl has a built-in web server
// (`cgijacl -p <port> <game.j2>`), so we just spawn it and load the URL in a
// BrowserWindow. One cgijacl process serves one game. The map will later open
// in its own BrowserWindow (step 3).

const { app, BrowserWindow } = require('electron')
const { spawn } = require('child_process')
const path = require('path')
const fs = require('fs')
const net = require('net')

const REPO = path.resolve(__dirname, '..')             // the jacl repo root
const RUN = path.join(__dirname, 'run')                // local scratch (gitignored)
const CGIJACL = path.join(REPO, 'bin', 'cgijacl')
// Hardcoded game for the scaffold; a picker comes next.
const GAME = path.join(REPO, 'projects', 'temp', 'grail.j2')
const PORT = 8099

let server = null   // the cgijacl child process

// Write a cgijacl.conf with machine-correct absolute paths.
function writeConfig () {
  fs.mkdirSync(path.join(RUN, 'temp'), { recursive: true })
  fs.mkdirSync(path.join(RUN, 'logs'), { recursive: true })
  const conf = [
    `access_log\t"${path.join(RUN, 'logs', 'access.log')}"`,
    `error_log\t"${path.join(RUN, 'logs', 'error.log')}"`,
    `include\t\t"${path.join(REPO, 'projects', 'include')}/"`,
    `temp\t\t"${path.join(RUN, 'temp')}/"`,
    'cookie_expiry\t20000',
    ''
  ].join('\n')
  fs.writeFileSync(path.join(RUN, 'cgijacl.conf'), conf)
}

// `-p` = server mode; the game file is the last arg; cwd=RUN so it finds
// ./cgijacl.conf.
function startServer () {
  server = spawn(CGIJACL, ['-p', String(PORT), GAME], { cwd: RUN })
  const log = d => process.stdout.write('[cgijacl] ' + d)
  server.stdout.on('data', log)
  server.stderr.on('data', log)
  server.on('exit', code => console.log('[cgijacl] exited', code))
}

function createWindow () {
  const win = new BrowserWindow({ width: 1024, height: 800, title: 'JACL' })
  // The web JACL's "map window" / "map open" command pops the map via
  // window.open('', 'jacl_map', '...resizable=yes') and auto-updates it. Let
  // Electron honour that as a real, resizable BrowserWindow -- that IS our
  // separate, live map window, for free.
  win.webContents.setWindowOpenHandler(({ frameName }) => ({
    action: 'allow',
    overrideBrowserWindowOptions: {
      width: 720, height: 720, resizable: true,
      title: frameName === 'jacl_map' ? 'Map' : 'JACL'
    }
  }))
  const url = `http://127.0.0.1:${PORT}/`
  let tries = 40
  const tryLoad = () => win.loadURL(url)
  // cgijacl may not be bound yet (or briefly refuse). Retry the load rather than
  // probing the port -- a bare probe connection makes webjacl log a NULL request.
  win.webContents.on('did-fail-load', () => { if (tries-- > 0) setTimeout(tryLoad, 250) })
  tryLoad()
}

app.whenReady().then(() => {
  writeConfig()
  startServer()
  setTimeout(createWindow, 600)   // let cgijacl bind first
})

app.on('window-all-closed', () => {
  if (server) server.kill()
  app.quit()
})

app.on('quit', () => { if (server) server.kill() })
