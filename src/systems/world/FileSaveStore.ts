import { type LocalSave, type SaveList, type SaveStore } from './LocalSaveStore';
import { parseWorldSave, type WorldSave } from './WorldSave';
import { decodeArchive } from './ArchiveWorker';
import { WorldDataStore } from './WorldDataStore';

export class FileSaveStore implements SaveStore {
  private readonly request: typeof fetch;
  constructor(request: typeof fetch = fetch,
    private readonly endpoint = '/api/saves', readonly locationLabel = 'userdata/saves') {
    // 无接收者包装，避免 WebIDL 报错
    this.request = (...args: Parameters<typeof fetch>) => request(...args);
  }

  private async call(path = '', body?: unknown): Promise<unknown> {
    let response: Response;
    try {
      response = await this.request(this.endpoint + path, { method: body === undefined ? 'GET' : 'POST',
        headers: body === undefined ? undefined : { 'Content-Type': 'application/json' },
        body: body === undefined ? undefined : JSON.stringify(body), cache: 'no-store' });
    } catch (error) {
      // 附带底层原因
      const reason = error instanceof Error ? `${error.name}: ${error.message}` : String(error);
      throw new Error(`无法连接本地存档服务，请通过本地启动器打开游戏，或先导出存档文件备份。（原因：${reason}）`);
    }
    let data: unknown;
    try { data = await response.json(); }
    catch { throw new Error('当前运行方式不支持 userdata/saves 存档，请使用本地游戏服务，或先导出存档文件。'); }
    if (!response.ok) throw new Error((data as { error?: string })?.error ?? '本地存档操作失败。');
    return data;
  }
  async list(): Promise<SaveList> {
    const list = await this.call() as SaveList;
    if (list.supportsPlayModes !== true) {
      throw new Error('存档服务尚未更新，无法安全读写生存/悠游模式。请先导出当前世界备份，再完全退出游戏和启动器后重新启动。');
    }
    return list;
  }
  async put(save: WorldSave, name: string, id?: string): Promise<LocalSave> {
    const valid = parseWorldSave(JSON.stringify(save));
    await this.list();
    return await this.call('', { save: valid, name, id }) as LocalSave;
  }
  async rename(id:string,name:string):Promise<LocalSave>{return await this.call('',{operation:'rename',id,name}) as LocalSave;}
  async copyToLeisure(id:string,name:string):Promise<LocalSave>{return await this.call('',{operation:'copyLeisure',id,name}) as LocalSave;}
  async import(bytes: Uint8Array, name: string): Promise<LocalSave> {
    const item = await decodeArchive(bytes);
    if(item.worldData){
      item.save.worldKey=crypto.randomUUID();
      const store=new WorldDataStore(item.save.worldKey);
      for(const [key,data] of Object.entries(item.worldData))await store.put(key,data);
    }
    return this.put(item.save, item.name || name);
  }
}
