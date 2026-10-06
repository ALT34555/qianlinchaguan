import { LocalSaveStore, type LocalSave, type SaveList, type SaveStore } from './LocalSaveStore';
import { parseWorldSave, type WorldSave } from './WorldSave';

export class FileSaveStore implements SaveStore {
  private migration: Promise<void> | null = null;
  private migrationMessage = '';
  private readonly request: typeof fetch;
  constructor(private readonly legacy?: LocalSaveStore, request: typeof fetch = fetch,
    private readonly endpoint = '/api/saves', readonly locationLabel = 'userdata/saves') {
    // fetch 必须以"无接收者"的方式调用
    // 浏览器会拿本实例当接收者做 WebIDL 品牌检查
    // TypeError: Failed to exe
    // 该异常会被下面的 catch 吞成"无法连接本地
    this.request = (...args: Parameters<typeof fetch>) => request(...args);
  }

  private async call(path = '', body?: unknown): Promise<unknown> {
    let response: Response;
    try {
      response = await this.request(this.endpoint + path, { method: body === undefined ? 'GET' : 'POST',
        headers: body === undefined ? undefined : { 'Content-Type': 'application/json' },
        body: body === undefined ? undefined : JSON.stringify(body), cache: 'no-store' });
    } catch (error) {
      // 附上真实原因
      // 曾导致一个 fetch 调用错误被长期误判为本地
      const reason = error instanceof Error ? `${error.name}: ${error.message}` : String(error);
      throw new Error(`无法连接本地存档服务，请通过本地启动器打开游戏，或先导出存档文件备份。（原因：${reason}）`);
    }
    let data: unknown;
    try { data = await response.json(); }
    catch { throw new Error('当前运行方式不支持 userdata/saves 存档，请使用本地游戏服务，或先导出存档文件。'); }
    if (!response.ok) throw new Error((data as { error?: string })?.error ?? '本地存档操作失败。');
    return data;
  }
  private ensureMigration(): Promise<void> {
    if (!this.migration) this.migration = this.migrate().catch(error => {
      this.migration = null; throw error;
    });
    return this.migration;
  }
  private async migrate(): Promise<void> {
    if (!this.legacy) return;
    let previous: SaveList;
    try { previous = this.legacy.list(); }
    catch { this.migrationMessage = '旧浏览器存档目录无法读取，原数据已保留。'; return; }
    let imported = 0;
    for (const item of previous.saves) {
      const result = await this.call('/migrate', item) as { created: boolean };
      if (result.created) imported++;
    }
    this.migrationMessage = [imported ? `已将 ${imported} 份浏览器存档迁移到 userdata/saves，原记录已保留。` : '',
      previous.unreadable ? `${previous.unreadable} 份旧浏览器存档不可读，原记录已保留。` : ''].filter(Boolean).join(' ');
  }
  async list(): Promise<SaveList> {
    await this.ensureMigration();
    const result = await this.call() as SaveList;
    return { ...result, ...(this.migrationMessage ? { message: this.migrationMessage } : {}) };
  }
  async put(save: WorldSave, name: string, id?: string): Promise<LocalSave> {
    const valid = parseWorldSave(JSON.stringify(save));
    await this.ensureMigration();
    return await this.call('', { save: valid, name, id }) as LocalSave;
  }
  async import(text: string, name: string): Promise<LocalSave> { return this.put(parseWorldSave(text), name); }
}
