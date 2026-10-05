import { createServer } from 'node:http';
import { readFile } from 'node:fs/promises';
import { resolve, sep, extname } from 'node:path';
import { FileSaveService, fileSaveMiddleware } from '../saves/local-saves';

/** Shared save implementation; an ephemeral loopback port belongs to this app. */
export async function startDesktopServer(root: string, directory: string) {
  const service = new FileSaveService(directory); await service.initialize();
  const saves = fileSaveMiddleware(service);
  const types: Record<string, string> = { '.html': 'text/html; charset=utf-8', '.js': 'text/javascript', '.css': 'text/css', '.json': 'application/json', '.png': 'image/png', '.ttf': 'font/ttf', '.svg': 'image/svg+xml', '.ico': 'image/x-icon' };
  const dist = resolve(root, 'dist');
  const server = createServer((request, response) => {
    saves(request, response, () => {
      void (async () => {
        if (request.method !== 'GET' && request.method !== 'HEAD') { response.writeHead(405).end(); return; }
        const path = decodeURIComponent(new URL(request.url ?? '/', 'http://localhost').pathname);
        const target = resolve(dist, '.' + (path === '/' ? '/index.html' : path));
        if (!target.startsWith(dist + sep)) { response.writeHead(403).end(); return; }
        try {
          const body = await readFile(target);
          response.writeHead(200, { 'Content-Type': types[extname(target)] ?? 'application/octet-stream', 'Cache-Control': 'no-cache' });
          response.end(request.method === 'HEAD' ? undefined : body);
        } catch { response.writeHead(404).end(); }
      })().catch(() => { if (!response.headersSent) response.writeHead(400); response.end(); });
    });
  });
  await new Promise<void>((done, reject) => { server.once('error', reject); server.listen(0, '127.0.0.1', done); });
  const address = server.address();
  if (!address || typeof address === 'string') throw new Error('无法启动本地存档服务。');
  return { server, url: `http://127.0.0.1:${address.port}/` };
}
