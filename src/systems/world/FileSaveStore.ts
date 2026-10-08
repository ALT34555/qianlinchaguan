import { type LocalSave, type SaveList, type SaveStore } from './LocalSaveStore';
import { parseWorldSave, type WorldSave } from './WorldSave';
import { decodeSaveArchive } from './SaveArchive';

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
  async list(): Promise<SaveList> { return await this.call() as SaveList; }
  async put(save: WorldSave, name: string, id?: string): Promise<LocalSave> {
    const valid = parseWorldSave(JSON.stringify(save));
    return await this.call('', { save: valid, name, id }) as LocalSave;
  }
  async import(bytes: Uint8Array, name: string): Promise<LocalSave> {
    const item = decodeSaveArchive(bytes);
    return this.put(item.save, item.name || name);
  }
}
