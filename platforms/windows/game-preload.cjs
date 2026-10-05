const { contextBridge, ipcRenderer } = require('electron');
contextBridge.exposeInMainWorld('desktopSettings', {
  read: () => ipcRenderer.sendSync('read-settings'),
  write: value => { const error = ipcRenderer.sendSync('write-settings', value); if (error) throw new Error(error); },
});
