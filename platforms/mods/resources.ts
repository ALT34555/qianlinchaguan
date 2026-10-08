import { mkdir, readdir, readFile, realpath } from 'node:fs/promises';
import { extname, isAbsolute, relative, resolve, sep } from 'node:path';
import type { IncomingMessage, ServerResponse } from 'node:http';
import type { ModCatalog, ModFont, ModResource } from '../../src/modding_api/Resources.ts';

const fontExtensions = new Set(['.ttf', '.otf', '.woff', '.woff2']);
const types: Record<string, string> = {
  '.ttf': 'font/ttf', '.otf': 'font/otf', '.woff': 'font/woff', '.woff2': 'font/woff2',
  '.json': 'application/json', '.png': 'image/png', '.jpg': 'image/jpeg',
  '.webp': 'image/webp', '.svg': 'image/svg+xml', '.glb': 'model/gltf-binary',
  '.gltf': 'model/gltf+json', '.ogg': 'audio/ogg', '.mp3': 'audio/mpeg', '.wav': 'audio/wav',
};

export class ModResourceService {
  readonly directory: string;
  private files = new Map<string, string>();
  private fonts: ModFont[] = [];
  constructor(directory: string) { this.directory = resolve(directory); }

  async initialize(): Promise<void> {
    await mkdir(this.directory, { recursive: true });
    this.files.clear(); this.fonts = [];
    const walk = async (directory: string): Promise<void> => {
      const entries = await readdir(directory, { withFileTypes: true });
      for (const entry of entries.sort((a, b) => a.name.localeCompare(b.name))) {
        if (entry.name.startsWith('.') || entry.isSymbolicLink()) continue;
        const file = resolve(directory, entry.name);
        if (entry.isDirectory()) await walk(file);
        else if (entry.isFile()) this.files.set(relative(this.directory, file).split(sep).join('/'), file);
      }
    };
    await walk(this.directory);
    let definitions: Record<string, { id?: string; label?: string; family?: string }> = {};
    const manifest = this.files.get('fonts/fonts.json');
    if (manifest) {
      try {
        const data = JSON.parse(await readFile(manifest, 'utf8'));
        definitions = Object.fromEntries(data.fonts.map((font: { file: string }) => [font.file, font]));
      } catch (error) { console.warn('字体 mod 清单无效', error); }
    }
    for (const path of this.files.keys()) {
      if (!path.startsWith('fonts/') || !fontExtensions.has(extname(path).toLowerCase())) continue;
      const file = path.slice('fonts/'.length);
      const definition = definitions[file] ?? {};
      const id = definition.id ?? `mod:${file}`;
      const family = definition.family ?? `Mod ${file}`;
      const label = definition.label ?? file.replace(/\.(ttf|otf|woff2?)$/i, '');
      if (typeof id !== 'string' || !/^(song|round|mod:[^\x00-\x1f]{1,200})$/.test(id)
        || typeof family !== 'string' || !/^[^"\\\x00-\x1f]{1,200}$/.test(family)
        || typeof label !== 'string' || this.fonts.some(font => font.id === id)) continue;
      this.fonts.push({ id, family, label, url: this.url(path) });
    }
  }

  private url(path: string): string { return '/api/mods/resources/' + path.split('/').map(encodeURIComponent).join('/'); }
  catalog(): ModCatalog {
    const resources: ModResource[] = [...this.files.keys()].map(path => ({ path, url: this.url(path) }));
    return { resources, fonts: [...this.fonts] };
  }
  async read(path: string): Promise<Buffer | undefined> {
    const file = this.files.get(path);
    if (!file) return undefined;
    const inside = relative(await realpath(this.directory), await realpath(file));
    if (inside.startsWith('..') || isAbsolute(inside)) return undefined;
    return readFile(file);
  }
}

export function modResourceMiddleware(service: ModResourceService) {
  return (request: IncomingMessage, response: ServerResponse, next: () => void): void => {
    const pathname = (request.url ?? '').split('?')[0];
    if (pathname !== '/api/mods' && !pathname.startsWith('/api/mods/')) { next(); return; }
    void (async () => {
      if (request.method !== 'GET' && request.method !== 'HEAD') {
        response.writeHead(405, { Allow: 'GET, HEAD' }).end(); return;
      }
      let body: string | Buffer | undefined;
      let type = 'application/json; charset=utf-8';
      if (pathname === '/api/mods') body = JSON.stringify(service.catalog());
      else if (pathname.startsWith('/api/mods/resources/')) {
        const path = decodeURIComponent(pathname.slice('/api/mods/resources/'.length));
        body = await service.read(path);
        type = types[extname(path).toLowerCase()] ?? 'application/octet-stream';
      }
      if (body === undefined) { response.writeHead(404).end(); return; }
      response.writeHead(200, { 'Content-Type': type, 'Content-Length': Buffer.byteLength(body),
        'Cache-Control': 'no-store', 'X-Content-Type-Options': 'nosniff' });
      response.end(request.method === 'HEAD' ? undefined : body);
    })().catch(error => {
      const status = error instanceof URIError ? 400 : 404;
      if (!response.headersSent) response.writeHead(status);
      response.end();
    });
  };
}
