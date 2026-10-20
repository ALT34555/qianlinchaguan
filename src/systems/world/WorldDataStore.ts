const DATABASE = 'qianlin-world-data';
const CACHE_BUDGET = 256 * 1024 * 1024;
let database: Promise<IDBDatabase> | undefined;
let backend: 'file' | 'browser' | undefined;

function openDatabase(): Promise<IDBDatabase> {
  return database ??= new Promise((resolve,reject)=>{
    const request = indexedDB.open(DATABASE,1);
    request.onupgradeneeded = () => request.result.createObjectStore('entries',{keyPath:'id'});
    request.onsuccess = () => resolve(request.result);
    request.onerror = () => reject(request.error);
  });
}

async function fileBackend(): Promise<boolean> {
  if (backend) return backend === 'file';
  try {
    const response = await fetch('/api/world-data',{cache:'no-store'});
    backend = response.ok && (await response.json()).worldData === true ? 'file' : 'browser';
  } catch { backend = 'browser'; }
  return backend === 'file';
}

export function validWorldKey(key: string): boolean { return /^[A-Za-z0-9_-]{1,80}$/.test(key); }
export function validDataKey(key: string): boolean { return /^(hydrology|spawn|weather[a-z]{8}|chunk_-?\d+_-?\d+|preview_-?\d+_-?\d+|atlas_node_-?\d+_-?\d+|exploration)$/.test(key); }

export class WorldDataStore {
  private writes=0;
  readonly worldKey: string;
  constructor(worldKey: string) {
    this.worldKey=worldKey;
    if (!validWorldKey(worldKey)) throw new Error('世界缓存编号无效');
  }
  private url(key: string): string {
    if (!validDataKey(key)) throw new Error('区块缓存键无效');
    return `/api/world-data/${this.worldKey}/${key}`;
  }
  async get(key: string): Promise<Uint8Array<ArrayBuffer> | null> {
    const url = this.url(key);
    if (await fileBackend()) {
      const response = await fetch(url,{cache:'no-store'});
      if (response.status === 404) return null;
      if (!response.ok) throw new Error('读取区块缓存失败');
      return new Uint8Array(await response.arrayBuffer());
    }
    const db = await openDatabase();
    return new Promise((resolve,reject)=>{
      const request = db.transaction('entries').objectStore('entries').get(`${this.worldKey}/${key}`);
      request.onsuccess = () => resolve(request.result ? new Uint8Array(request.result.bytes) : null);
      request.onerror = () => reject(request.error);
    });
  }
  async put(key: string,bytes: Uint8Array): Promise<void> {
    const url = this.url(key);
    if (await fileBackend()) {
      const response = await fetch(url,{method:'PUT',body:new Uint8Array(bytes),headers:{'Content-Type':'application/octet-stream'}});
      if (!response.ok) throw new Error('写入区块缓存失败');
      return;
    }
    const db = await openDatabase();
    const previous = await this.get(key);
    if (!previous && key.startsWith('chunk_') && ++this.writes % 64 === 0) {
      const records = await new Promise<{id:string;size:number;updatedAt:number}[]>((resolve,reject)=>{
        const metadata:{id:string;size:number;updatedAt:number}[]=[];
        const request = db.transaction('entries').objectStore('entries').openCursor();
        request.onsuccess = () => {
          const cursor=request.result;if(!cursor){resolve(metadata);return;}
          const value=cursor.value;metadata.push({id:value.id,size:value.bytes.byteLength,updatedAt:value.updatedAt});cursor.continue();
        };request.onerror = () => reject(request.error);
      });
      let total = records.reduce((n,r)=>n+r.size,bytes.byteLength);
      if (total > CACHE_BUDGET) {
        const tx = db.transaction('entries','readwrite'), store = tx.objectStore('entries');
        for (const record of records.sort((a,b)=>a.updatedAt-b.updatedAt)) {
          if (total <= CACHE_BUDGET) break;
          if (!record.id.includes('/chunk_') && !record.id.includes('/preview_')) continue;
          store.delete(record.id); total -= record.size;
        }
        await new Promise<void>((resolve,reject)=>{tx.oncomplete=()=>resolve();tx.onabort=tx.onerror=()=>reject(tx.error);});
      }
    }
    await new Promise<void>((resolve,reject)=>{
      const tx = db.transaction('entries','readwrite');
      tx.objectStore('entries').put({id:`${this.worldKey}/${key}`,bytes:new Uint8Array(bytes).buffer,updatedAt:Date.now()});
      tx.oncomplete=()=>resolve(); tx.onabort=tx.onerror=()=>reject(tx.error);
    });
  }
  async keys():Promise<string[]>{
    if(await fileBackend()){
      const response=await fetch(`/api/world-data/${this.worldKey}`,{cache:'no-store'});
      if(!response.ok)throw new Error('读取世界目录失败');
      return (await response.json() as string[]).filter(validDataKey);
    }
    const db=await openDatabase();
    return new Promise((resolve,reject)=>{
      const request=db.transaction('entries').objectStore('entries').getAllKeys(IDBKeyRange.bound(`${this.worldKey}/`,`${this.worldKey}/\uffff`));
      request.onsuccess=()=>resolve(request.result.map(key=>String(key).slice(this.worldKey.length+1)).filter(validDataKey));
      request.onerror=()=>reject(request.error);
    });
  }
  async copyTo(worldKey:string):Promise<void>{
    if(worldKey===this.worldKey)throw new Error('副本不能使用原世界编号');
    const target=new WorldDataStore(worldKey);
    for(const key of await this.keys()){
      const bytes=await this.get(key);if(!bytes)throw new Error('复制期间世界数据已改变');
      await target.put(key,bytes);
    }
  }
  async entries():Promise<Record<string,Uint8Array>>{
    if(await fileBackend()){
      const response=await fetch(`/api/world-data/${this.worldKey}`,{cache:'no-store'});
      if(!response.ok)throw new Error('读取世界目录失败');
      const keys=await response.json() as string[],out:Record<string,Uint8Array>={};
      let total=0;
      for(const key of keys){const bytes=await this.get(key);if(bytes){total+=bytes.length;if(total>CACHE_BUDGET+64*1024*1024)throw new Error('导出世界过大');out[key]=bytes;}}
      return out;
    }
    const db=await openDatabase();
    return new Promise((resolve,reject)=>{
      const request=db.transaction('entries').objectStore('entries').getAll(IDBKeyRange.bound(`${this.worldKey}/`,`${this.worldKey}/\uffff`));
      request.onsuccess=()=>resolve(Object.fromEntries(request.result.map((r:{id:string;bytes:ArrayBuffer})=>[r.id.split('/')[1],new Uint8Array(r.bytes)])));
      request.onerror=()=>reject(request.error);
    });
  }
}
