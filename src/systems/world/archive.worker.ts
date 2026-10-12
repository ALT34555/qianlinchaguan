import { encodeSaveArchive, decodeSaveArchive } from './SaveArchive';
const ctx=self as unknown as Worker;
ctx.onmessage=event=>{
  try{
    if(event.data.kind==='encode'){
      const bytes=encodeSaveArchive(event.data.item,event.data.worldData);ctx.postMessage({value:bytes},[bytes.buffer]);
    }else{
      const value=decodeSaveArchive(event.data.bytes);
      ctx.postMessage({value},Object.values(value.worldData??{}).map(bytes=>bytes.buffer as ArrayBuffer));
    }
  }catch(error){ctx.postMessage({error:error instanceof Error?error.message:String(error)});}
};
