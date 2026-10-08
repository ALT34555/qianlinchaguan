import { defineConfig, type Plugin } from 'vite';
import { ModResourceService, modResourceMiddleware } from './platforms/mods/resources.ts';
import { resolve } from 'node:path';
import { FileSaveService, fileSaveMiddleware } from './platforms/saves/local-saves.ts';

function userdataSaves(): Plugin {
  let root = '';
  return {
    name: 'qianlin-userdata-saves',
    configResolved(config) { root = config.root; },
    async configureServer(server) {
      const service = new FileSaveService(resolve(root, 'userdata/saves'));
      await service.initialize();
      server.middlewares.use(fileSaveMiddleware(service));
      const mods = new ModResourceService(resolve(root, 'userdata/mods'));
      await mods.initialize();
      server.middlewares.use(modResourceMiddleware(mods));
    },
    async configurePreviewServer(server) {
      const service = new FileSaveService(resolve(root, 'userdata/saves'));
      await service.initialize();
      server.middlewares.use(fileSaveMiddleware(service));
      const mods = new ModResourceService(resolve(root, 'userdata/mods'));
      await mods.initialize();
      server.middlewares.use(modResourceMiddleware(mods));
    },
  };
}

export default defineConfig({
  root: '.',
  publicDir: false,
  plugins: [userdataSaves()],
  server: {
    port: 5173,
    fs: { deny: ['.env', '.env.*', '*.{crt,pem}', '**/.git/**', '**/userdata/**', '**/userdata_dev/**'] },
  },
  worker: {
    format: 'es',
  },
  build: {
    outDir: 'dist',
    target: 'es2022',
    chunkSizeWarningLimit: 1024,
  },
});
