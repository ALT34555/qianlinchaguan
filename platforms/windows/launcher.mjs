#!/usr/bin/env node
/** Windows本地启动服务与静态代理 */

import { spawn, spawnSync } from 'node:child_process';
import { createReadStream, existsSync } from 'node:fs';
import { mkdir, mkdtemp, readdir, rm, stat, unlink, writeFile } from 'node:fs/promises';
import { createServer } from 'node:http';
import { createRequire } from 'node:module';
import { tmpdir } from 'node:os';
import { dirname, extname, isAbsolute, join, relative, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

const APP_ID = 'qianlinchaguan-launcher';
/** 产品版本唯一真源：package.json */
const APP_VERSION = createRequire(import.meta.url)('../../package.json').version;
const HERE = dirname(fileURLToPath(import.meta.url));
const PROJECT_ROOT = resolve(HERE, '..', '..');
const DIST_DIR = join(PROJECT_ROOT, 'dist');
const DEFAULT_PORT = 17873;
const DEFAULT_HOST = '127.0.0.1';
const IS_WINDOWS = process.platform === 'win32';

// -----------
// 0. Node 版本守卫
// -----------
const [NODE_MAJOR, NODE_MINOR] = process.versions.node.split('.').map(Number);

function stripMode() {
  // 版本特性支持档位
  if (NODE_MAJOR > 23) return 'native';
  if (NODE_MAJOR === 23) return NODE_MINOR >= 6 ? 'native' : 'flag';
  if (NODE_MAJOR === 22) return NODE_MINOR >= 18 ? 'native' : NODE_MINOR >= 6 ? 'flag' : 'none';
  return 'none';
}

const STRIP = stripMode();
if (STRIP === 'none') {
  console.error(`本启动器需要 Node.js 22.6 或更高版本（当前 v${process.versions.node}）。`);
  console.error('请从 https://nodejs.org/ 安装 Node.js 22 LTS 或更高版本后重试。');
  process.exit(1);
}
if (STRIP === 'flag' && !process.execArgv.includes('--experimental-strip-types')) {
  // 用子进程带上参数重跑自己，父进程原样转发退出码。
  const result = spawnSync(process.execPath,
    ['--experimental-strip-types', fileURLToPath(import.meta.url), ...process.argv.slice(2)],
    { stdio: 'inherit' });
  process.exit(result.status ?? 1);
}

// -----------
// 1. 命令行参数
// -----------
function fail(message) {
  console.error(`\n${message}\n`);
  process.exit(1);
}

function parseArgs(argv) {
  const options = { port: DEFAULT_PORT, host: DEFAULT_HOST, saves: join(PROJECT_ROOT, 'userdata', 'saves'),
    open: true, build: 'auto', check: false };
  for (let index = 0; index < argv.length; index++) {
    const arg = argv[index];
    const value = () => {
      const next = argv[++index];
      if (next === undefined || next.startsWith('--')) fail(`${arg} 缺少参数值。`);
      return next;
    };
    switch (arg) {
      case '--port': {
        const port = Number(value());
        if (!Number.isInteger(port) || port < 0 || port > 65535) fail('--port 必须是 0 ~ 65535 之间的整数。');
        options.port = port;
        break;
      }
      case '--host': options.host = value(); break;
      case '--saves': options.saves = resolve(value()); break;
      case '--no-open': options.open = false; break;
      case '--build': options.build = 'always'; break;
      case '--no-build': options.build = 'never'; break;
      case '--check': options.check = true; options.open = false; break;
      case '--stop': options.stop = true; options.open = false; break;
      case '--help': case '-h': options.help = true; break;
      default: fail(`未知参数：${arg}（用 --help 查看用法）`);
    }
  }
  return options;
}

function help() {
  console.log(`茜林茶馆 · Windows 本地启动器 v${APP_VERSION}

用法：node platforms/windows/launcher.mjs [选项]

  --port <n>     首选端口（默认 ${DEFAULT_PORT}；被占用时自动顺延）
  --host <addr>  监听地址（默认 ${DEFAULT_HOST}，只对本机开放）
  --saves <dir>  存档目录（默认 <工程根>\\userdata\\saves）
  --no-open      不自动打开浏览器
  --build        启动前强制重新构建
  --no-build     跳过构建检查，直接提供现有 dist\\
  --check        自检：跑一轮存档接口往返后退出（使用临时目录）
  --stop         停止正在运行的后台启动器（静默启动后用它收工）
  --help, -h     显示本帮助

说明：本服务只监听回环地址；存档以独立 JSON 文件写入存档目录。
      直接双击 file:// 打开 dist\\index.html 无法本地存档，必须经由本启动器。`);
}

// -----------
// 2. 检查并按需构建
// -----------
const WATCHED = ['src', 'content', 'index.html', 'vite.config.ts', 'tsconfig.json', 'package.json'];

async function newestMtime(target) {
  const info = await stat(target).catch(() => null);
  if (!info) return 0;
  if (!info.isDirectory()) return info.mtimeMs;
  const entries = await readdir(target, { withFileTypes: true }).catch(() => []);
  let newest = 0;
  for (const entry of entries) {
    if (entry.name === 'node_modules' || entry.name.startsWith('.')) continue;
    newest = Math.max(newest, await newestMtime(join(target, entry.name)));
  }
  return newest;
}

function runBuild() {
  console.log('正在构建（npm run build）…\n');
  // Windows 上 .cmd 包装脚本必须走 s
  const result = spawnSync('npm run build', { cwd: PROJECT_ROOT, stdio: 'inherit', shell: true });
  return result.status === 0;
}

async function ensureBuild(options, { allowBuild = true } = {}) {
  const marker = join(DIST_DIR, 'index.html');
  const hasDist = existsSync(marker);
  if (options.build === 'never') {
    if (!hasDist) fail(`未找到构建产物 ${marker}。请先运行 npm run build，或去掉 --no-build 让启动器自动构建。`);
    return;
  }
  let stale = !hasDist;
  let reason = hasDist ? '' : '尚未生成构建产物 dist\\index.html';
  if (hasDist && options.build === 'auto') {
    const distTime = (await stat(marker)).mtimeMs;
    let newest = 0;
    for (const item of WATCHED) newest = Math.max(newest, await newestMtime(join(PROJECT_ROOT, item)));
    if (newest > distTime) { stale = true; reason = '源码比构建产物更新'; }
  }
  if (!stale) return;
  if (!allowBuild) return;
  if (!existsSync(join(PROJECT_ROOT, 'node_modules', 'vite'))) {
    if (hasDist) { console.warn(`提示：${reason}，但缺少 node_modules，无法重建，将直接提供现有 dist\\。\n`); return; }
    fail(`缺少 node_modules，无法构建。请先在 ${PROJECT_ROOT} 运行：npm install`);
  }
  console.log(`检测到${reason}，正在重新构建…（加 --no-build 可跳过）\n`);
  if (!runBuild()) fail('构建失败（请查看上面的 tsc / vite 输出）。未提供任何旧版本产物，以免运行到过期代码。');
}

// -----------
// 3. 挂载本地存档服务
// -----------
async function loadSaveService() {
  try {
    return await import('../../platforms/saves/local-saves.ts');
  } catch (error) {
    fail(`无法加载存档服务 platforms/saves/local-saves.ts：${error instanceof Error ? error.message : String(error)}
请确认 Node.js 版本 >= 22.6（当前 v${process.versions.node}），且 platforms\\saves\\local-saves.ts 未被移动或删除。`);
  }
}

// -----------
// 4. 静态站点
// -----------
const MIME = {
  '.html': 'text/html; charset=utf-8',
  '.js': 'text/javascript; charset=utf-8',
  '.mjs': 'text/javascript; charset=utf-8',
  '.css': 'text/css; charset=utf-8',
  '.json': 'application/json; charset=utf-8',
  '.map': 'application/json; charset=utf-8',
  '.txt': 'text/plain; charset=utf-8',
  '.svg': 'image/svg+xml',
  '.png': 'image/png',
  '.jpg': 'image/jpeg',
  '.jpeg': 'image/jpeg',
  '.webp': 'image/webp',
  '.gif': 'image/gif',
  '.ico': 'image/x-icon',
  '.ttf': 'font/ttf',
  '.otf': 'font/otf',
  '.woff': 'font/woff',
  '.woff2': 'font/woff2',
  '.wasm': 'application/wasm',
  '.mp3': 'audio/mpeg',
  '.ogg': 'audio/ogg',
  '.wav': 'audio/wav',
};

function send(response, status, headers, body) {
  response.writeHead(status, { 'X-Content-Type-Options': 'nosniff', ...headers });
  if (body === undefined) response.end(); else response.end(body);
}

function contentType(file) { return MIME[extname(file).toLowerCase()] ?? 'application/octet-stream'; }

function cacheControl(pathname) {
  // Vite 产物带内容哈希
  if (pathname.startsWith('/assets/')) return 'public, max-age=31536000, immutable';
  if (pathname === '/' || pathname.endsWith('.html')) return 'no-cache, no-store, must-revalidate';
  return 'no-cache';
}

async function sendFile(request, response, file, pathname) {
  const info = await stat(file);
  if (!info.isFile()) return false;
  const etag = `W/"${info.size.toString(16)}-${Math.floor(info.mtimeMs).toString(16)}"`;
  const headers = {
    'Content-Type': contentType(file),
    'Content-Length': String(info.size),
    'Last-Modified': info.mtime.toUTCString(),
    'ETag': etag,
    'Cache-Control': cacheControl(pathname),
  };
  // 304 不得携带实体头（Content-Type
  if (request.headers['if-none-match'] === etag) {
    send(response, 304, { 'ETag': etag, 'Cache-Control': headers['Cache-Control'] });
    return true;
  }
  if (request.method === 'HEAD') { send(response, 200, headers); return true; }
  response.writeHead(200, headers);
  await new Promise((done) => {
    const stream = createReadStream(file);
    stream.on('error', () => { response.destroy(); done(); });
    stream.on('close', done);
    stream.pipe(response);
  });
  return true;
}

function serveStatic(request, response) {
  void (async () => {
    if (request.method !== 'GET' && request.method !== 'HEAD') {
      send(response, 405, { Allow: 'GET, HEAD', 'Content-Type': 'text/plain; charset=utf-8' }, '仅支持 GET / HEAD');
      return;
    }
    let pathname;
    try { pathname = decodeURIComponent(new URL(request.url, 'http://localhost').pathname); }
    catch { send(response, 400, { 'Content-Type': 'text/plain; charset=utf-8' }, '请求路径无效'); return; }

    // 目录穿越防护：解析后必须仍在 dist 内。
    const target = resolve(DIST_DIR, `.${pathname}`);
    const inside = relative(DIST_DIR, target);
    if (inside.startsWith('..') || isAbsolute(inside)) {
      send(response, 403, { 'Content-Type': 'text/plain; charset=utf-8' }, '禁止访问');
      return;
    }
    const indexFile = join(DIST_DIR, 'index.html');
    const candidates = pathname.endsWith('/') ? [join(target, 'index.html')] : [target, join(target, 'index.html')];
    let failure = null;
    for (const candidate of candidates) {
      try { if (await sendFile(request, response, candidate, pathname)) return; }
      catch (error) { if (error?.code !== 'ENOENT') failure = error; }
    }
    if (failure) { send(response, 500, { 'Content-Type': 'text/plain; charset=utf-8' }, '读取文件失败'); return; }
    // 单页应用回退
    if ((request.headers.accept ?? '').includes('text/html')) {
      try { if (await sendFile(request, response, indexFile, '/index.html')) return; }
      catch (error) { if (error?.code !== 'ENOENT') { send(response, 500, { 'Content-Type': 'text/plain; charset=utf-8' }, '读取文件失败'); return; } }
    }
    send(response, 404, { 'Content-Type': 'text/plain; charset=utf-8' }, `未找到 ${pathname}`);
  })();
}

// -----------
// 5. 组装服务
// -----------
function createLauncherServer(saveMiddleware, savesDirectory) {
  return createServer((request, response) => {
    const pathname = (request.url ?? '').split('?')[0];
    if (pathname === '/api/launcher') {
      // 供单实例探测与排障使用。
      send(response, 200, { 'Content-Type': 'application/json; charset=utf-8', 'Cache-Control': 'no-store' },
        JSON.stringify({ app: APP_ID, version: APP_VERSION, pid: process.pid, root: PROJECT_ROOT, saves: savesDirectory }));
      return;
    }
    saveMiddleware(request, response, () => serveStatic(request, response));
  });
}

async function existingLauncher(port, host) {
  try {
    const response = await fetch(`http://${host}:${port}/api/launcher`, { signal: AbortSignal.timeout(700) });
    const data = await response.json();
    return data?.app === APP_ID ? data : null;
  } catch { return null; }
}

function listen(server, host, port) {
  return new Promise((done, reject) => {
    const onError = (error) => { server.removeListener('listening', onListening); reject(error); };
    const onListening = () => { server.removeListener('error', onError); done(server.address().port); };
    server.once('error', onError);
    server.once('listening', onListening);
    server.listen(port, host);
  });
}

/** 停止后台启动器 */
async function stopRunning(options) {
  let stopped = 0;
  for (let port = options.port; port <= options.port + 15; port++) {
    const info = await existingLauncher(port, options.host);
    if (!info) continue;
    try {
      process.kill(info.pid, 'SIGTERM');
      console.log(`已停止端口 ${port} 上的启动器（PID ${info.pid}）。`);
      stopped++;
    } catch (error) {
      console.error(`端口 ${port} 上的启动器（PID ${info.pid}）停止失败：${error instanceof Error ? error.message : String(error)}`);
    }
  }
  if (!stopped) console.log('没有发现正在运行的启动器。');
  else await new Promise(done => setTimeout(done, 500));
  return 0;
}

function openBrowser(url) {
  const [command, args] = IS_WINDOWS ? ['cmd.exe', ['/c', 'start', '', url]]
    : process.platform === 'darwin' ? ['open', [url]] : ['xdg-open', [url]];
  try { spawn(command, args, { detached: true, stdio: 'ignore' }).unref(); }
  catch { console.warn('未能自动打开浏览器，请手动访问上面的地址。'); }
}

/** 打印/打开用的地址 */
function displayHost(host) {
  return host === '0.0.0.0' || host === '::' || host === '' ? DEFAULT_HOST : host;
}

function banner(host, port, savesDirectory) {
  const url = `http://${host}:${port}/`;
  const line = '─'.repeat(58);
  console.log(`\n${line}`);
  console.log('  茜林茶馆 · 本地启动器已就绪');
  console.log(line);
  console.log(`  游戏地址   ${url}`);
  console.log(`  存档目录   ${savesDirectory}`);
  console.log(`  构建产物   ${DIST_DIR}`);
  console.log(line);
  console.log('  存档会以独立 JSON 文件写入上面的存档目录，可在游戏内“读取”界面看到。');
  console.log('  按 Ctrl+C 停止本地服务（或直接关闭本窗口）。\n');
  return url;
}

function shutdown(server) {
  let done = false;
  const close = () => { if (done) return; done = true; console.log('\n正在停止本地服务…'); server.close(() => process.exit(0)); setTimeout(() => process.exit(0), 1500).unref(); };
  process.on('SIGINT', close);
  process.on('SIGTERM', close);
  process.on('SIGHUP', close);
}

// -----------
// 6. 自检（--check）
// -----------
async function runCheck(options, { FileSaveService, fileSaveMiddleware }) {
  const results = [];
  const check = (name, ok, detail = '') => { results.push({ name, ok, detail }); console.log(`  ${ok ? '✓' : '✗'} ${name}${detail ? ` — ${detail}` : ''}`); };

  console.log('茜林茶馆 · 启动器自检\n');
  console.log('[1] 静态资源与构建产物');
  const indexFile = join(DIST_DIR, 'index.html');
  check('dist\\index.html 存在', existsSync(indexFile), indexFile);
  if (existsSync(indexFile)) {
    const assets = join(DIST_DIR, 'assets');
    const files = existsSync(assets) ? await readdir(assets) : [];
    check('dist\\assets 含入口 JS/CSS/Worker', files.some(f => f.endsWith('.js')) && files.some(f => f.endsWith('.css')) && files.some(f => f.includes('worker')), `${files.length} 个文件`);
  }

  console.log('\n[2] 存档目录可写性');
  await mkdir(options.saves, { recursive: true });
  const probe = join(options.saves, '.launcher-write-probe');
  try { await writeFile(probe, 'ok', 'utf8'); await unlink(probe); check('存档目录可写', true, options.saves); }
  catch (error) { check('存档目录可写', false, error instanceof Error ? error.message : String(error)); }

  console.log('\n[3] 存档接口往返（临时目录）');
  const scratch = await mkdtemp(join(tmpdir(), 'qianlin-check-'));
  const service = new FileSaveService(scratch);
  await service.initialize();
  const server = createLauncherServer(fileSaveMiddleware(service), scratch);
  const port = await listen(server, DEFAULT_HOST, 0);
  const base = `http://${DEFAULT_HOST}:${port}`;
  try {
    const home = await fetch(`${base}/`);
    check('GET / 返回入口页面', home.status === 200 && (home.headers.get('content-type') ?? '').includes('text/html'), `HTTP ${home.status}`);
    const identity = await fetch(`${base}/api/launcher`).then(r => r.json());
    check('GET /api/launcher 身份正确', identity.app === APP_ID, `v${identity.version}`);
    const empty = await fetch(`${base}/api/saves`).then(r => r.json());
    check('GET /api/saves 初始为空', Array.isArray(empty.saves) && empty.saves.length === 0);

    const { GENERATOR_VERSION } = await import('../../src/core/version.ts');
    const { DEFAULT_CLIMATE_WEIGHTS, DEFAULT_GENERATION } = await import('../../src/systems/world/WorldSettings.ts');
    const makeSave = (name) => ({ generatorVersion: GENERATOR_VERSION, seed: 20261004,
      climateWeights: DEFAULT_CLIMATE_WEIGHTS, generation: DEFAULT_GENERATION, calendarType: 'real',
      unixMs: Date.now(), utcOffsetMinutes: 480, worldName: name,
      player: { x: 32.5, y: 12, z: -40.25, yaw: 1.57, pitch: -0.2 } });
    const post = (path, body) => fetch(`${base}${path}`, { method: 'POST',
      headers: { 'Content-Type': 'application/json' }, body: JSON.stringify(body) }).then(async r => ({ status: r.status, data: await r.json() }));

    const created = await post('/api/saves', { save: makeSave('启动器自检'), name: '启动器自检' });
    check('POST /api/saves 新建存档', created.status === 200 && !!created.data.id, `HTTP ${created.status}`);
    const updated = await post('/api/saves', { save: makeSave('启动器自检'), name: '启动器自检(改名)', id: created.data.id });
    check('POST /api/saves 覆盖同一条存档', updated.status === 200 && updated.data.id === created.data.id);
    const listed = await fetch(`${base}/api/saves`).then(r => r.json());
    check('GET /api/saves 读回 1 条且名称已更新', listed.saves.length === 1 && listed.saves[0].name === '启动器自检(改名)', listed.saves[0]?.name ?? '');
    const onDisk = (await readdir(scratch)).filter(f => f.endsWith('.json'));
    check('存档已落盘为独立 JSON 文件', onDisk.length === 1, onDisk.join(', '));
    const migrated = await post('/api/saves/migrate', { id: 'legacy-check', name: '旧浏览器存档', createdAt: 1, updatedAt: 2, save: makeSave('旧浏览器存档') });
    check('POST /api/saves/migrate 迁移旧存档', migrated.status === 200 && migrated.data.created === true);
    const bad = await post('/api/saves', { save: { seed: 'x' }, name: '非法存档' });
    check('非法存档被拒绝', bad.status >= 400, bad.data.error ?? '');
  } finally {
    await new Promise(done => server.close(done));
    await rm(scratch, { recursive: true, force: true });
  }

  const failed = results.filter(item => !item.ok);
  console.log(`\n${'─'.repeat(58)}`);
  console.log(`自检结果：通过 ${results.length - failed.length} / ${results.length}${failed.length ? `，失败 ${failed.length} 项` : '，全部通过'}`);
  return failed.length === 0 ? 0 : 1;
}

// -----------
// 7. 入口
// -----------
async function main() {
  const options = parseArgs(process.argv.slice(2));
  if (options.help) { help(); return 0; }
  if (options.stop) return await stopRunning(options);

  const { FileSaveService, fileSaveMiddleware } = await loadSaveService();

  if (options.check) {
    await ensureBuild(options, { allowBuild: options.build !== 'never' });
    return await runCheck(options, { FileSaveService, fileSaveMiddleware });
  }

  await ensureBuild(options);

  const running = await existingLauncher(options.port, options.host);
  if (running) {
    console.log(`检测到启动器已在运行（端口 ${options.port}）。直接打开游戏页面。`);
    if (options.open) openBrowser(`http://${displayHost(options.host)}:${options.port}/`);
    return 0;
  }

  const service = new FileSaveService(options.saves);
  await service.initialize();
  const server = createLauncherServer(fileSaveMiddleware(service), service.directory);

  let port;
  try { port = await listen(server, options.host, options.port); }
  catch (error) {
    if (error?.code !== 'EADDRINUSE') throw error;
    console.log(`端口 ${options.port} 已被其它程序占用，正在寻找可用端口…`);
    let bound = null;
    for (let candidate = options.port + 1; candidate <= options.port + 15 && bound === null; candidate++) {
      try { bound = await listen(server, options.host, candidate); } catch (inner) { if (inner?.code !== 'EADDRINUSE') throw inner; }
    }
    if (bound === null) bound = await listen(server, options.host, 0);
    port = bound;
  }

  const url = banner(displayHost(options.host), port, service.directory);
  shutdown(server);
  if (options.open) openBrowser(url);
  return await new Promise(() => {}); // 保持前台运行，直到 Ctrl+C / 关闭窗口
}

main().then(code => { if (typeof code === 'number') process.exit(code); }).catch(error => {
  console.error(`\n启动器发生错误：${error instanceof Error ? error.stack ?? error.message : String(error)}\n`);
  process.exit(1);
});
