import { encodeSaveArchive, decodeSaveArchive } from './SaveArchive';
import type { LocalSave } from './LocalSaveStore';
export async function encodeArchive(item:LocalSave,worldData:Record<string,Uint8Array>):Promise<Uint8Array>{
  if(typeof Worker==='undefined'||typeof location==='undefined')return encodeSaveArchive(item,worldData);
  return operation({kind:'encode',item,worldData},Object.values(worldData).map(bytes=>bytes.buffer as ArrayBuffer));
}
export async function decodeArchive(bytes:Uint8Array):Promise<LocalSave>{
  if(typeof Worker==='undefined'||typeof location==='undefined')return decodeSaveArchive(bytes);
  return operation({kind:'decode',bytes},[bytes.buffer as ArrayBuffer]);
}
function operation<T>(message:unknown,transfer:Transferable[]):Promise<T>{
  const worker=new Worker(new URL('./archive.worker.ts',import.meta.url),{type:'module'});
  return new Promise<T>((resolve,reject)=>{
    worker.onmessage=event=>event.data.error?reject(new Error(event.data.error)):resolve(event.data.value);
    worker.onerror=event=>reject(new Error(event.message));worker.postMessage(message,[...new Set(transfer)]);
  }).finally(()=>worker.terminate());
}
