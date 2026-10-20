import { open, lstat } from 'node:fs/promises';
import { createReadStream } from 'node:fs';
import type { Writable } from 'node:stream';
import { createInflateRaw } from 'node:zlib';
import type { ServerResponse } from 'node:http';
import { Zip, ZipPassThrough, inflateSync } from 'fflate';
import { MAX_ARCHIVE_BYTES, MAX_SAVE_BYTES, parseSaveArchiveFiles, saveArchiveFiles } from '../../src/systems/world/SaveArchive.ts';
import { validDataKey } from '../../src/systems/world/WorldDataStore.ts';
import type { LocalSave } from '../../src/systems/world/LocalSaveStore.ts';
import type { FileWorldData } from './world-data.ts';

export async function readSaveMetadata(path:string):Promise<LocalSave> {
  const stat=await lstat(path);
  if(!stat.isFile()||stat.isSymbolicLink()||stat.size>MAX_ARCHIVE_BYTES||stat.size<22)throw new Error('不可读的存档文件');
  const file=await open(path,'r');
  const read=async(offset:number,length:number)=>{
    if(offset<0||length<0||offset+length>stat.size)throw new Error('存档索引无效');
    const bytes=Buffer.alloc(length),result=await file.read(bytes,0,length,offset);
    if(result.bytesRead!==length)throw new Error('存档读取不完整');return bytes;
  };
  try {
    const tail=await read(Math.max(0,stat.size-65557),Math.min(65557,stat.size));
    let end=-1;
    for(let i=tail.length-22;i>=0;i--)if(tail.readUInt32LE(i)===0x06054b50&&i+22+tail.readUInt16LE(i+20)===tail.length){end=i;break;}
    if(end<0||tail.readUInt16LE(end+4)||tail.readUInt16LE(end+6)||tail.readUInt16LE(end+8)!==tail.readUInt16LE(end+10))throw new Error('存档目录无效');
    const count=tail.readUInt16LE(end+10),size=tail.readUInt32LE(end+12),offset=tail.readUInt32LE(end+16);
    if(size>4*1024*1024||offset+size>stat.size-tail.length+end)throw new Error('存档目录过大');
    const directory=await read(offset,size),files:Record<string,Uint8Array>={},names=new Set<string>();
    let at=0,total=0,worldTotal=0;
    for(let i=0;i<count;i++){
      if(at+46>directory.length||directory.readUInt32LE(at)!==0x02014b50)throw new Error('存档条目无效');
      const flags=directory.readUInt16LE(at+8),method=directory.readUInt16LE(at+10),compressed=directory.readUInt32LE(at+20),original=directory.readUInt32LE(at+24);
      const nameSize=directory.readUInt16LE(at+28),extra=directory.readUInt16LE(at+30),comment=directory.readUInt16LE(at+32),local=directory.readUInt32LE(at+42);
      if(at+46+nameSize+extra+comment>directory.length||flags&1||![0,8].includes(method)||local>=offset)throw new Error('存档条目无效');
      const name=directory.toString('utf8',at+46,at+46+nameSize),binary=/^world-data\/(.+)\.bin$/.exec(name);
      if(names.has(name)||(!['world.json','player.json','human-activity.json'].includes(name)&&!(binary&&validDataKey(binary[1]))))throw new Error('存档文件名无效');
      names.add(name);
      if(binary){worldTotal+=original;if(original>64*1024*1024||worldTotal>MAX_ARCHIVE_BYTES)throw new Error('区块存档过大');}
      else {
        total+=original;if(total>MAX_SAVE_BYTES||compressed>MAX_SAVE_BYTES)throw new Error('存档元数据过大');
        const header=await read(local,30);
        if(header.readUInt32LE(0)!==0x04034b50||header.readUInt16LE(8)!==method)throw new Error('存档本地索引无效');
        const start=local+30+header.readUInt16LE(26)+header.readUInt16LE(28);
        if(start+compressed>offset)throw new Error('存档条目越界');
        const payload=await read(start,compressed);
        const bytes=method===0?payload:inflateSync(payload,{out:new Uint8Array(original)});
        if(bytes.length!==original)throw new Error('存档元数据大小不符');files[name]=bytes;
      }
      at+=46+nameSize+extra+comment;
    }
    if(at!==directory.length)throw new Error('存档目录尾部无效');
    return parseSaveArchiveFiles(files);
  } finally {await file.close();}
}

async function* embeddedData(path:string,excluded:Set<string>):AsyncGenerator<{key:string;stream:AsyncIterable<Buffer>;size:number}> {
  const file=await open(path,'r');
  try {
    const stat=await file.stat(),tail=Buffer.alloc(Math.min(65557,stat.size));await file.read(tail,0,tail.length,stat.size-tail.length);
    let end=-1;for(let i=tail.length-22;i>=0;i--)if(tail.readUInt32LE(i)===0x06054b50&&i+22+tail.readUInt16LE(i+20)===tail.length){end=i;break;}
    if(end<0)throw new Error('存档目录无效');
    const size=tail.readUInt32LE(end+12),offset=tail.readUInt32LE(end+16);
    if(size>4*1024*1024||offset+size>stat.size-22)throw new Error('存档目录过大');
    const directory=Buffer.alloc(size);await file.read(directory,0,directory.length,offset);
    let at=0;
    while(at<directory.length){
      if(at+46>directory.length||directory.readUInt32LE(at)!==0x02014b50)throw new Error('存档目录无效');
      const nameSize=directory.readUInt16LE(at+28),name=directory.toString('utf8',at+46,at+46+nameSize),match=/^world-data\/(.+)\.bin$/.exec(name);
      if(at+46+nameSize+directory.readUInt16LE(at+30)+directory.readUInt16LE(at+32)>directory.length)throw new Error('存档目录越界');
      if(match&&!excluded.has(match[1])){
        const compressed=directory.readUInt32LE(at+20),size=directory.readUInt32LE(at+24),local=directory.readUInt32LE(at+42),method=directory.readUInt16LE(at+10);
        const header=Buffer.alloc(30);await file.read(header,0,30,local);
        const start=local+30+header.readUInt16LE(26)+header.readUInt16LE(28);
        if(header.readUInt32LE(0)!==0x04034b50||![0,8].includes(method)||size>64*1024*1024||start+compressed>offset)throw new Error('区块存档越界');
        const input=compressed?createReadStream(path,{start,end:start+compressed-1,highWaterMark:64*1024}):undefined;
        try {
          if(method===8&&input){const inflated=createInflateRaw();input.on('error',err=>inflated.destroy(err));yield {key:match[1],stream:input.pipe(inflated),size};}
          else yield {key:match[1],stream:input??(async function*(){})(),size};
        } finally {input?.destroy();}
      }
      at+=46+nameSize+directory.readUInt16LE(at+30)+directory.readUInt16LE(at+32);
    }
  } finally {await file.close();}
}
export async function writeSaveArchive(response:Writable,item:LocalSave,data?:FileWorldData,source?:string):Promise<void> {
  let blocked=false,written=0,error:Error|undefined;
  const zip=new Zip((err,chunk,final)=>{
    if(err){error=err;return;}
    written+=chunk.length;
    if(written>MAX_ARCHIVE_BYTES){error=new Error('存档导出超过预算');return;}
    if(!response.write(chunk))blocked=true;
    if(final)response.end();
  });
  const drain=async()=>{
    if(error)throw error;
    if(response.destroyed)throw new Error('导出连接关闭');
    if(!blocked)return;
    await new Promise<void>((resolve,reject)=>{
      const cleanup=()=>{response.off('drain',done);response.off('close',closed);response.off('error',failed);};
      const done=()=>{cleanup();blocked=false;resolve();};
      const closed=()=>{cleanup();reject(new Error('导出连接关闭'));};
      const failed=(err:Error)=>{cleanup();reject(err);};
      response.once('drain',done);response.once('close',closed);response.once('error',failed);
    });
  };
  try {
    const included=new Set<string>();
    for(const [name,bytes] of Object.entries(saveArchiveFiles(item))){const entry=new ZipPassThrough(name);zip.add(entry);entry.push(bytes,true);await drain();}
    if(data&&item.save.worldKey)for(const key of await data.keys(item.save.worldKey)){
      const path=data.entryPath(item.save.worldKey,key),stat=await lstat(path);
      if(!stat.isFile()||stat.isSymbolicLink()||stat.size>64*1024*1024)throw new Error('世界数据不可读');
      const entry=new ZipPassThrough('world-data/'+key+'.bin');zip.add(entry);await drain();
      let size=0;
      for await(const bytes of createReadStream(path,{highWaterMark:64*1024})){size+=bytes.length;if(size>64*1024*1024)throw new Error('世界数据过大');entry.push(bytes as Buffer,false);await drain();}
      if(size!==stat.size)throw new Error('导出期间世界数据发生变化');
      entry.push(new Uint8Array(),true);await drain();
      included.add(key);
    }
    if(source)for await(const embedded of embeddedData(source,included)){
      const entry=new ZipPassThrough('world-data/'+embedded.key+'.bin');zip.add(entry);let size=0;
      for await(const bytes of embedded.stream){size+=bytes.length;if(size>embedded.size)throw new Error('区块大小不符');entry.push(bytes,false);await drain();}
      if(size!==embedded.size)throw new Error('区块大小不符');entry.push(new Uint8Array(),true);await drain();
    }
    zip.end();if(error)throw error;
  } catch(err){zip.terminate();response.destroy();throw err;}
}
export async function streamSaveArchive(response:ServerResponse,item:LocalSave,data:FileWorldData,source?:string):Promise<void>{
  response.setHeader('Content-Type','application/zip');response.setHeader('Cache-Control','no-store');
  await writeSaveArchive(response,item,data,source);
}
