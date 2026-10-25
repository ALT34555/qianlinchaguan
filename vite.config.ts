import { defineConfig, type Plugin } from 'vite';
import { ModResourceService, modResourceMiddleware } from './platforms/mods/resources.ts';
import { resolve } from 'node:path';

function userdataSaves(): Plugin {
  return {
    name: 'qianlin-userdata-saves',
    configureServer(server) {
      const dataDir = resolve(process.cwd(), 'userdata');
      const modService = new ModResourceService(dataDir);
      server.middlewares.use(modResourceMiddleware(modService));
    },
  };
}

export default defineConfig(({ mode }) => ({
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
    outDir: mode === 'desktop-compat' ? 'dist-compat' : 'dist',
    target: mode === 'desktop-compat' ? 'chrome108' : 'es2022',
    cssTarget: mode === 'desktop-compat' ? 'chrome108' : undefined,
    chunkSizeWarningLimit: 1024,
  },
}));
