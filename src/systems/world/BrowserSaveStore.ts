import type { SaveStore, SaveList, LocalSave } from './LocalSaveStore';
import { parseWorldSave, type WorldSave } from './WorldSave';
import { decodeArchive } from './ArchiveWorker';
import { WorldDataStore } from './WorldDataStore';

export class BrowserSaveStore implements SaveStore {
  readonly locationLabel='浏览器世界数据库';
  private db?:Promise<IDBDatabase>;
  private open():Promise<IDBDatabase>{
    return this.db??=new Promise((resolve,reject)=>{
      const request=indexedDB.open('qianlin-save-manifests',1);
      request.onupgradeneeded=()=>request.result.createObjectStore('saves',{keyPath:'id'});
      request.onsuccess=()=>resolve(request.result);request.onerror=()=>reject(request.error);
    });
  }
  async list():Promise<SaveList>{
    const db=await this.open();
    const raw=await new Promise<LocalSave[]>((resolve,reject)=>{
      const request=db.transaction('saves').objectStore('saves').getAll();
      request.onsuccess=()=>resolve(request.result);request.onerror=()=>reject(request.error);
    });
    const saves:LocalSave[]=[];let unreadable=0;
    for(const item of raw){try{saves.push({...item,save:parseWorldSave(JSON.stringify(item.save))});}catch{unreadable++;}}
    return{saves:saves.sort((a,b)=>b.updatedAt-a.updatedAt),unreadable,supportsPlayModes:true};
  }
  async put(save:WorldSave,name:string,id?:string):Promise<LocalSave>{
    const valid=parseWorldSave(JSON.stringify(save)),db=await this.open();
    const old=id?(await this.list()).saves.find(s=>s.id===id):undefined;
    if(id&&!old)throw new Error('原存档不存在');
    const now=Date.now(),item:LocalSave={id:id??crypto.randomUUID(),name:name.trim().slice(0,80)||'未命名世界',createdAt:old?.createdAt??now,updatedAt:now,save:valid};
    await new Promise<void>((resolve,reject)=>{
      const tx=db.transaction('saves','readwrite');tx.objectStore('saves').put(item);
      tx.oncomplete=()=>resolve();tx.onabort=tx.onerror=()=>reject(tx.error);
    });return item;
  }
  async import(bytes:Uint8Array,name:string):Promise<LocalSave>{
    const item=await decodeArchive(bytes);
    if(item.worldData){
      item.save.worldKey=crypto.randomUUID();const store=new WorldDataStore(item.save.worldKey);
      for(const [key,data] of Object.entries(item.worldData))await store.put(key,data);
    }
    return this.put(item.save,item.name||name);
  }
}
