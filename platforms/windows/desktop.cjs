const { app, BrowserWindow, Menu, ipcMain, shell, dialog } = require('electron');
const path = require('node:path');
const { pathToFileURL } = require('node:url');
const fs = require('node:fs');
let service, launcher, game;
if (!app.requestSingleInstanceLock()) app.quit();
else {
  app.on('second-instance', () => { const window = game || launcher; window?.restore(); window?.focus(); });
  app.whenReady().then(async () => {
    Menu.setApplicationMenu(null);
    const root = app.getAppPath();
    const saves = app.isPackaged ? path.join(app.getPath('userData'), 'saves') : path.join(root, 'userdata', 'saves');
    const settingsPath = path.join(path.dirname(saves), 'settings.json');
    let settings = null;
    try { settings = fs.readFileSync(settingsPath, 'utf8'); } catch {}
    const { startDesktopServer } = await import(pathToFileURL(path.join(root, 'platforms/windows/runtime/server.mjs')).href);
    service = await startDesktopServer(root, saves);
    const trustedGame = event => event.sender === game?.webContents && event.senderFrame?.url.startsWith(service.url);
    ipcMain.on('read-settings', event => { event.returnValue = trustedGame(event) ? settings : null; });
    ipcMain.on('write-settings', (event, value) => {
      if (!trustedGame(event) || typeof value !== 'string' || value.length > 16384) { event.returnValue = '无效设置请求'; return; }
      try {
        JSON.parse(value); fs.mkdirSync(path.dirname(settingsPath), { recursive: true });
        fs.writeFileSync(settingsPath + '.tmp', value, 'utf8'); fs.renameSync(settingsPath + '.tmp', settingsPath);
        settings = value; event.returnValue = null;
      } catch (error) { event.returnValue = String(error); }
    });
    const options = { width: 1100, height: 760, minWidth: 640, minHeight: 480, icon: path.join(root, 'platforms/windows/qianlin.ico'), backgroundColor: '#172a20', webPreferences: { sandbox: true, contextIsolation: true, nodeIntegration: false } };
    // 启动器用纸色打底（与 launcher.css 同色，避免加载瞬间闪一下深色）；
    // 游戏窗口保持深色，用来衬托天空与地形。
    launcher = new BrowserWindow({ ...options, title: '茜林茶馆', backgroundColor: '#e5d3aa', webPreferences: { ...options.webPreferences, preload: path.join(root, 'platforms/windows/preload.cjs') } });
    const launcherURL = pathToFileURL(path.join(root, 'platforms/windows/launcher.html')).href;
    const trusted = event => event.sender === launcher?.webContents && event.senderFrame?.url === launcherURL;
    ipcMain.handle('start-game', event => {
      if (!trusted(event)) return;
      if (game) { game.focus(); return; }
      game = new BrowserWindow({ ...options, title: '茜林茶馆', webPreferences: { ...options.webPreferences, preload: path.join(root, 'platforms/windows/game-preload.cjs') } });
      game.webContents.setWindowOpenHandler(() => ({ action: 'deny' }));
      game.webContents.on('will-navigate', (event, url) => { if (new URL(url).origin !== new URL(service.url).origin) event.preventDefault(); });
      game.on('closed', () => { game = null; launcher?.show(); });
      const gameURL = new URL(service.url); gameURL.searchParams.set('saveLocation', saves);
      game.loadURL(gameURL.href); launcher.hide();
    });
    ipcMain.handle('open-saves', event => { if (trusted(event)) return shell.openPath(saves); });
    launcher.webContents.setWindowOpenHandler(() => ({ action: 'deny' }));
    launcher.webContents.on('will-navigate', event => event.preventDefault());
    launcher.on('closed', () => { launcher = null; game?.close(); });
    await launcher.loadURL(launcherURL);
  }).catch(error => { dialog.showErrorBox('启动失败', String(error)); app.quit(); });
  app.on('window-all-closed', () => app.quit());
  app.on('before-quit', () => { service?.server.close(); service?.server.closeAllConnections(); });
}
