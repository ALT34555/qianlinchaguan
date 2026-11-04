import type { ChunkResultMessage } from './ChunkProtocol';

export class DerivedChunkCache {
  private readonly entries=new Map<string,{value:ChunkResultMessage;bytes:number}>();
  private bytes=0;
  constructor(private limit=8*1024*1024) {}
  setLimit(limit:number):void {this.limit=Math.max(0,limit);this.trim();}
  get(key:string):ChunkResultMessage|undefined {
    const entry=this.entries.get(key);if(!entry)return;
    this.entries.delete(key);this.entries.set(key,entry);return structuredClone(entry.value);
  }
  put(key:string,value:ChunkResultMessage):void {
    const buffers=new Set<ArrayBufferLike>();
    const visit=(item:unknown):void=>{
      if(ArrayBuffer.isView(item)){buffers.add(item.buffer);return;}
      if(item&&typeof item==='object')for(const child of Object.values(item))visit(child);
    };
    visit(value);
    const bytes=[...buffers].reduce((total,buffer)=>total+buffer.byteLength,0)+(value.decorations?.length??0)*96;
    if(bytes>this.limit)return;
    const previous=this.entries.get(key);if(previous){this.bytes-=previous.bytes;this.entries.delete(key);}
    while(this.entries.size&&(this.bytes+bytes>this.limit||this.entries.size>=32))this.drop();
    this.entries.set(key,{value:structuredClone(value),bytes});this.bytes+=bytes;
  }
  private drop():void {const key=this.entries.keys().next().value!;this.bytes-=this.entries.get(key)!.bytes;this.entries.delete(key);}
  private trim():void {while(this.entries.size&&this.bytes>this.limit)this.drop();}
}
