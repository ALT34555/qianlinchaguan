import { build } from 'vite';

await build({ configFile: false, publicDir: false, ssr: { noExternal: ['fflate'] }, build: {
  ssr: 'platforms/windows/server.ts', outDir: 'platforms/windows/runtime',
  rollupOptions: { output: { entryFileNames: 'server.mjs' } },
} });
