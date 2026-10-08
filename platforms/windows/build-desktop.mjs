import { build } from 'vite';
import { cp, mkdir, mkdtemp, readFile, writeFile, access } from 'node:fs/promises';
import { resolve } from 'node:path';
try { await access('node_modules/electron/dist/electron.exe'); }
catch { await import('electron/install.js'); }
await build({ configFile: false, publicDir: false, ssr: { noExternal: ['fflate'] }, build: { ssr: 'platforms/windows/server.ts', outDir: 'platforms/windows/runtime', rollupOptions: { output: { entryFileNames: 'server.mjs' } } } });
if (process.argv.includes('--package')) {
  const { packager } = await import('@electron/packager');
  await mkdir(resolve('../../902temp'), { recursive: true });
  const stage = await mkdtemp(resolve('../../902temp/windows-stage-'));
  for (const source of ['dist', 'platforms/windows/runtime']) await cp(source, resolve(stage, source), { recursive: true });
  for (const file of ['desktop.cjs', 'preload.cjs', 'game-preload.cjs', 'launcher.html', 'launcher.css', 'launcher-ui.js', 'qianlin.ico']) await cp(`platforms/windows/${file}`, resolve(stage, 'platforms/windows', file));
  // 携带初次启动的字体 mod
  await cp('userdata/mods/fonts', resolve(stage, 'userdata/mods/fonts'), { recursive: true });
  const source = JSON.parse(await readFile('package.json', 'utf8'));
  await writeFile(resolve(stage, 'package.json'), JSON.stringify({ name: source.name, version: source.version, main: 'platforms/windows/desktop.cjs' }));
  const outIndex = process.argv.indexOf('--out');
  const output = outIndex >= 0 ? process.argv[outIndex + 1] : 'release';
  if (!output) throw new Error('--out 需要指定输出目录');
  console.log(await packager({ dir: stage, out: resolve(output), name: 'QianlinChaguan', executableName: 'QianlinChaguan', platform: 'win32', arch: 'x64', electronVersion: JSON.parse(await readFile('node_modules/electron/package.json', 'utf8')).version, icon: resolve('platforms/windows/qianlin.ico'), overwrite: true, asar: false }));
}
