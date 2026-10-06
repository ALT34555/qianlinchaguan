import { Watershed, type WatershedCell, type WatershedLake } from './Watershed';
import {riverDirectionRoute,riverRouteRandom,riverPerturbationProbability} from './RiverRouting';
import {riverTurnBridge,type RiverCell} from './RiverTurns';
import {riverWidth,MAX_RIVER_RADIUS} from './RiverChannels';
import {RIVER_BANK_BLEND} from './RiverGeometry';
import {mergedRiverSize} from './RiverSize';
import {CHUNK_SIZE} from '../../core/config';
import { FLOW_DIRECTIONS, effectiveRunoff, RIVER_THRESHOLD, liquidRiverAllowed, waterfallDrop, splashWetlandDrop, type DrainageNode } from './Hydrology';
export { FLOW_DIRECTIONS, effectiveRunoff, RIVER_THRESHOLD, type DrainageNode } from './Hydrology';
const key = (x: number, z: number) => `${x},${z}`;
/** 非主河格取邻近主河道流向时的最大搜索半径（切比雪夫距离） */
const CORRIDOR_FLOW_RADIUS = 2;
/** 方向与“主河→本块”连线的最小同向余弦，避免借到反向河道 */
const CORRIDOR_FLOW_ALIGNMENT = .5;
/** 河槽带按水面真正覆盖到的陆地核心判定，核心从区块边界内收。 */
function coreDistance(cx: number, cz: number, ax: number, az: number, bx: number, bz: number): number {
  const minX = cx * CHUNK_SIZE + RIVER_BANK_BLEND, maxX = (cx + 1) * CHUNK_SIZE - RIVER_BANK_BLEND;
  const minZ = cz * CHUNK_SIZE + RIVER_BANK_BLEND, maxZ = (cz + 1) * CHUNK_SIZE - RIVER_BANK_BLEND;
  const at = (t: number) => {
    const px = ax + (bx - ax) * t, pz = az + (bz - az) * t;
    return Math.hypot(Math.max(minX - px, 0, px - maxX), Math.max(minZ - pz, 0, pz - maxZ));
  };
  let low = 0, high = 1;
  for (let i = 0; i < 20; i++) {
    const one = low + (high - low) / 3, two = high - (high - low) / 3;
    if (at(one) < at(two)) high = two; else low = one;
  }
  return at((low + high) / 2);
}

interface RiverRoute {
  direction: number; level: number; discharge: number; area: number; order: number;
  coarse: WatershedCell; apex: boolean;
  progress: number;
  main: boolean;
  displaced:{cx:number;cz:number}|null;
  widthBlocks:number;
}
interface RiverTurn {route:RiverRoute; paths:RiverCell[][]; incoming:RiverCell[]}
export interface AlluvialFan {
  x: number; z: number; dx: number; dz: number; level: number; grade: number;
  length: number; width: number; discharge: number;
}

/** 共用流域求解器的区块接口 */
export class RiverNetwork {
  readonly watershed: Watershed;
  private readonly routes = new Map<string, RiverRoute>();
  private readonly turns=new Map<string,RiverTurn|null>();
  private readonly rounded=new Map<string,RiverRoute|undefined>();
  private readonly corridors=new Map<string,number>();
  private readonly corridorBlocks=new Map<string,number>();
  private readonly corridorDirs=new Map<string,number>();
  private readonly reaches = new Set<string>();
  private readonly queried = new Set<string>();
  private readonly fans = new Map<string, AlluvialFan | null>();
  private readonly wetlands=new Map<string,number>();
  private readonly falls=new Map<string,number>();

  constructor(private readonly node: (cx: number, cz: number) => DrainageNode,
    private readonly precipitation = .8, private readonly circumference?: number,
    private readonly valley?: (x: number, z: number) => number, boundarySample=node,private readonly seed=0) {
    this.watershed = new Watershed(node, precipitation, this.threshold, circumference, valley,boundarySample);
  }
  // 0.5 经三种种子标定为约一半细河
  get threshold(): number { return RIVER_THRESHOLD + (.5 - this.precipitation) * 212; }
  clear(): void {
    this.routes.clear(); this.reaches.clear(); this.queried.clear(); this.fans.clear();
    this.turns.clear();this.rounded.clear();
    this.corridors.clear();
    this.corridorBlocks.clear();
    this.corridorDirs.clear();
    this.wetlands.clear();this.falls.clear();
    this.watershed.clear();
  }
  private wrap(x: number): number {
    if (!this.circumference) return x;
    return ((x + this.circumference / 2) % this.circumference + this.circumference) % this.circumference - this.circumference / 2;
  }
  private rawRoute(cx: number, cz: number): RiverRoute | undefined {
    if (!this.precipitation) return undefined;
    cx = this.wrap(cx);
    const [gx, gz] = this.watershed.coordinates(cx, cz), k = key(gx, gz);
    if (!this.queried.has(k)) {
      for (let z = gz - 1; z <= gz + 1; z++) for (let x = gx - 1; x <= gx + 1; x++) this.reach(x, z);
      this.queried.add(k);
    }
    return this.routes.get(key(cx, cz));
  }
  /** 主支流都按相同点积规则削角。 */
  private turn(cx:number,cz:number):RiverTurn|null{
    cx=this.wrap(cx);const k=key(cx,cz);if(this.turns.has(k))return this.turns.get(k)!;
    const r=this.rawRoute(cx,cz),paths:RiverCell[][]=[],incoming:RiverCell[]=[];
    if(r?.main){
      const d=FLOW_DIRECTIONS[r.direction],next:RiverCell=[cx+d.dx,cz+d.dz];
      const target=this.rawRoute(...next);
      if(target?.main&&r.level>=target.level){
        for(const step of FLOW_DIRECTIONS){
          const p:RiverCell=[cx+step.dx,cz+step.dz],up=this.rawRoute(...p);
          if(!up?.main)continue;
          const f=FLOW_DIRECTIONS[up.direction];if(f.dx!==-step.dx||f.dz!==-step.dz)continue;
          incoming.push(p);
          const bridge=riverTurnBridge(p,[cx,cz],next);if(!bridge)continue;
          if(bridge.length===3&&this.rawRoute(...bridge[1])?.main)continue;
          const nodes=bridge.map(([x,z])=>this.node(this.wrap(x),z));
          if(nodes.some(n=>!liquidRiverAllowed(n))||!liquidRiverAllowed(this.node(cx,cz)))continue;
          if(nodes.some((n,i)=>i>0&&Math.abs(n.height-nodes[i-1].height)>=16))continue;
          if(Math.abs(this.node(cx,cz).height-nodes[0].height)>=16||Math.abs(this.node(cx,cz).height-nodes.at(-1)!.height)>=16)continue;
          paths.push(bridge);
        }
      }
    }
    const result=r&&paths.length?{route:r,paths,incoming}:null;this.turns.set(k,result);return result;
  }
  /** 原网格查询与削角结果分离。 */
  private route(cx:number,cz:number):RiverRoute|undefined{
    cx=this.wrap(cx);const k=key(cx,cz);if(this.rounded.has(k))return this.rounded.get(k);
    let result=this.rawRoute(cx,cz);
    for(let z=cz-1;z<=cz+1;z++)for(let x=cx-1;x<=cx+1;x++){
      const turn=this.turn(x,z);if(!turn)continue;
      for(const path of turn.paths)for(let i=0;i<path.length-1;i++){
        const p=path[i],next=path[i+1];if(this.wrap(p[0])!==cx||p[1]!==cz)continue;
        const source=i===0?this.rawRoute(...p)!:turn.route;
        result={...source,direction:FLOW_DIRECTIONS.findIndex(d=>d.dx===next[0]-p[0]&&d.dz===next[1]-p[1]),
          apex:false,displaced:i?{cx:this.wrap(x),cz:z}:source.displaced};
      }
      if(this.wrap(x)===cx&&z===cz){
        const diverted=turn.paths.map(p=>this.rawRoute(...p[0])!);
        const remaining=turn.incoming.filter(p=>!turn.paths.some(path=>path[0]===p)).map(p=>this.rawRoute(...p)!);
        const discharge=Math.max(effectiveRunoff(this.node(cx,cz),this.precipitation),turn.route.discharge-diverted.reduce((s,r)=>s+r.discharge,0));
        const size=mergedRiverSize(discharge,remaining.map(r=>({capacity:r.coarse.riverCapacity??(r.discharge>=2500?2:1),blocks:r.widthBlocks??(r.discharge>=2500?2:1)})));
        result={...turn.route,main:remaining.length>0,widthBlocks:size.blocks,discharge,
          area:Math.max(1,turn.route.area-diverted.reduce((s,r)=>s+r.area,0))};
      }
    }
    this.rounded.set(k,result);return result;
  }
  private reach(gx: number, gz: number): void {
    [gx, gz] = this.watershed.normalize(gx, gz);
    const k = key(gx, gz);
    if (this.reaches.has(k)) return;
    this.reaches.add(k);
    const a = this.watershed.cell(gx, gz), b = a.receiver;
    if (!a.weakChannel || !b) return;
    const [ax, az] = this.watershed.position(a.x, a.z);
    let [bx, bz] = this.watershed.position(b.x, b.z);
    if (this.circumference) bx = ax + this.wrap(bx - ax);
    if (Math.abs(bx - ax) > this.watershed.step * 1.5 || Math.abs(bz - az) > this.watershed.step * 1.5) return;
    const count = Math.max(Math.abs(bx - ax), Math.abs(bz - az));
    const points=riverDirectionRoute([ax,az],[bx,bz],salt=>riverRouteRandom(this.seed,a.x,a.z,salt),
      (previous,detour,next)=>{
        if(!a.channel)return false;
        const p=this.node(this.wrap(previous[0]),previous[1]),d=this.node(this.wrap(detour[0]),detour[1]),n=this.node(this.wrap(next[0]),next[1]);
        if(!liquidRiverAllowed(p)||!liquidRiverAllowed(d)||!liquidRiverAllowed(n)||
          Math.abs(p.height-d.height)>=16||Math.abs(d.height-n.height)>=16)return false;
        for(const [a,b] of [[previous,detour],[detour,next]]){
          if(a[0]===b[0]||a[1]===b[1])continue;
          for(const [x,z] of [[a[0],b[1]],[b[0],a[1]]]){
            const bank=this.node(this.wrap(x),z);
            if(!liquidRiverAllowed(bank)||bank.height>Math.min(p.height,d.height,n.height)+16)return false;
          }
        }
        return true;
      },cell=>riverPerturbationProbability(this.node(this.wrap(cell[0]),cell[1]).temperature));
    let progress=0;
    for (let i = 0; i < count; i++) {
      const [x,z]=points[i],[nextX,nextZ]=points[i+1];
      const direction = FLOW_DIRECTIONS.findIndex(d => d.dx === nextX - x && d.dz === nextZ - z);
      const raw=this.node(this.wrap(x),z),lower=this.node(this.wrap(nextX),nextZ);
      if(!liquidRiverAllowed(raw)||lower.height>0&&!liquidRiverAllowed(lower)||Math.abs(raw.height-lower.height)>256)continue;
      const rawProgress=a.height>b.height?Math.max(0,Math.min(1,(a.height-raw.height)/(a.height-b.height))):i/count;
      progress=Math.max(progress,rawProgress*.85+i/count*.15);
      const originalX=Math.round(ax+(bx-ax)*i/count),originalZ=Math.round(az+(bz-az)*i/count);
      const candidate={direction, level:a.level+(b.level-a.level)*progress,
        discharge:a.discharge,area:a.area,order:a.order,coarse:a,apex:i===0,progress:i/count,main:a.channel,
        displaced:x!==originalX||z!==originalZ?{cx:this.wrap(originalX),cz:originalZ}:null,widthBlocks:a.riverBlocks};
      const k=key(this.wrap(x),z),previous=this.routes.get(k);
      if(!previous || candidate.main&&!previous.main || candidate.main===previous.main&&candidate.discharge>previous.discharge)this.routes.set(k,candidate);
    }
  }
  flow(cx: number, cz: number): number { return this.route(cx, cz)?.direction ?? -1; }
  accumulation(cx: number, cz: number): number {
    return this.route(cx, cz)?.discharge ?? effectiveRunoff(this.node(cx, cz), this.precipitation);
  }
  area(cx: number, cz: number): number { return this.route(cx, cz)?.area ?? 1; }
  order(cx: number, cz: number): number { return this.route(cx, cz)?.order ?? 0; }
  /** 几何曲流独立于排水拓扑 */
  bend(cx: number, cz: number): {x: number; z: number} {
    const route = this.route(cx,cz), b = route?.coarse.receiver;
    if (!route || !b) return {x:0,z:0};
    const [ax,az] = this.watershed.position(route.coarse.x,route.coarse.z);
    let [bx,bz] = this.watershed.position(b.x,b.z);
    if (this.circumference) bx = ax+this.wrap(bx-ax);
    const length = Math.hypot(bx-ax,bz-az);
    const t = route.progress;
    const signal = this.valley?.(ax+(bx-ax)*t, az+(bz-az)*t) ?? 0;
    const phase = (this.valley?.(ax,az) ?? 0) * Math.PI;
    const grade = (route.coarse.level-b.level) / (length*64);
    const amplitude = 34 + 30 / (1 + Math.max(0,grade)*8);
    const offset = (signal * .7 + Math.sin(t*Math.PI*2+phase) * .3) * amplitude * Math.sin(Math.PI*t)**2;
    return {x:-(bz-az)/length*offset,z:(bx-ax)/length*offset};
  }
  level(cx: number, cz: number): number {
    const route = this.route(cx, cz);
    if (route) return route.level;
    const [x, z] = this.watershed.coordinates(this.wrap(cx), cz), c = this.watershed.cell(x, z);
    const [px, pz] = this.watershed.position(c.x, c.z);
    return px === this.wrap(cx) && pz === cz ? c.level : Math.max(0, this.node(cx, cz).height - 1.2);
  }
  upstream(cx: number, cz: number): [number, number][] {
    const result: [number, number][] = [];
    for (const d of FLOW_DIRECTIONS) {
      const x = cx + d.dx, z = cz + d.dz, f = this.flow(x, z);
      if (f >= 0 && FLOW_DIRECTIONS[f].dx === -d.dx && FLOW_DIRECTIONS[f].dz === -d.dz) result.push([x, z]);
    }
    return result;
  }
  /** 主河以最强上游作为跨区块几何导线 */
  dominantUpstream(cx:number,cz:number):[number,number]|null{
    const main=this.route(cx,cz)?.main;
    let best:[number,number]|null=null,q=-Infinity;
    for(const p of this.upstream(cx,cz)){
      const r=this.route(...p);if(!r||main&&!r.main)continue;
      if(r.discharge>q||r.discharge===q&&best&&(p[1]<best[1]||p[1]===best[1]&&p[0]<best[0])){best=p;q=r.discharge;}
    }
    return best;
  }
  isChannel(cx: number, cz: number): boolean { return this.precipitation > 0 && !!this.route(cx, cz)?.main; }
  widthBlocks(cx:number,cz:number):number{
    const r=this.route(cx,cz);if(r?.main)return r.widthBlocks??(r.discharge>=2500?2:1);
    this.corridor(cx,cz);return this.corridorBlocks.get(key(this.wrap(cx),cz))??1;
  }
  /** 河区块流向：非主河格先取指向本块的邻近主河道，再退化为同向的最近主河道。 */
  corridorDirection(cx:number,cz:number):number{
    cx=this.wrap(cx);
    const k=key(cx,cz),cached=this.corridorDirs.get(k);if(cached!==undefined)return cached;
    let result=-1;
    const r=this.route(cx,cz);
    if(r?.main)result=r.direction;
    else {
      // 1) 指向本块的邻近主河道（原河槽带判据，取流量最大者）
      let strictQ=-1;
      for(let z=cz-1;z<=cz+1;z++)for(let x=cx-1;x<=cx+1;x++){
        const up=this.route(x,z);if(!up?.main||up.displaced)continue;
        const d=FLOW_DIRECTIONS[up.direction];
        if(x+d.dx!==cx||z+d.dz!==cz)continue;
        if(up.discharge>strictQ){strictQ=up.discharge;result=up.direction;}
      }
      // 2) 仍未定向时取邻近主河道方向，要求方向与连线同向（水面本身就是这么流的）
      if(result<0){
        let bestQ=-1,bestDist=Infinity;
        for(let z=cz-CORRIDOR_FLOW_RADIUS;z<=cz+CORRIDOR_FLOW_RADIUS;z++)for(let x=cx-CORRIDOR_FLOW_RADIUS;x<=cx+CORRIDOR_FLOW_RADIUS;x++){
          const up=this.route(x,z);if(!up?.main)continue;
          const dist=Math.max(Math.abs(x-cx),Math.abs(z-cz));
          if(dist>bestDist||dist===bestDist&&up.discharge<=bestQ)continue;
          const dx=cx-x,dz=cz-z,length=Math.hypot(dx,dz)||1,d=FLOW_DIRECTIONS[up.direction];
          if((dx*d.dx+dz*d.dz)/length<CORRIDOR_FLOW_ALIGNMENT)continue;
          bestDist=dist;bestQ=up.discharge;result=up.direction;
        }
      }
    }
    this.corridorDirs.set(k,result);return result;
  }
  /** 大河河槽随流量扩展到邻接区块。 */
  corridor(cx:number,cz:number):number{
    cx=this.wrap(cx);const k=key(cx,cz),cached=this.corridors.get(k);if(cached!==undefined)return cached;
    const n=this.node(cx,cz);let discharge=0;
    if(liquidRiverAllowed(n)&&!this.isChannel(cx,cz)){
      const radius=Math.ceil(MAX_RIVER_RADIUS/CHUNK_SIZE)+1;
      for(let z=cz-radius;z<=cz+radius;z++)for(let x=cx-radius;x<=cx+radius;x++){
        const r=this.route(x,z);if(!r?.main)continue;
        const blocks=r.widthBlocks??(r.discharge>=2500?2:1),width=riverWidth(r.discharge,blocks);
        const d=FLOW_DIRECTIONS[r.direction];
        const dx=cx-x,dz=cz-z,t=Math.max(0,Math.min(1,(dx*d.dx+dz*d.dz)/(d.dx*d.dx+d.dz*d.dz)));
        // 河槽带同时要求区块紧邻河道，且水面真正覆盖到该块核心
        if(Math.hypot(dx-t*d.dx,dz-t*d.dz)*CHUNK_SIZE>width+(d.dx&&d.dz?RIVER_BANK_BLEND*4:0)+1e-6)continue;
        const ax=(x+.5)*CHUNK_SIZE,az=(z+.5)*CHUNK_SIZE;
        if(width<=0||r.discharge<=discharge)continue;
        if(coreDistance(cx,cz,ax,az,ax+d.dx*CHUNK_SIZE,az+d.dz*CHUNK_SIZE)>width)continue;
        discharge=r.discharge;this.corridorBlocks.set(k,blocks);
      }
    }
    this.corridors.set(k,discharge);return discharge;
  }
  waterfall(cx:number,cz:number):number{
    cx=this.wrap(cx);const k=key(cx,cz),cached=this.falls.get(k);if(cached!==undefined)return cached;
    const r=this.route(cx,cz);let drop=0;
    if(r?.main){const d=FLOW_DIRECTIONS[r.direction];
      const value=r.level-this.level(cx+d.dx,cz+d.dz);
      if(waterfallDrop(value)&&Math.abs(this.node(cx,cz).height-this.node(this.wrap(cx+d.dx),cz+d.dz).height)<=256)drop=value;
    }
    this.falls.set(k,drop);return drop;
  }
  /** 瀑布落点两岸及缓坡河湾的湿地 */
  wetland(cx:number,cz:number):number{
    cx=this.wrap(cx);const k=key(cx,cz),cached=this.wetlands.get(k);if(cached!==undefined)return cached;
    const n=this.node(cx,cz);if(!liquidRiverAllowed(n)){this.wetlands.set(k,0);return 0;}
    const localFall=this.waterfall(cx,cz);
    if(localFall>0&&localFall<64){this.wetlands.set(k,0);return 0;}
    let strength=this.turn(cx,cz)? .8:0;
    for(let z=cz-5;z<=cz+5;z++)for(let x=cx-5;x<=cx+5;x++){
      const r=this.route(x,z);if(!r?.main)continue;
      const d=FLOW_DIRECTIONS[r.direction],drop=this.waterfall(x,z),length=Math.hypot(d.dx,d.dz);
      const dx=cx-x-d.dx,dz=cz-z-d.dz,along=(dx*d.dx+dz*d.dz)/length,lateral=Math.abs(dx*d.dz-dz*d.dx)/length;
      const lower=this.node(this.wrap(x+d.dx),z+d.dz);
      const grade=Math.max(0,r.level-this.level(x+d.dx,z+d.dz))/64;
      // 被弯折替代的原河格转为湿地。
      if(r.displaced?.cx===cx&&r.displaced.cz===cz)strength=Math.max(strength,.8);
      if(splashWetlandDrop(drop)&&along>=-1&&along<=4&&lateral<3.5&&Math.abs(n.height-lower.height)<=8){
        strength=Math.max(strength,(1-lateral/3.5)*(1-Math.max(0,along)/5));
      }
      const dist=Math.hypot(cx-x,cz-z);
      if(dist<=1.75&&grade<.035&&n.moisture>.35&&n.height<=lower.height+1.5&&n.height>=lower.height-4&&r.discharge>190){
        const patch=Math.max(0,(this.valley?.(cx/1.7,cz/1.7)??0)-.35);
        strength=Math.max(strength,patch*(1-dist/1.75)*1.5);
      }
    }
    this.wetlands.set(k,strength);return strength;
  }
  isWetlandChannel(cx:number,cz:number):boolean{
    const r=this.route(cx,cz);return !!r&&!r.main&&this.wetland(cx,cz)>.12;
  }
  lake(cx: number, cz: number): WatershedLake | null {
    if (!this.precipitation) return null;
    const [gx, gz] = this.watershed.coordinates(this.wrap(cx), cz);
    const lake = this.watershed.cell(gx, gz).lake;
    const route = this.route(cx, cz);
    // 对角河段可擦过另一个盆地的粗网格角
    return lake && route && route.level < lake.level - 1e-6 ? null : lake;
  }
  contains(lake: WatershedLake, cx: number, cz: number): boolean {
    const [gx, gz] = this.watershed.normalize(...this.watershed.coordinates(this.wrap(cx), cz));
    return lake.cells.has(key(gx, gz));
  }
  fan(cx: number, cz: number): AlluvialFan | null {
    const route = this.route(cx, cz);
    if (!route?.apex) return null;
    const a = route.coarse, k = key(a.x, a.z);
    if (this.fans.has(k)) return this.fans.get(k)!;
    let fan: AlluvialFan | null = null;
    if (a.receiver && a.channel && !a.lake && a.discharge > 320) {
      const b = a.receiver, [ax, az] = this.watershed.position(a.x, a.z);
      let [bx, bz] = this.watershed.position(b.x, b.z);
      if (this.circumference) bx = ax + this.wrap(bx - ax);
      const distance = Math.hypot(bx - ax, bz - az) * 64, grade = (a.level - b.level) / distance;
      let upstreamGrade = 0;
      for (const d of FLOW_DIRECTIONS) {
        const n = this.watershed.cell(a.x + d.dx, a.z + d.dz);
        if (n.receiver === a && n.channel) upstreamGrade = Math.max(upstreamGrade,
          (n.level - a.level) / (this.watershed.step * 64 * Math.hypot(d.dx, d.dz)));
      }
      if (grade >= 0 && grade < .16 && (upstreamGrade > Math.max(.035, grade * 1.7) || b.height <= 0)) {
        const root = Math.sqrt(a.discharge);
        fan = {x: (ax + .5) * 64, z: (az + .5) * 64, dx: (bx - ax) * 64 / distance, dz: (bz - az) * 64 / distance,
          level: a.level, grade: Math.max(.002, grade), discharge: a.discharge,
          length: Math.min(1024, 260 + root * 5), width: Math.min(512, 90 + root * 3)};
      }
    }
    this.fans.set(k, fan); return fan;
  }
}
