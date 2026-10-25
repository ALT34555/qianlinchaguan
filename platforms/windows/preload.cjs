const { contextBridge, ipcRenderer } = require('electron');
contextBridge.exposeInMainWorld('launcher', {
  start: () => ipcRenderer.invoke('start-game'),
  saves: () => ipcRenderer.invoke('open-saves'),
  onError: callback => ipcRenderer.on('game-error', (_event, message) => callback(message)),
});
