import { gzipSync, gunzipSync } from 'fflate';

const MAX_ENTRY = 64 * 1024 * 1024;
const arrays = { Uint8Array, Uint8ClampedArray, Uint16Array, Uint32Array, Int16Array, Int32Array, Float32Array, Float64Array };
const crcTable=Uint32Array.from({length:256},(_,i)=>{let n=i;for(let bit=0;bit<8;bit++)n=n&1?0xedb88320^(n>>>1):n>>>1;return n>>>0;});
function checksum(bytes:Uint8Array):number {let crc=-1;for(const byte of bytes)crc=crcTable[(crc^byte)&255]^(crc>>>8);return(crc^-1)>>>0;}

export function encodeWorldData(value: unknown): Uint8Array<ArrayBuffer> {
  const buffers: Uint8Array[] = [];
  const visit = (v: unknown): unknown => {
    if (ArrayBuffer.isView(v) && !(v instanceof DataView)) {
      const bytes = new Uint8Array(v.buffer,v.byteOffset,v.byteLength);
      const index = buffers.push(bytes) - 1;
      return { $array:v.constructor.name, index, bytes:bytes.length };
    }
    if (Array.isArray(v)) return v.map(visit);
    if (v && typeof v === 'object') return Object.fromEntries(Object.entries(v).map(([k,x])=>[k,visit(x)]));
    return v;
  };
  const header = new TextEncoder().encode(JSON.stringify(visit(value)));
  const size = 4 + header.length + buffers.reduce((n,b)=>n+b.length,0);
  if (size > MAX_ENTRY) throw new Error('区块缓存过大');
  const raw = new Uint8Array(size); new DataView(raw.buffer).setUint32(0,header.length,true);
  raw.set(header,4); let at = 4 + header.length;
  for (const buffer of buffers) { raw.set(buffer,at); at += buffer.length; }
  return gzipSync(raw,{level:1});
}

export function decodeWorldData<T>(bytes: Uint8Array): T {
  if (bytes.length < 18 || bytes.length > MAX_ENTRY || bytes[0] !== 31 || bytes[1] !== 139) throw new Error('区块缓存无效');
  const expected = new DataView(bytes.buffer,bytes.byteOffset,bytes.byteLength).getUint32(bytes.length-4,true);
  if (expected < 4 || expected > MAX_ENTRY) throw new Error('区块解压大小无效');
  const raw = gunzipSync(bytes);
  if (raw.length !== expected) throw new Error('区块缓存大小不符');
  if(checksum(raw)!==new DataView(bytes.buffer,bytes.byteOffset,bytes.byteLength).getUint32(bytes.length-8,true))throw new Error('区块校验失败');
  const headerSize = new DataView(raw.buffer,raw.byteOffset,raw.byteLength).getUint32(0,true);
  if (headerSize > raw.length - 4) throw new Error('区块索引无效');
  let at = 4 + headerSize, next = 0;
  const restore = (v: unknown): unknown => {
    if (Array.isArray(v)) return v.map(restore);
    if (v && typeof v === 'object') {
      const record = v as Record<string,unknown>;
      if ('$array' in record) {
        const Constructor = arrays[record.$array as keyof typeof arrays];
        const size = record.bytes as number;
        if (!Constructor || record.index !== next++ || !Number.isInteger(size) || size < 0 || size % Constructor.BYTES_PER_ELEMENT || at + size > raw.length) throw new Error('区块数组无效');
        const buffer = raw.slice(at,at+size).buffer; at += size;
        return new Constructor(buffer);
      }
      return Object.fromEntries(Object.entries(record).map(([k,x])=>[k,restore(x)]));
    }
    return v;
  };
  const value = restore(JSON.parse(new TextDecoder().decode(raw.subarray(4,4+headerSize))));
  if (at !== raw.length) throw new Error('区块缓存尾部无效');
  return value as T;
}
