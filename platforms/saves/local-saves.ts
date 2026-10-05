/// <reference types="node" />
/**
 * Node 本地存档适配层：
 * 用于 dev / preview 服务及 Windows 本地启动器，
 * 浏览器通过 /api/saves 接口与本地文件系统交互。
 */
import { mkdir, readdir, readFile, lstat, open, rename, unlink } from 'node:fs/promises';
import { basename, join, resolve } from 'node:path';
import { createHash, randomUUID } from 'node:crypto';
import type { IncomingMessage, ServerResponse } from 'node:http';
import { parseWorldSave, type WorldSave } from '../../src/systems/world/WorldSave.ts';
import type { LocalSave, SaveList } from '../../src/systems/world/LocalSaveStore.ts';

const MAX_BYTES = 4 * 1024 * 1024;
const VALID_ID = /^[A-Za-z0-9][A-Za-z0-9_-]{0,79}$/;
/** 注意：本文件会被 platforms/windows/launcher.mjs 直接以 Node 类型擦除方式加载，
 *  因此这里不使用"构造函数参数属性"（parameter property）等需要真正转译的 TS 语法。 */
class SaveError extends Error {
  readonly status: number;
  constructor(message: string, status = 400) { super(message); this.status = status; }
}
interface DiskEntry { filename: string; item: LocalSave }
function fileId(filename: string): string {
  const stem = filename.replace(/\.json$/i, '');
  return VALID_ID.test(stem) ? stem : `file-${createHash('sha256').update(filename).digest('hex').slice(0, 40)}`;
}

export class FileSaveService {
  readonly directory: string;
  private queue: Promise<unknown> = Promise.resolve();
  constructor(directory: string) { this.directory = resolve(directory); }
  async initialize(): Promise<void> { await mkdir(this.directory, { recursive: true }); }
  private async scan(): Promise<{ entries: DiskEntry[]; unreadable: number }> {
    await this.initialize();
    const entries: DiskEntry[] = [];
    let unreadable = 0;
    for (const filename of await readdir(this.directory)) {
      if (!/\.json$/i.test(filename)) continue;
      try {
        const path = join(this.directory, filename), stat = await lstat(path);
        if (!stat.isFile() || stat.isSymbolicLink() || stat.size > MAX_BYTES) throw new Error('不可读的存档文件');
        const text = await readFile(path, 'utf8');
        const save = parseWorldSave(text);
        const data = JSON.parse(text) as { _localSave?: { version: number; createdAt: number; updatedAt: number } };
        const metadata = data._localSave;
        if (metadata && (metadata.version !== 1 || !Number.isFinite(metadata.createdAt) || !Number.isFinite(metadata.updatedAt))) throw new Error('存档元数据无效');
        entries.push({ filename, item: { id: fileId(filename), name: save.worldName || basename(filename).replace(/\.json$/i, '').slice(0, 80),
          createdAt: metadata?.createdAt ?? stat.birthtimeMs, updatedAt: metadata?.updatedAt ?? stat.mtimeMs, save } });
      } catch { unreadable++; }
    }
    return { entries, unreadable };
  }
  async list(): Promise<SaveList> {
    const { entries, unreadable } = await this.scan();
    return { saves: entries.map(entry => entry.item).sort((a, b) => b.updatedAt - a.updatedAt), unreadable };
  }
  private serialized<T>(action: () => Promise<T>): Promise<T> {
    const result = this.queue.then(action);
    this.queue = result.catch(() => {});
    return result;
  }
  private async write(filename: string, item: LocalSave): Promise<void> {
    const destination = join(this.directory, filename);
    const temporary = join(this.directory, `.${filename}.${randomUUID()}.tmp`);
    try {
      const file = await open(temporary, 'wx');
      try {
        await file.writeFile(JSON.stringify({ ...item.save, _localSave: { version: 1, createdAt: item.createdAt, updatedAt: item.updatedAt } }, null, 2), 'utf8');
        await file.sync();
      } finally { await file.close(); }
      await rename(temporary, destination);
    } finally { await unlink(temporary).catch(() => {}); }
  }
  async put(save: WorldSave, name: string, id?: string): Promise<LocalSave> {
    const valid = parseWorldSave(JSON.stringify(save));
    if (typeof name !== 'string') throw new SaveError('存档名称无效。');
    if (id !== undefined && (typeof id !== 'string' || !VALID_ID.test(id))) throw new SaveError('存档编号无效。');
    return this.serialized(async () => {
      const { entries } = await this.scan();
      const previous = id ? entries.find(entry => entry.item.id === id) : undefined;
      if (id && !previous) throw new SaveError('原存档已移动、删除或无法读取，请导出当前世界备份后重新导入。', 404);
      const nextId = previous?.item.id ?? randomUUID();
      const nextName = name.trim().slice(0, 80) || '未命名世界';
      const now = Date.now();
      const item: LocalSave = { id: nextId, name: nextName, createdAt: previous?.item.createdAt ?? now, updatedAt: now,
        save: { ...valid, worldName: nextName } };
      await this.write(previous?.filename ?? `${nextId}.json`, item);
      return item;
    });
  }
  async migrate(legacy: LocalSave): Promise<{ item: LocalSave; created: boolean }> {
    if (!legacy || typeof legacy.id !== 'string' || !legacy.id || legacy.id.length > 200 || typeof legacy.name !== 'string' ||
      !Number.isFinite(legacy.createdAt) || !Number.isFinite(legacy.updatedAt)) throw new SaveError('旧浏览器存档元数据无效。');
    const save = parseWorldSave(JSON.stringify(legacy.save));
    const id = `legacy-${createHash('sha256').update(legacy.id).digest('hex').slice(0, 40)}`;
    return this.serialized(async () => {
      const { entries } = await this.scan();
      const existing = entries.find(entry => entry.item.id === id);
      if (existing) return { item: existing.item, created: false };
      // 迁移目标存在但已损坏时保留原文件，不能用旧浏览器数据悄悄覆盖。
      const filename = `${id}.json`;
      try { await lstat(join(this.directory, filename)); throw new SaveError('已有迁移存档无法读取，原文件已保留。'); }
      catch (error) { if ((error as NodeJS.ErrnoException).code !== 'ENOENT') throw error; }
      const name = legacy.name.trim().slice(0, 80) || '未命名世界';
      const item = { ...legacy, id, name, save: { ...save, worldName: name } };
      await this.write(filename, item);
      return { item, created: true };
    });
  }
}

async function readBody(request: IncomingMessage): Promise<Record<string, unknown>> {
  if (!request.headers['content-type']?.startsWith('application/json')) throw new SaveError('请使用 JSON 存档请求。', 415);
  if (Number(request.headers['content-length']) > MAX_BYTES) { request.resume(); throw new SaveError('存档请求过大。', 413); }
  const chunks: Buffer[] = []; let size = 0;
  for await (const chunk of request) {
    const buffer = Buffer.isBuffer(chunk) ? chunk : Buffer.from(chunk);
    size += buffer.length;
    if (size > MAX_BYTES) throw new SaveError('存档请求过大。', 413);
    chunks.push(buffer);
  }
  let data: unknown;
  try { data = JSON.parse(Buffer.concat(chunks).toString('utf8')); }
  catch { throw new SaveError('存档请求不是有效的 JSON。'); }
  if (!data || typeof data !== 'object' || Array.isArray(data)) throw new SaveError('存档请求格式无效。');
  return data as Record<string, unknown>;
}
export function fileSaveMiddleware(service: FileSaveService) {
  return (request: IncomingMessage, response: ServerResponse, next: () => void): void => {
    const path = request.url?.split('?')[0];
    if (path !== '/api/saves' && path !== '/api/saves/migrate') { next(); return; }
    const send = (status: number, data: unknown) => {
      response.statusCode = status; response.setHeader('Content-Type', 'application/json; charset=utf-8');
      response.setHeader('Cache-Control', 'no-store'); response.setHeader('X-Content-Type-Options', 'nosniff');
      response.end(JSON.stringify(data));
    };
    void (async () => {
      const remote = request.socket.remoteAddress;
      if (!remote || !['127.0.0.1', '::1', '::ffff:127.0.0.1'].includes(remote)) throw new SaveError('存档服务仅接受本机访问。', 403);
      const origin = request.headers.origin;
      if ((origin && new URL(origin).host !== request.headers.host) || request.headers['sec-fetch-site'] === 'cross-site') throw new SaveError('不接受其他网站的存档请求。', 403);
      if (request.method === 'GET' && path === '/api/saves') { send(200, await service.list()); return; }
      if (request.method !== 'POST') { response.setHeader('Allow', 'GET, POST'); throw new SaveError('不支持此存档操作。', 405); }
      const data = await readBody(request);
      if (path === '/api/saves/migrate') send(200, await service.migrate(data as unknown as LocalSave));
      else send(200, await service.put(data.save as WorldSave, data.name as string, data.id as string | undefined));
    })().catch(error => send(error instanceof SaveError ? error.status : 400, {
      error: (error as NodeJS.ErrnoException).code ? '无法读写 userdata/saves，请检查目录权限和磁盘空间，或先导出存档文件。' : error instanceof Error ? error.message : '本地存档操作失败。',
    }));
  };
}
