import { defineConfig, type Plugin } from 'vite';
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
    },
    async configurePreviewServer(server) {
      const service = new FileSaveService(resolve(root, 'userdata/saves'));
      await service.initialize();
      server.middlewares.use(fileSaveMiddleware(service));
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
