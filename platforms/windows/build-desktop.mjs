import { cp, mkdir, mkdtemp, readFile, writeFile, access } from 'node:fs/promises';
import { resolve } from 'node:path';
import { buildRuntime } from './build-runtime.mjs';
const compat = process.argv.includes('--compat');
const archIndex = process.argv.indexOf('--arch');
const arch = archIndex >= 0 ? process.argv[archIndex + 1] : 'x64';
if (!['x64', 'ia32'].includes(arch)) throw new Error('--arch 仅支持 x64 或 ia32');
if (!compat) {
  try { await access('node_modules/electron/dist/electron.exe'); }
  catch { await import('electron/install.js'); }
}
await buildRuntime(compat);
if (process.argv.includes('--package')) {
  const { packager } = await import('@electron/packager');
  await mkdir(resolve('../../902temp'), { recursive: true });
  const stage = await mkdtemp(resolve('../../902temp/windows-stage-'));
  await cp(compat ? 'dist-compat' : 'dist', resolve(stage, 'dist'), { recursive: true });
  await cp(`platforms/windows/${compat ? 'runtime-compat' : 'runtime'}`, resolve(stage, 'platforms/windows/runtime'), { recursive: true });
  for (const file of ['desktop.cjs', 'settings.cjs', 'preload.cjs', 'game-preload.cjs', 'launcher.html', 'launcher.css', 'launcher-ui.js', 'qianlin.ico']) await cp(`platforms/windows/${file}`, resolve(stage, 'platforms/windows', file));
  // 携带初次启动的字体 mod
  await cp('userdata/mods/fonts', resolve(stage, 'userdata/mods/fonts'), { recursive: true });
  const source = JSON.parse(await readFile('package.json', 'utf8'));
  const electronVersion = compat ? '22.3.27' : JSON.parse(await readFile('node_modules/electron/package.json', 'utf8')).version;
  await writeFile(resolve(stage, 'desktop-runtime.json'), JSON.stringify({ profile: compat ? 'compat' : 'modern', electronVersion, arch }, null, 2));
  await writeFile(resolve(stage, 'package.json'), JSON.stringify({ name: source.name, version: source.version, main: 'platforms/windows/desktop.cjs' }));
  const outIndex = process.argv.indexOf('--out');
  const output = outIndex >= 0 ? process.argv[outIndex + 1] : compat ? 'release/compat' : 'release';
  if (!output) throw new Error('--out 需要指定输出目录');
  console.log(await packager({ dir: stage, out: resolve(output), name: 'QianlinChaguan', executableName: 'QianlinChaguan', platform: 'win32', arch, electronVersion, icon: resolve('platforms/windows/qianlin.ico'), overwrite: true, asar: false }));
}
