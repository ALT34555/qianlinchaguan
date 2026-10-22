export interface ExplorationSnapshot { regions: {x:number;z:number;bits:number[]}[] }
export function validateExploration(value:unknown):ExplorationSnapshot {
  if(!value||typeof value!=='object')throw new Error('探索记录无效。');
  const snapshot=value as ExplorationSnapshot;
  if(!Array.isArray(snapshot.regions)||snapshot.regions.length>32768)throw new Error('探索记录无效。');
  const keys=new Set<string>();
  for(const r of snapshot.regions){
    if(!r||!Number.isInteger(r.x)||!Number.isInteger(r.z)||Math.abs(r.x)>1000000||Math.abs(r.z)>1000000||
      !Array.isArray(r.bits)||r.bits.length!==128||r.bits.some(v=>!Number.isInteger(v)||v<0||v>255)||keys.has(`${r.x},${r.z}`))throw new Error('探索区域无效。');
    keys.add(`${r.x},${r.z}`);
  }
  return snapshot;
}
export class Exploration {
  private readonly regions = new Map<string,Uint8Array>();
  revision = 0;
  constructor(private readonly circumference?: number, snapshot?: ExplorationSnapshot) {
    for (const region of (snapshot?validateExploration(snapshot).regions:[])) this.regions.set(`${region.x},${region.z}`,Uint8Array.from(region.bits));
  }
  private normalize(cx:number,cz:number): [number,number] | null {
    if (this.circumference) {
      const n=this.circumference;
      if(cz < -n/4 || cz >= n/4)return null;
      cx=((cx+n/2)%n+n)%n-n/2;
    }
    return [Math.floor(cx),Math.floor(cz)];
  }
  has(cx:number,cz:number): boolean {
    const p=this.normalize(cx,cz); if(!p)return false;
    [cx,cz]=p;const x=Math.floor(cx/32),z=Math.floor(cz/32),at=(cz-z*32)*32+cx-x*32;
    return !!((this.regions.get(`${x},${z}`)?.[at>>3]??0) & (1<<(at&7)));
  }
  reveal(cx:number,cz:number,radius=1): boolean {
    let changed=false;
    for(let dz=-radius;dz<=radius;dz++)for(let dx=-radius;dx<=radius;dx++){
      const p=this.normalize(cx+dx,cz+dz);if(!p)continue;
      const [px,pz]=p,x=Math.floor(px/32),z=Math.floor(pz/32),key=`${x},${z}`,at=(pz-z*32)*32+px-x*32;
      const bits=this.regions.get(key)??new Uint8Array(128),mask=1<<(at&7);
      if(!(bits[at>>3]&mask)){bits[at>>3]|=mask;this.regions.set(key,bits);changed=true;}
    }
    if(changed)this.revision++;return changed;
  }
  revealDiamond(cx:number,cz:number,radius=2):boolean {
    let changed=false;
    for(let z=-radius;z<=radius;z++)for(let x=-radius;x<=radius;x++)if(Math.abs(x)+Math.abs(z)<=radius)changed=this.reveal(cx+x,cz+z,0)||changed;
    return changed;
  }
  *coordinates(): Generator<[number,number]> {
    for(const [key,bits] of this.regions){
      const [x,z]=key.split(',').map(Number);
      for(let i=0;i<1024;i++)if(bits[i>>3]&(1<<(i&7)))yield [x*32+i%32,z*32+Math.floor(i/32)];
    }
  }
  snapshot(): ExplorationSnapshot {return {regions:[...this.regions].map(([key,bits])=>{const [x,z]=key.split(',').map(Number);return{x,z,bits:Array.from(bits)};})};}
}
