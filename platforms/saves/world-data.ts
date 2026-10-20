import { mkdir, readFile, readdir, stat, lstat, open, rename, unlink, copyFile } from 'node:fs/promises';
import { constants } from 'node:fs';
import { join, resolve } from 'node:path';
import { randomUUID } from 'node:crypto';
import { validWorldKey, validDataKey } from '../../src/systems/world/WorldDataStore.ts';

export const MAX_WORLD_ENTRY=64*1024*1024;
export const MAX_WORLD_CACHE=256*1024*1024;
export class FileWorldData {
  private readonly queues=new Map<string,Promise<void>>();
  private writes=0;
  readonly directory:string;
  constructor(directory:string){this.directory=resolve(directory);}
  private path(world:string,key:string):string{
    if(!validWorldKey(world)||!validDataKey(key))throw new Error('世界数据路径无效');
    return join(this.directory,world,`${key}.bin`);
  }
  entryPath(world:string,key:string):string{return this.path(world,key);}
  async get(world:string,key:string):Promise<Uint8Array|null>{
    const path=this.path(world,key);
    try{const info=await stat(path);if(info.size>MAX_WORLD_ENTRY)throw new Error('世界数据过大');return await readFile(path);}
    catch(error){if((error as NodeJS.ErrnoException).code==='ENOENT')return null;throw error;}
  }
  async put(world:string,key:string,bytes:Uint8Array):Promise<void>{
    const path=this.path(world,key);if(bytes.length>MAX_WORLD_ENTRY)throw new Error('世界数据过大');
    const slot=`${world}/${key}`,previous=this.queues.get(slot)??Promise.resolve();
    const task=previous.catch(()=>{}).then(async()=>{
      await mkdir(join(this.directory,world),{recursive:true});
      const temporary=`${path}.${randomUUID()}.tmp`,file=await open(temporary,'wx');
      try{await file.writeFile(bytes);await file.sync();}finally{await file.close();}
      try{await rename(temporary,path);}finally{await unlink(temporary).catch(()=>{});}
      if(++this.writes%64===0)await this.trim(world);
    });
    this.queues.set(slot,task);await task.finally(()=>{if(this.queues.get(slot)===task)this.queues.delete(slot);});
  }
  async entries(world:string):Promise<Record<string,Uint8Array>>{
    if(!validWorldKey(world))throw new Error('世界数据编号无效');
    const dir=join(this.directory,world),out:Record<string,Uint8Array>={};
    let names:string[];try{names=await readdir(dir);}catch(error){if((error as NodeJS.ErrnoException).code==='ENOENT')return out;throw error;}
    let size=0;
    for(const filename of names){
      const key=filename.replace(/\.bin$/,'');if(!filename.endsWith('.bin')||!validDataKey(key))continue;
      const bytes=await this.get(world,key);if(!bytes)continue;
      size+=bytes.length;if(size>MAX_WORLD_CACHE+MAX_WORLD_ENTRY)throw new Error('世界导出超过预算');out[key]=bytes;
    }
    return out;
  }
  async keys(world:string):Promise<string[]>{
    if(!validWorldKey(world))throw new Error('世界数据编号无效');
    try{return(await readdir(join(this.directory,world))).filter(n=>n.endsWith('.bin')&&validDataKey(n.slice(0,-4))).map(n=>n.slice(0,-4));}
    catch(error){if((error as NodeJS.ErrnoException).code==='ENOENT')return[];throw error;}
  }
  async copyTo(source:string,target:string):Promise<void>{
    if(source===target||!validWorldKey(source)||!validWorldKey(target))throw new Error('副本世界编号无效');
    await mkdir(join(this.directory,target),{recursive:true});
    let total=0;
    for(const key of await this.keys(source)){
      const path=this.path(source,key),info=await lstat(path);
      total+=info.size;
      if(!info.isFile()||info.isSymbolicLink()||info.size>MAX_WORLD_ENTRY||total>MAX_WORLD_CACHE+MAX_WORLD_ENTRY)throw new Error('世界数据不可复制');
      await copyFile(path,this.path(target,key),constants.COPYFILE_EXCL);
    }
  }
  private async trim(world:string):Promise<void>{
    const dir=join(this.directory,world),entries=[];
    let total=0;
    for(const name of await readdir(dir)){
      if(!name.endsWith('.bin'))continue;
      const info=await stat(join(dir,name));total+=info.size;
      if(/^(chunk|preview)_/.test(name))entries.push({name,size:info.size,time:info.mtimeMs});
    }
    for(const entry of entries.sort((a,b)=>a.time-b.time)){
      if(total<=MAX_WORLD_CACHE)break;
      await unlink(join(dir,entry.name));total-=entry.size;
    }
  }
}
