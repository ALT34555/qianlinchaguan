import {CHUNK_SIZE} from '../../core/config';
import {PADDED_SIZE, type ChunkGenResult} from './WorldGenerator';

export interface SurfacePreview {cx: number;cz: number;rgba: Uint8ClampedArray<ArrayBuffer>;average: [number,number,number]}
/** 与实际顶层的混合材质颜色、水深共用数据 */
export function buildSurfacePreview(chunk: ChunkGenResult): SurfacePreview {
  const rgba=new Uint8ClampedArray(CHUNK_SIZE*CHUNK_SIZE*4),average:[number,number,number]=[0,0,0];
  const shallow=[96,166,158],deep=[28,70,104];
  for(let z=0;z<CHUNK_SIZE;z++)for(let x=0;x<CHUNK_SIZE;x++){
    const i=(z+1)*PADDED_SIZE+x+1,o=(z*CHUNK_SIZE+x)*4;
    const h=chunk.heights[i],w=chunk.waterLevels[i],wet=Number.isFinite(w)&&h<w;
    const depth=Math.min(1,Math.max(0,w-h)/14);
    const slope=(chunk.heights[i-PADDED_SIZE]-chunk.heights[i+PADDED_SIZE])*.10+(chunk.heights[i-1]-chunk.heights[i+1])*.07;
    const shade=wet?1:Math.max(.68,Math.min(1.18,.98+slope));
    for(let c=0;c<3;c++){
      const value=wet?shallow[c]+(deep[c]-shallow[c])*depth:chunk.surfaceColors[i*3+c];
      average[c]+=value;rgba[o+c]=Math.max(0,Math.min(255,value*shade));
    }
    rgba[o+3]=255;
  }
  for(let c=0;c<3;c++)average[c]/=CHUNK_SIZE*CHUNK_SIZE;
  return {cx:chunk.cx,cz:chunk.cz,rgba,average};
}
