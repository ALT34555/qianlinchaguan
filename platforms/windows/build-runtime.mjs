import { build } from 'vite';
import { resolve } from 'node:path';
import { pathToFileURL } from 'node:url';

export async function buildRuntime(compat = false) {
  await build({ configFile: false, publicDir: false, ssr: { noExternal: ['fflate'] }, build: {
    target: compat ? 'node16.17' : 'node22',
    ssr: 'platforms/windows/server.ts',
    outDir: `platforms/windows/${compat ? 'runtime-compat' : 'runtime'}`,
    rollupOptions: { output: { entryFileNames: 'server.mjs' } },
  } });
}

if (process.argv[1] && import.meta.url === pathToFileURL(resolve(process.argv[1])).href) {
  await buildRuntime(process.argv.includes('--compat'));
}
