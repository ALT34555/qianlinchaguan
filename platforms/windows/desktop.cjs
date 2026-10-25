const { app, BrowserWindow, Menu, ipcMain, shell, dialog } = require('electron');
const path = require('node:path');
const { pathToFileURL } = require('node:url');
const fs = require('node:fs');
const { writeDesktopSettings } = require('./settings.cjs');
const smokeTest = process.argv.includes('--desktop-smoke');
const userData = app.commandLine.getSwitchValue('user-data-dir');
if (userData) app.setPath('userData', path.resolve(userData));
let service, launcher, game, startup;
let settleStartup;
if (!app.requestSingleInstanceLock()) app.quit();
else {
  app.on('second-instance', () => { const window = game || launcher; window?.restore(); window?.focus(); });
  app.whenReady().then(async () => {
    Menu.setApplicationMenu(null);
    const root = app.getAppPath();
    const saves = app.isPackaged ? path.join(app.getPath('userData'), 'saves') : path.join(root, 'userdata', 'saves');
    const mods = path.join(path.dirname(saves), 'mods');
    if (app.isPackaged && !fs.existsSync(mods)) fs.cpSync(path.join(root, 'userdata/mods'), mods, { recursive: true });
    const settingsPath = path.join(path.dirname(saves), 'settings.json');
    let settings = null;
    try { settings = fs.readFileSync(settingsPath, 'utf8'); } catch {}
    const { startDesktopServer } = await import(pathToFileURL(path.join(root, 'platforms/windows/runtime/server.mjs')).href);
    service = await startDesktopServer(root, saves);
    const trustedGame = event => event.sender === game?.webContents && event.senderFrame?.url.startsWith(service.url);
    ipcMain.on('game-ready', event => { if (trustedGame(event)) settleStartup?.(); });
    ipcMain.on('game-failed', (event, message) => { if (trustedGame(event)) settleStartup?.(new Error(String(message).slice(0, 2000))); });
    ipcMain.on('read-settings', event => { event.returnValue = trustedGame(event) ? settings : null; });
    ipcMain.on('write-settings', (event, value) => {
      if (!trustedGame(event) || typeof value !== 'string' || value.length > 16384) { event.returnValue = '无效设置请求'; return; }
      try {
        writeDesktopSettings(settingsPath, value);
        settings = value; event.returnValue = null;
      } catch (error) { event.returnValue = String(error); }
    });
    const options = { show: !smokeTest, width: 1100, height: 760, minWidth: 640, minHeight: 480, icon: path.join(root, 'platforms/windows/qianlin.ico'), backgroundColor: '#172a20', webPreferences: { webgl: !app.commandLine.hasSwitch('disable-webgl'), sandbox: true, contextIsolation: true, nodeIntegration: false } };
    // 启动器用纸色，游戏窗口深色
    launcher = new BrowserWindow({ ...options, title: '茜林茶馆', backgroundColor: '#e5d3aa', webPreferences: { ...options.webPreferences, preload: path.join(root, 'platforms/windows/preload.cjs') } });
    const launcherURL = pathToFileURL(path.join(root, 'platforms/windows/launcher.html')).href;
    const trusted = event => event.sender === launcher?.webContents && event.senderFrame?.url === launcherURL;
    ipcMain.handle('start-game', async event => {
      if (!trusted(event)) throw new Error('启动器连接尚未就绪，请重试。');
      if (startup) return startup;
      if (game) { game.focus(); return; }
      game = new BrowserWindow({ ...options, show: false, title: '茜林茶馆', webPreferences: { ...options.webPreferences, preload: path.join(root, 'platforms/windows/game-preload.cjs') } });
      const window = game;
      game.webContents.setWindowOpenHandler(() => ({ action: 'deny' }));
      game.webContents.on('will-navigate', (event, url) => { if (new URL(url).origin !== new URL(service.url).origin) event.preventDefault(); });
      game.webContents.on('render-process-gone', (_event, details) => {
        const message = `游戏进程已退出（${details.reason}），请重新启动游戏。`;
        settleStartup?.(new Error(message));
        launcher?.webContents.send('game-error', message);
        if (!window.isDestroyed()) window.destroy();
      });
      game.on('closed', () => { settleStartup?.(new Error('游戏窗口在启动完成前已关闭。')); if (game === window) game = null; launcher?.show(); });
      const gameURL = new URL(service.url); gameURL.searchParams.set('saveLocation', saves); gameURL.searchParams.set('desktop', '1');
      let timer;
      const ready = new Promise((done, reject) => {
        settleStartup = error => error ? reject(error) : done();
        timer = setTimeout(() => reject(new Error('游戏启动超时，请重试。')), 30000);
      });
      startup = (async () => {
        try {
          await Promise.all([window.loadURL(gameURL.href), ready]);
          if (!smokeTest) window.show(); launcher?.hide();
        } catch (error) {
          if (!window.isDestroyed()) window.destroy();
          throw error;
        } finally { clearTimeout(timer); settleStartup = undefined; startup = undefined; }
      })();
      return startup;
    });
    ipcMain.handle('open-saves', event => { if (trusted(event)) return shell.openPath(saves); });
    launcher.webContents.setWindowOpenHandler(() => ({ action: 'deny' }));
    launcher.webContents.on('will-navigate', event => event.preventDefault());
    launcher.on('closed', () => { launcher = null; game?.close(); });
    await launcher.loadURL(launcherURL);
  }).catch(error => { dialog.showErrorBox('启动失败', String(error)); app.quit(); });
  app.on('window-all-closed', () => app.quit());
  app.on('before-quit', () => { service?.close(); });
}
