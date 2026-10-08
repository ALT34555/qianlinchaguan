import type { StorageLike } from '../../core/GameSettings';
import { parseWorldSave, type WorldSave } from './WorldSave';
import { encodeSaveArchive, decodeSaveArchive, saveFilename } from './SaveArchive';

export const SAVES_KEY = 'qianlin.saves.v1';
export interface LocalSave { id: string; name: string; createdAt: number; updatedAt: number; save: WorldSave }
export interface SaveList { saves: LocalSave[]; unreadable: number; message?: string }
export interface SaveStore {
  readonly locationLabel: string;
  list(): SaveList | Promise<SaveList>;
  put(save: WorldSave, name: string, id?: string): LocalSave | Promise<LocalSave>;
  import(bytes: Uint8Array, name: string): LocalSave | Promise<LocalSave>;
}
export class LocalSaveStore implements SaveStore {
  readonly locationLabel = '浏览器本地存储';
  constructor(private readonly storage: StorageLike) {}
  private records(): unknown[] {
    const raw = this.storage.getItem(SAVES_KEY);
    if (!raw) return [];
    let data: unknown;
    try { data = JSON.parse(raw); }
    catch { throw new Error('本地存档目录无法解析，现有数据已保留。请使用独立 .sav 文件导出或恢复世界。'); }
    if (!Array.isArray(data)) throw new Error('本地存档目录损坏。现有数据已保留，请先导出备份或导入独立存档。');
    return data;
  }
  list(): SaveList {
    const saves: LocalSave[] = [];
    let unreadable = 0;
    for (const raw of this.records()) {
      try {
        const item = raw as LocalSave;
        if (!item || typeof item.id !== 'string' || typeof item.name !== 'string' || !Number.isFinite(item.createdAt) || !Number.isFinite(item.updatedAt)) throw new Error();
        saves.push({ ...item, save: parseWorldSave(JSON.stringify(item.save)) });
      } catch { unreadable++; }
    }
    return { saves: saves.sort((a, b) => b.updatedAt - a.updatedAt), unreadable };
  }
  put(save: WorldSave, name: string, id?: string, now = Date.now()): LocalSave {
    const valid = parseWorldSave(JSON.stringify(save));
    const records = this.records();
    const slot = id ? records.findIndex(item => !!item && (item as LocalSave).id === id) : -1;
    const previous = slot >= 0 ? records[slot] as LocalSave : undefined;
    const item: LocalSave = { id: previous?.id ?? crypto.randomUUID(), name: name.trim().slice(0, 80) || '未命名世界',
      createdAt: previous?.createdAt ?? now, updatedAt: now, save: valid };
    if (slot >= 0) records[slot] = item; else records.push(item);
    try { this.storage.setItem(SAVES_KEY, JSON.stringify(records)); }
    catch { throw new Error('无法写入本地存档，可能是存储空间不足或浏览器禁止存储。请使用“导出存档文件”备份。'); }
    return item;
  }
  import(bytes: Uint8Array, name: string): LocalSave { const item = decodeSaveArchive(bytes); return this.put(item.save, item.name || name); }
}
export function downloadWorldSave(save: WorldSave, name: string, id: string = crypto.randomUUID()): void {
  const now = Date.now();
  const bytes = encodeSaveArchive({ id, name, createdAt: now, updatedAt: now, save });
  const url = URL.createObjectURL(new Blob([new Uint8Array(bytes)], { type: 'application/zip' }));
  const link = document.createElement('a');
  link.href = url;
  link.download = saveFilename(name, id);
  link.click();
  setTimeout(() => URL.revokeObjectURL(url), 1000);
}
