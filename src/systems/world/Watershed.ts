import { FLOW_DIRECTIONS, effectiveRunoff, liquidRiverAllowed, type DrainageNode } from './Hydrology';
import {snowLine} from './TerrainLayers';
import {mergedRiverSize,type RiverSize} from './RiverSize';

const key = (x: number, z: number) => `${x},${z}`;
/** 相邻粗格水位差超过此值按独立河段处理，否则视为同一水体的内部台�?*/
const WATER_STEP_LIMIT = 6;
export interface WatershedCell {
  x: number; z: number; height: number; level: number; distance: number; rank: number;
  receiver: WatershedCell | null; discharge: number; area: number; order: number; channel: boolean;
  lake: WatershedLake | null;
  weakChannel: boolean;
  riverCapacity:RiverSize['capacity'];riverBlocks:number;
}
export interface WatershedLake { level: number; discharge: number; cells: ReadonlySet<string> }
interface Candidate { cell: WatershedCell; parent: WatershedCell | null; level: number; distance: number }
const before = (a: Candidate, b: Candidate) => a.level - b.level || a.distance - b.distance || a.cell.z - b.cell.z || a.cell.x - b.cell.x;

/** 确定性最小堆；同高边缘以到出口距离和坐标决胜�?*/
class FloodQueue {
  private readonly items: Candidate[] = [];
  get length(): number { return this.items.length; }
  push(value: Candidate): void {
    let i = this.items.length; this.items.push(value);
    while (i) {
      const p = (i - 1) >> 1;
      if (before(this.items[p], value) <= 0) break;
      this.items[i] = this.items[p]; i = p;
    }
    this.items[i] = value;
  }
  pop(): Candidate {
    const first = this.items[0], last = this.items.pop()!;
    if (this.items.length) {
      let i = 0;
      while (i * 2 + 1 < this.items.length) {
        let c = i * 2 + 1;
        if (c + 1 < this.items.length && before(this.items[c + 1], this.items[c]) < 0) c++;
        if (before(last, this.items[c]) <= 0) break;
        this.items[i] = this.items[c]; i = c;
      }
      this.items[i] = last;
    }
    return first;
  }
}

/** 在完整陆块上一次求解排水树 */
export class Watershed {
  readonly step: number;
  private readonly columns: number;
  private readonly rows: number;
  private readonly solved = new Map<string, WatershedCell>();
  private readonly samples = new Map<string, DrainageNode>();
  private readonly districts = new Map<string, string>();
  private readonly largeLand = new Set<string>();
  private readonly barriers=new Map<string,boolean>();
  private readonly fineSamples=new Map<string,DrainageNode>();

  constructor(private readonly sample: (x: number, z: number) => DrainageNode,
    private readonly precipitation: number, readonly threshold: number,
    private readonly circumference?: number, private readonly valley?: (x: number, z: number) => number,
    private readonly boundarySample=sample) {
    this.columns = circumference ? Math.min(512, Math.floor(circumference / 8 / 4) * 4) : 0;
    this.rows = this.columns / 2;
    this.step = circumference ? circumference / this.columns : 8;
  }

  clear(): void { this.solved.clear(); this.samples.clear(); this.districts.clear(); this.largeLand.clear();this.barriers.clear();this.fineSamples.clear(); }

  /** 液态排水连边不得穿过封冻带或单区块超过256格的 */
  private passable(a:WatershedCell,b:WatershedCell):boolean{
    const ka=key(a.x,a.z),kb=key(b.x,b.z),k=ka<kb?`${ka}/${kb}`:`${kb}/${ka}`;
    const cached=this.barriers.get(k);if(cached!==undefined)return cached;
    const ar=this.raw(a.x,a.z) as DrainageNode&{plateBase?:number},br=this.raw(b.x,b.z) as DrainageNode&{plateBase?:number};
    if(ar.height>0&&!liquidRiverAllowed(ar)||br.height>0&&!liquidRiverAllowed(br)){this.barriers.set(k,false);return false;}
    // 同一精确板块底座的内�?
    // 只对底座过渡、海岸大落差及临界雪线做细分检�?
    if((!this.circumference||this.circumference>=4096)&&ar.plateBase!==undefined&&ar.plateBase===br.plateBase&&Math.abs(ar.height-br.height)<128&&
      ar.height<snowLine(ar.temperature)-160&&br.height<snowLine(br.temperature)-160){this.barriers.set(k,true);return true;}
    let [ax,az]=this.position(a.x,a.z),[bx,bz]=this.position(b.x,b.z);
    if(this.circumference)bx=ax+((bx-ax+this.circumference/2)%this.circumference+this.circumference)%this.circumference-this.circumference/2;
    const steps=Math.max(Math.abs(bx-ax),Math.abs(bz-az));let previous:DrainageNode|null=null,allowed=true;
    for(let i=0;i<=steps;i++){
      let x=Math.round(ax+(bx-ax)*i/steps);const z=Math.round(az+(bz-az)*i/steps);
      if(this.circumference)x=((x+this.circumference/2)%this.circumference+this.circumference)%this.circumference-this.circumference/2;
      const fk=key(x,z);let n=this.fineSamples.get(fk);
      if(!n){n=this.boundarySample(x,z);this.fineSamples.set(fk,n);}
      if(n.height>0&&!liquidRiverAllowed(n)||previous&&Math.abs(previous.height-n.height)>256){allowed=false;break;}
      previous=n;
    }
    this.barriers.set(k,allowed);return allowed;
  }

  coordinates(x: number, z: number): [number, number] {
    return [Math.floor((x + (this.circumference ?? 0) / 2) / this.step),
      Math.floor((z + (this.circumference ?? 0) / 4) / this.step)];
  }
  position(x: number, z: number): [number, number] {
    return [Math.floor((x + .5) * this.step - (this.circumference ?? 0) / 2),
      Math.floor((z + .5) * this.step - (this.circumference ?? 0) / 4)];
  }
  normalize(x: number, z: number): [number, number] {
    if (!this.circumference) return [x, z];
    if (z < 0) { z = -z - 1; x += this.columns / 2; }
    if (z >= this.rows) { z = this.rows * 2 - z - 1; x += this.columns / 2; }
    return [((x % this.columns) + this.columns) % this.columns, z];
  }
  private neighbors(c: {x: number; z: number}): [number, number][] {
    return FLOW_DIRECTIONS.filter(d => !this.circumference || c.z + d.dz >= 0 && c.z + d.dz < this.rows)
      .map(d => this.normalize(c.x + d.dx, c.z + d.dz));
  }
  /** 弯曲的有�?Voronoi 分水�?*/
  private district(x: number, z: number): string {
    const k = key(x, z), cached = this.districts.get(k);
    if (cached) return cached;
    const px = x + Math.sin(z / 37 + Math.sin(x / 83)) * 14;
    const pz = z + Math.sin(x / 43 + Math.sin(z / 79)) * 14;
    const ix = Math.floor(px / 96), iz = Math.floor(pz / 96);
    let best = Infinity, owner = '';
    for (let dz = -1; dz <= 1; dz++) for (let dx = -1; dx <= 1; dx++) {
      const cx = ix + dx, cz = iz + dz;
      let hash = Math.imul(cx, 73856093) ^ Math.imul(cz, 19349663);
      hash = Math.imul(hash ^ hash >>> 16, 0x45d9f3b); hash ^= hash >>> 16;
      const sx = (cx + .5) * 96 + ((hash & 65535) / 65535 - .5) * 40;
      const sz = (cz + .5) * 96 + ((hash >>> 16) / 65535 - .5) * 40;
      const distance = (px - sx) ** 2 + (pz - sz) ** 2;
      if (distance < best) { best = distance; owner = key(cx, cz); }
    }
    this.districts.set(k, owner); return owner;
  }
  private raw(x: number, z: number): DrainageNode {
    const k = key(x, z), cached = this.samples.get(k);
    if (cached) return cached;
    const [cx, cz] = this.position(x, z), n = this.sample(cx, cz);
    this.samples.set(k, n); return n;
  }
  cell(x: number, z: number): WatershedCell {
    [x, z] = this.normalize(x, z);
    const k = key(x, z), cached = this.solved.get(k);
    if (cached) return cached;
    if (this.raw(x, z).height <= 0) {
      const sea = this.make(x, z); sea.level = 0; sea.rank = 0;
      this.solved.set(k, sea); return sea;
    }
    this.build(x, z);
    return this.solved.get(k)!;
  }
  private make(x: number, z: number): WatershedCell {
    return {x, z, height: this.raw(x, z).height, level: Infinity, distance: Infinity, rank: -1,
      receiver: null, discharge: 0, area: 0, order: 1, channel: false, weakChannel:false, lake: null,riverCapacity:1,riverBlocks:1};
  }
  private incision(c: WatershedCell): number {
    if (!this.valley) return 0;
    const [x, z] = this.position(c.x, c.z);
    return Math.max(0, Math.min(1, (this.valley(x, z) + 1) * .5)) * 96;
  }

  private build(x: number, z: number): void {
    let cells = new Map<string, WatershedCell>(), pending: [number, number][] = [[x, z]];
    const district = this.circumference ? '' : this.district(x, z);
    let bounded = !this.circumference && this.largeLand.has(key(x, z));
    const inside = (nx: number, nz: number) => !bounded || this.district(nx, nz) === district;
    cells.set(key(x, z), this.make(x, z));
    for (let i = 0; i < pending.length; i++) {
      if (!this.circumference && !bounded && pending.length > 12000) {
        // 小大陆完整求�?
        for (const [px, pz] of pending) this.largeLand.add(key(px, pz));
        bounded = true; cells = new Map([[key(x, z), this.make(x, z)]]); pending = [[x, z]]; i = -1; continue;
      }
      const [cx, cz] = pending[i];
      for (const [nx, nz] of this.neighbors({x: cx, z: cz})) {
        const k = key(nx, nz);
        if (cells.has(k) || !inside(nx, nz)) continue;
        const c = this.make(nx, nz); cells.set(k, c);
        if (c.height > 0) pending.push([nx, nz]);
      }
    }
    const queue = new FloodQueue(), sorted: WatershedCell[] = [];
    for (const c of cells.values()) if (c.height <= 0) {
      c.level = 0; c.distance = 0; queue.push({cell: c, parent: null, level: 0, distance: 0});
    }
    if (!queue.length) {
      // 无海洋的区域以最低点为内流湖
      const sink = [...cells.values()].sort((a, b) => a.height - b.height || a.z - b.z || a.x - b.x)[0];
      sink.level = Math.max(0, sink.height + 6); sink.distance = 0;
      queue.push({cell: sink, parent: null, level: sink.level, distance: 0});
    }
    // 陡坎/封冻屏障会划出内流盆�?
    const unsolved=[...cells.values()].filter(c=>c.height>0).sort((a,b)=>a.height-b.height||a.z-b.z||a.x-b.x);
    let cursor=0;
    while (true) {
      if(!queue.length){
        while(cursor<unsolved.length&&unsolved[cursor].rank>=0)cursor++;
        if(cursor===unsolved.length)break;
        const sink=unsolved[cursor++];sink.level=Math.max(0,sink.height+6);sink.distance=0;
        queue.push({cell:sink,parent:null,level:sink.level,distance:0});
      }
      const item = queue.pop(), c = item.cell;
      if (c.rank >= 0 || item.level !== c.level || item.distance !== c.distance) continue;
      c.receiver = item.parent; c.rank = sorted.length; sorted.push(c);
      // 已处理的对角边若会交�?
      if (c.receiver && c.x !== c.receiver.x && c.z !== c.receiver.z) {
        const a = cells.get(key(c.x, c.receiver.z)), b = cells.get(key(c.receiver.x, c.z));
        if (a?.receiver === b && b && b.rank >= 0 && b.level <= c.level && this.passable(c,b)) c.receiver = b;
        else if (b?.receiver === a && a && a.rank >= 0 && a.level <= c.level && this.passable(c,a)) c.receiver = a;
      }
      for (const [nx, nz] of this.neighbors(c)) {
        const n = cells.get(key(nx, nz));
        if (!n || n.rank >= 0 || n.height <= 0 || !this.passable(c,n)) continue;
        const level = Math.max(c.level, n.height - 1.2 - this.incision(n));
        const distance = c.distance + (nx !== c.x && nz !== c.z ? Math.SQRT2 : 1);
        if (level < n.level || level === n.level && distance < n.distance) {
          n.level = level; n.distance = distance;
          queue.push({cell: n, parent: c, level, distance});
        }
      }
    }
    const area = this.step * this.step;
    // 第一遍以默认降水选择主谷�?
    for (const c of sorted) c.discharge = effectiveRunoff(this.raw(c.x,c.z), .5) * (c.height > 0 ? area : 0);
    for (let i = sorted.length - 1; i >= 0; i--) {
      const c = sorted[i]; if (c.receiver) c.receiver.discharge += c.discharge;
    }
    for (const c of sorted) {
      if (!c.receiver) continue;
      const original = c.receiver;
      let steepest = 1;
      for (const [nx,nz] of this.neighbors(c)) {
        const n = cells.get(key(nx,nz));
        if (n && n.rank < c.rank) steepest = Math.max(steepest,c.level-n.level);
      }
      let score = -Infinity;
      for (const [nx,nz] of this.neighbors(c)) {
        const n = cells.get(key(nx,nz));
        if (!n || n.rank >= c.rank || n.level > c.level || !this.passable(c,n)) continue;
        const distance = nx !== c.x && nz !== c.z ? Math.SQRT2 : 1;
        const incoming = Math.max(0,n.discharge - (n === original ? c.discharge : 0));
        const valley = (this.incision(n) - this.incision(c)) / 96;
        const candidate = (c.level-n.level)/steepest/distance * .8 + Math.log1p(incoming/area)*2.2 + valley*2 - distance*.12;
        if (candidate > score) {score = candidate; c.receiver = n;}
      }
      // 改接后仍只允许向已处理节点流�?
      if (c.receiver && c.x !== c.receiver.x && c.z !== c.receiver.z) {
        const a = cells.get(key(c.x,c.receiver.z)), b = cells.get(key(c.receiver.x,c.z));
        if (a?.receiver === b && b && b.rank < c.rank && b.level <= c.level && this.passable(c,b)) c.receiver = b;
        else if (b?.receiver === a && a && a.rank < c.rank && a.level <= c.level && this.passable(c,a)) c.receiver = a;
      }
    }
    for (const c of sorted) {
      c.area = c.height > 0 ? area : 0;
      c.discharge = effectiveRunoff(this.raw(c.x, c.z), this.precipitation) * c.area;
    }
    const orders = new Map<WatershedCell, number[]>();
    const riverSizes=new Map<WatershedCell,RiverSize[]>();
    for (let i = sorted.length - 1; i >= 0; i--) {
      const c = sorted[i], incoming = orders.get(c) ?? [];
      const max = incoming.length ? Math.max(...incoming) : 1;
      c.order = max + (incoming.filter(v => v === max).length > 1 ? 1 : 0);
      // 面积和来水沿 receiver 单调增长
      const eligible=this.precipitation>0&&liquidRiverAllowed(this.raw(c.x,c.z))&&c.area>=area*4;
      c.channel = eligible && c.discharge >= this.threshold;
      c.weakChannel=eligible&&c.discharge>=84;
      const size=mergedRiverSize(c.discharge,riverSizes.get(c)??[]);
      c.riverCapacity=size.capacity;c.riverBlocks=size.blocks;
      if (c.receiver) {
        c.receiver.area += c.area; c.receiver.discharge += c.discharge;
        const list = orders.get(c.receiver) ?? []; list.push(c.order); orders.set(c.receiver, list);
        if(c.channel){const sizes=riverSizes.get(c.receiver)??[];sizes.push(size);riverSizes.set(c.receiver,sizes);}
      }
    }
    // 深洼地采用刻蚀溢出�?
    // 逆拓扑传播上游水位上�?
    for (let i = sorted.length - 1; i >= 0; i--) {
      const c = sorted[i];
      if (c.height <= 0) continue;
      const retention = Math.min(18, 4 + Math.sqrt(c.discharge) * .15);
      c.level = Math.min(c.level, c.height + retention);
      if (c.receiver && c.receiver.height > 0) c.receiver.level = Math.min(c.receiver.level, c.level);
    }
    // 水面不得高于下游出口；陡坎保留落差，缓坡上的水位反升是同一水体的内部台�?
    for (let pass = 0; pass < 3; pass++) for (let i = 0; i < sorted.length; i++) {
      const c = sorted[i];
      if (!c.receiver || c.receiver.height <= 0 || c.height <= 0) continue;
      if (c.level - c.receiver.level <= WATER_STEP_LIMIT) c.level = c.receiver.level;
    }
    // 同一溢流水位下的连通洼地形成一个静水湖
    const visited = new Set<WatershedCell>();
    for (const c of sorted) {
      const raw=this.raw(c.x,c.z);
      // 封冻/临界区不靠来水生成河�?
      const staticPond=!liquidRiverAllowed(raw)&&raw.moisture>-.2&&this.neighbors(c).every(([x,z])=>this.raw(x,z).height>c.height+.5);
      if (visited.has(c) || c.height <= 0 || c.level - c.height < .5 || c.discharge < this.threshold&&!staticPond) continue;
      const lakeCells = [c]; visited.add(c);
      for (let i = 0; i < lakeCells.length; i++) for (const [nx, nz] of this.neighbors(lakeCells[i])) {
        const n = cells.get(key(nx, nz));
        if (n && !visited.has(n) && n.height > 0 && n.level === c.level && n.height < c.level) {
          visited.add(n); lakeCells.push(n);
        }
      }
      const lake = {level: c.level, discharge: lakeCells.reduce((maximum, n) => Math.max(maximum, n.discharge), 0),
        cells: new Set(lakeCells.map(n => key(n.x, n.z)))};
      for (const n of lakeCells) n.lake = lake;
    }
    for (const c of sorted) if (c.height > 0) this.solved.set(key(c.x, c.z), c);
    // 陆块结果保留
    this.samples.clear();
    this.districts.clear();
    this.barriers.clear();this.fineSamples.clear();
  }
}
