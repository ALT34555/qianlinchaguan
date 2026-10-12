import type { RiverNetwork } from './RiverNetwork';
import type { WatershedLake } from './Watershed';

export type BasinLake = WatershedLake;

/** 湖泊与河流使用同一张排水树 */
export class BasinLakes {
  private readonly refined = new Map<BasinLake, BasinLake>();
  constructor(private readonly network: RiverNetwork, private readonly height?: (cx: number, cz: number) => number) {}
  clear(): void { this.refined.clear(); }
  /** 超限时按插入顺序淘汰最旧的湖面精修结果 */
  trim(keep = .75): void {
    const target = Math.floor(this.refined.size * keep);
    while (this.refined.size > target) this.refined.delete(this.refined.keys().next().value!);
  }
  get(cx: number, cz: number): BasinLake | null {
    const lake=this.network.lake(cx,cz);
    if(!lake||!this.height) return lake;
    const cached=this.refined.get(lake);
    if(cached) return cached;
    const watershed=this.network.watershed,step=watershed.step;
    let level=lake.level;
    for(const cell of lake.cells) {
      const [gx,gz]=cell.split(',').map(Number),[mx,mz]=watershed.position(gx,gz);
      const x0=mx-Math.floor(step/2),z0=mz-Math.floor(step/2);
      for(const [dx,dz] of [[-1,0],[1,0],[0,-1],[0,1]]) {
        const ex=x0+(dx>0?step-1:0),ez=z0+(dz>0?step-1:0);
        if(this.network.contains(lake,ex+dx,ez+dz)) continue;
        for(let i=0;i<step;i++) {
          const x=ex+(dx===0?i:0),z=ez+(dz===0?i:0);
          level=Math.min(level,Math.max(0,this.height(x,z),this.height(x+dx,z+dz)));
        }
      }
    }
    const refined={...lake,level};this.refined.set(lake,refined);return refined;
  }
  contains(lake: BasinLake, cx: number, cz: number): boolean { return this.network.contains(lake, cx, cz); }
}
