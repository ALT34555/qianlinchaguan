import type { StorageLike } from '../../core/StorageLike';
import { parseWorldSave, type WorldSave } from './WorldSave';
import { decodeSaveArchive, saveFilename } from './SaveArchive';
import { WorldDataStore } from './WorldDataStore';
import { encodeArchive } from './ArchiveWorker';

export const SAVES_KEY = 'qianlin.saves.v1';
export interface LocalSave { id: string; name: string; createdAt: number; updatedAt: number; save: WorldSave;worldData?:Record<string,Uint8Array> }
export interface SaveList { saves: LocalSave[]; unreadable: number; message?: string; supportsPlayModes?: boolean }
export interface SaveStore {
  readonly locationLabel: string;
  list(): SaveList | Promise<SaveList>;
  put(save: WorldSave, name: string, id?: string): LocalSave | Promise<LocalSave>;
  import(bytes: Uint8Array, name: string): LocalSave | Promise<LocalSave>;
  copyToLeisure(id:string,name:string):Promise<LocalSave>;
  rename(id:string,name:string):LocalSave|Promise<LocalSave>;
}
export async function leisureCopy(save:WorldSave):Promise<WorldSave>{
  const copy=parseWorldSave(JSON.stringify({...save,playMode:'leisure'}));
  if(save.worldKey){copy.worldKey=crypto.randomUUID();await new WorldDataStore(save.worldKey).copyTo(copy.worldKey);}
  return copy;
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
    return { saves: saves.sort((a, b) => b.updatedAt - a.updatedAt), unreadable, supportsPlayModes: true };
  }
  put(save: WorldSave, name: string, id?: string, now = Date.now()): LocalSave {
    const valid = parseWorldSave(JSON.stringify(save));
    const records = this.records();
    const slot = id ? records.findIndex(item => !!item && (item as LocalSave).id === id) : -1;
    const previous = slot >= 0 ? records[slot] as LocalSave : undefined;
    const item: LocalSave = { id: previous?.id ?? crypto.randomUUID(), name: name.trim().slice(0, 80) || '未命名世界',
      createdAt: previous?.createdAt ?? now, updatedAt: now, save: { ...valid, worldName: name.trim().slice(0,80) || '未命名世界' } };
    if (slot >= 0) records[slot] = item; else records.push(item);
    try { this.storage.setItem(SAVES_KEY, JSON.stringify(records)); }
    catch { throw new Error('无法写入本地存档，可能是存储空间不足或浏览器禁止存储。请使用“导出存档文件”备份。'); }
    return item;
  }
  import(bytes: Uint8Array, name: string): LocalSave { const item = decodeSaveArchive(bytes); return this.put(item.save, item.name || name); }
  rename(id:string,name:string):LocalSave {
    const item=this.list().saves.find(item=>item.id===id);if(!item)throw new Error('原存档不存在');
    if(!name.trim())throw new Error('存档名称不能为空');
    return this.put(item.save,name,id);
  }
  async copyToLeisure(id:string,name:string):Promise<LocalSave>{
    const item=this.list().saves.find(item=>item.id===id);if(!item)throw new Error('原存档不存在');
    return this.put(await leisureCopy(item.save),name);
  }
}
export async function downloadWorldSave(save: WorldSave, name: string, id: string = crypto.randomUUID()): Promise<void> {
  const now = Date.now();
  const worldData=save.worldKey?await new WorldDataStore(save.worldKey).entries():{};
  const bytes = await encodeArchive({ id, name, createdAt: now, updatedAt: now, save },worldData);
  const url = URL.createObjectURL(new Blob([new Uint8Array(bytes)], { type: 'application/zip' }));
  const link = document.createElement('a');
  link.href = url;
  link.download = saveFilename(name, id);
  link.click();
  setTimeout(() => URL.revokeObjectURL(url), 1000);
}
