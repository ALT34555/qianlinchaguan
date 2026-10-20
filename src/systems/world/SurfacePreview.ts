import type {PreviewChunk} from './WorldContract';

export interface SurfacePreview {cx: number;cz: number;size: number;average: [number,number,number];rgba?: Uint8ClampedArray<ArrayBuffer>;refined?:boolean}
/** 稀疏采样合成粗预览与平均色 */
export function buildSurfacePreview(chunk: PreviewChunk,image=true): SurfacePreview {
  const size=chunk.size,step=chunk.step,last=size-1;
  const rgba=image?new Uint8ClampedArray(size*size*4):undefined,average:[number,number,number]=[0,0,0];
  const shallow=[96,166,158],deep=[28,70,104];
  for(let z=0;z<size;z++)for(let x=0;x<size;x++){
    const i=z*size+x,o=i*4,h=chunk.heights[i],w=chunk.waterLevels[i],wet=Number.isFinite(w)&&h<w;
    const depth=Math.min(1,Math.max(0,w-h)/14);
    let shade=1;
    if(rgba){
      const slope=((chunk.heights[Math.max(0,z-1)*size+x]-chunk.heights[Math.min(last,z+1)*size+x])*.10+
        (chunk.heights[z*size+Math.max(0,x-1)]-chunk.heights[z*size+Math.min(last,x+1)])*.07)/step;
      shade=wet?1:Math.max(.68,Math.min(1.18,.98+slope));
    }
    for(let c=0;c<3;c++){
      const value=wet?shallow[c]+(deep[c]-shallow[c])*depth:chunk.surfaceColors[i*3+c];
      average[c]+=value;
      if(rgba)rgba[o+c]=Math.max(0,Math.min(255,value*shade));
    }
    if(rgba)rgba[o+3]=255;
  }
  for(let c=0;c<3;c++)average[c]/=size*size;
  return rgba?{cx:chunk.cx,cz:chunk.cz,size,rgba,average}:{cx:chunk.cx,cz:chunk.cz,size,average};
}
