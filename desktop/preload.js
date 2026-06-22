// Preload — exposes a tiny, safe API to the picker page (contextIsolation on).
const { contextBridge, ipcRenderer } = require('electron')

contextBridge.exposeInMainWorld('jacl', {
  listGames: () => ipcRenderer.invoke('list-games'),
  play: (gamePath) => ipcRenderer.invoke('play-game', gamePath),
  library: () => ipcRenderer.invoke('show-picker')   // back to the bookshelf from a game
})
