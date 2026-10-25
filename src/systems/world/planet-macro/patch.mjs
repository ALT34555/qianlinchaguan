import {sampleSurface} from './backend.mjs';
import {retrySample,runSync} from './preview-scheduler.mjs';
import {coverRGB} from './surface-cover.mjs';

const patterns=new Map();let patternBytes=0;
export function topology(side){
  if(patterns.has(side))return patterns.get(side);
  const indices=new Uint32Array((side-1)**2*6);let at=0;
  for(let z=0;z<side-1;z++)for(let x=0;x<side-1;x++){const a=z*side+x,b=a+side;indices[at++]=a;indices[at++]=b;indices[at++]=a+1;indices[at++]=b;indices[at++]=b+1;indices[at++]=a+1;}
  if(indices.byteLength<=4*2**20){while(patternBytes+indices.byteLength>4*2**20){const key=patterns.keys().next().value;patternBytes-=patterns.get(key).byteLength;patterns.delete(key);}patterns.set(side,indices);patternBytes+=indices.byteLength;}return indices;
}
export function gridBuffers(side,reuse=null){return reuse?.side===side?reuse:{side,positions:new Float32Array(side*side*3),colors:new Float32Array(side*side*3),levels:new Float32Array(side*side),kinds:new Uint8Array(side*side),wetlands:new Float32Array(side*side)};}
export function finishGrid(cx,cz,size,step,buffers,min,max,start){
  const {side,positions,levels}=buffers,waterPositions=[],waterIndices=[];
  for(let z=0;z<side-1;z++)for(let x=0;x<side-1;x++){
    const a=z*side+x,b=a+side,c=b+1,d=a+1;
    if(Number.isFinite(levels[a])&&levels[a]>positions[a*3+1]&&Number.isFinite(levels[b])&&levels[b]>positions[b*3+1]&&Number.isFinite(levels[c])&&levels[c]>positions[c*3+1]&&Number.isFinite(levels[d])&&levels[d]>positions[d*3+1]){
      const base=waterPositions.length/3;for(const i of [a,b,c,d])waterPositions.push(positions[i*3],levels[i]+.02,positions[i*3+2]);waterIndices.push(base,base+1,base+3,base+1,base+2,base+3);
    }
  }
  return {...buffers,cx,cz,size,step,indices:topology(side),waterPositions:new Float32Array(waterPositions),waterIndices:new Uint32Array(waterIndices),min,max,elapsed:performance.now()-start};
}

export function* buildPatchSteps(world,cx,cz,size=64,step=2,sampler=(x,z)=>sampleSurface(world,x,z),edgeRemainder=false,options={}) {
  if(!Number.isInteger(size)||!Number.isInteger(step)||size<1||step<1||(!edgeRemainder&&size%step))throw Error('地块尺寸与步长不匹配');
  const start=performance.now(),side=Math.ceil(size/step)+1,buffers=gridBuffers(side,options.reuse),{positions,colors,levels,kinds,wetlands}=buffers;let min=Infinity,max=-Infinity;
  for(let z=0;z<side;z++)for(let x=0;x<side;x++){
    const i=z*side+x,px=Math.min(size,x*step),pz=Math.min(size,z*step),s=yield* retrySample(()=>sampler(cx*64+px,cz*64+pz));positions[i*3]=px;positions[i*3+1]=s.height;positions[i*3+2]=pz;levels[i]=s.water;kinds[i]=s.kind;wetlands[i]=s.wetland??0;min=Math.min(min,s.height);max=Math.max(max,s.height);
    const rgb=coverRGB(s);for(let k=0;k<3;k++)colors[i*3+k]=rgb[k]/255;options.onSample?.(px,pz,s);
    if(i%64===63)yield {stage:'区域采样',completed:i+1,total:side*side};
  }
  return finishGrid(cx,cz,size,step,buffers,min,max,start);
}

export function buildPatch(...args){return runSync(buildPatchSteps(...args));}
