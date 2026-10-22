const { contextBridge, ipcRenderer } = require('electron');
contextBridge.exposeInMainWorld('desktopRuntime', {
  ready: () => ipcRenderer.send('game-ready'),
  failed: message => ipcRenderer.send('game-failed', String(message).slice(0, 2000)),
});
contextBridge.exposeInMainWorld('desktopSettings', {
  read: () => ipcRenderer.sendSync('read-settings'),
  write: value => { const error = ipcRenderer.sendSync('write-settings', value); if (error) throw new Error(error); },
});
