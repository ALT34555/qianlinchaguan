import { FLOW_DIRECTIONS, effectiveRunoff, liquidRiverAllowed, type DrainageNode } from './Hydrology';
import {mergedRiverSize,type RiverSize} from './RiverSize';

const key = (x: number, z: number) => `${x},${z}`;
/** 球面整数键 z<<16|x 的取值空间 */
const SAMPLE_SLOTS = 1 << 16;
/** 相邻水位阶梯阈值，超限算独立河段 */
const WATER_STEP_LIMIT = 6;
const FROZEN_OUTLET_PENALTY = 6;
export interface WatershedCell {
  x: number; z: number; height: number; level: number; distance: number; rank: number;
  receiver: WatershedCell | null; discharge: number; area: number; order: number; channel: boolean;
  lake: WatershedLake | null;
  weakChannel: boolean;
  riverCapacity:RiverSize['capacity'];riverBlocks:number;
  /** 球面整数键，仅内部使用 */
  at: number;
}
export interface WatershedLake { level: number; discharge: number; cells: ReadonlySet<string> }
/** 可跨 Worker 传递的粗格样本包 */
export interface WatershedSamples {
  slots: number; rows: number; cols: number;
  heights: Float64Array; moistures: Float64Array; temperatures: Float64Array; plateBases: Float64Array;
  edgeKeys: Int32Array; edgeHeights: Float64Array;
}
export interface WatershedState {
  samples: WatershedSamples;
  values: Float64Array;
  links: Int32Array;
  flags: Uint8Array;
  lakeOffsets: Uint32Array;
  lakeCells: Int32Array;
  lakeValues: Float64Array;
}
interface Candidate { cell: WatershedCell; parent: WatershedCell | null; level: number; distance: number }
const before = (a: Candidate, b: Candidate) => a.level - b.level || a.distance - b.distance || a.cell.z - b.cell.z || a.cell.x - b.cell.x;

/** 确定性最小堆优先级队列 */
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

/** 每个大循环步的检查间隔，控制让出频率 */
const YIELD_EVERY = 8;

/** 在完整陆块上一次求解排水树 */
export class Watershed {
  readonly step: number;
  private readonly columns: number;
  private readonly rows: number;
  private readonly solved = new Map<number | string, WatershedCell>();
  private readonly samples = new Map<string, DrainageNode>();
  private readonly districts = new Map<string, string>();
  private readonly largeLand = new Set<number | string>();
  private readonly barriers=new Map<number | string,boolean>();
  private readonly fineSamples=new Map<string|number,number>();
  /** 球面键可用位运算整数，省去逐格字符串分配 */
  private readonly numericKey: boolean;
  private readonly cols: number;
  /** 球面粗网格样本：整数键常驻，避免重复消耗地形噪声 */
  private readonly grid = new Map<number, DrainageNode & {plateBase?: number}>();
  /** 外来粗格样本，首次使用时解码 */
  private pendingSamples: WatershedSamples | null = null;
  /** 预热求解游标：取用时才把整颗星球解完 */
  private warm: { x: number; z: number } | null = null;
  private warmStep: Iterator<void> | null = null;
  private packed?: WatershedState;
  private packedIndex?: Int32Array;
  private packedCount = 0;
  private readonly packedCells = new Map<number,WatershedCell>();
  private readonly packedLakes = new Map<number,WatershedLake>();
  private packedIds = new WeakMap<WatershedCell,number>();
  private packedLakeIds = new WeakMap<WatershedLake,number>();
  private stepDeadline = 0;

  /** 预热当前粗格所在的星球，取用时一次解完 */
  startWarmGrid(x: number, z: number): void {
    if (!this.numericKey || this.solvedCells) return;
    this.warm = { x, z };
  }

  /** 预热是否已完成；未登记预热时视为已完成 */
  get warmDone(): boolean { return this.warm === null; }

  /** 按时间预算推进预热；返回是否已完成 */
  pumpWarm(budgetMs: number): boolean {
    const warm = this.warm;
    if (!warm) return true;
    if (!this.warmStep) this.warmStep = this.buildAsync(warm.x, warm.z);
    const deadline = performance.now() + budgetMs;
    this.stepDeadline = deadline;
    while (true) {
      if (this.warmStep.next().done === true) { this.warm = null; this.warmStep = null; return true; }
      if (performance.now() >= deadline) return false;
    }
  }

  /** 预热未完成时，取用前必须同步解完 */
  private settleWarm(): void {
    if (!this.warm) return;
    if(this.warmStep){
      this.stepDeadline=Infinity;
      while(!this.warmStep.next().done){ /* 沿原游标完成 */ }
      this.warm=null;this.warmStep=null;
      return;
    }
    this.warmStep = null;
    const warm = this.warm;
    this.warm = null;
    this.build(warm.x, warm.z);
  }
  /** 邻居暂存，替代逐次数组分配 */
  private readonly neighborKeys = new Int32Array(8);
  private readonly neighborX = new Int32Array(8);
  private readonly neighborZ = new Int32Array(8);
  private cachedCx = NaN;
  private cachedCz = NaN;
  private cachedSpherical = false;
  private cachedCount = 0;

  constructor(private readonly sample: (x: number, z: number) => DrainageNode,
    private readonly precipitation: number, readonly threshold: number,
    private readonly circumference?: number, private readonly valley?: (x: number, z: number) => number,
    private readonly boundarySample=sample) {
    this.columns = circumference ? Math.min(512, Math.floor(circumference / 8 / 4) * 4) : 0;
    this.rows = this.columns / 2;
    this.step = circumference ? circumference / this.columns : 8;
    // 键位宽受格数限制，超大星球退回字符串键
    this.numericKey = this.columns > 0 && this.columns < 32768 && this.rows < 32768;
    this.cols = this.columns;
  }

  clear(): void { this.packed=undefined;this.packedIndex=undefined;this.packedCount=0;this.packedCells.clear();this.packedLakes.clear();this.packedIds=new WeakMap();this.packedLakeIds=new WeakMap();this.pendingSamples=null;this.solved.clear(); this.samples.clear(); this.districts.clear(); this.largeLand.clear();this.barriers.clear();this.fineSamples.clear();this.grid.clear(); }
  get solvedCells():number{return this.solved.size+this.packedCount;}
  /** 球面拓扑常驻，平面按插入顺序淘汰 */
  trim(keep = .75): void {
    if(this.numericKey)return;
    const solved = Math.floor(this.solved.size * keep);
    while (this.solved.size > solved) this.solved.delete(this.solved.keys().next().value!);
    const land = Math.floor(this.largeLand.size * keep);
    while (this.largeLand.size > land) this.largeLand.delete(this.largeLand.keys().next().value!);
  }

  /** 球面整数键：极区折叠后 x 落在 0..columns-1 */
  private skey(x: number, z: number): number { return (z << 16) | x; }

  /** 导出已解粗格样本，供其它实例复用同一份地形采样 */
  exposeSamples(): WatershedSamples {
    const slots = this.rows * this.cols;
    const heights = this.pendingSamples?.heights.slice() ?? new Float64Array(slots).fill(NaN);
    const moistures = this.pendingSamples?.moistures.slice() ?? new Float64Array(slots);
    const temperatures = this.pendingSamples?.temperatures.slice() ?? new Float64Array(slots);
    const plateBases = this.pendingSamples?.plateBases.slice() ?? new Float64Array(slots).fill(NaN);
    for (let z = 0; z < this.rows; z++) for (let x = 0; x < this.cols; x++) {
      const sampled = this.grid.get(this.skey(x, z));
      if (!sampled) continue;
      const at = z * this.cols + x;
      heights[at] = sampled.height; moistures[at] = sampled.moisture; temperatures[at] = sampled.temperature;
      const plate = (sampled as { plateBase?: number }).plateBase;
      if (plate !== undefined) plateBases[at] = plate;
    }
    return { slots, rows: this.rows, cols: this.cols, heights, moistures, temperatures, plateBases, ...this.exposeEdgeSamples() };
  }

  /** 取用外部粗格样本：只解码一格，避免一次性分配 */
  adoptSamples(batch: WatershedSamples): void {
    if (batch.slots !== this.rows * this.cols || batch.heights.length !== batch.slots ||
        batch.rows !== this.rows || batch.cols !== this.cols) return;
    this.pendingSamples = batch;
    this.mergeEdgeSamples(batch.edgeKeys, batch.edgeHeights);
  }

  /** 边采样使用完整坐标对 */
  mergeEdgeSamples(edgeKeys: Int32Array, edgeHeights: Float64Array): void {
    if(edgeKeys.length!==edgeHeights.length*2)throw new Error('边采样坐标无效');
    for (let i = 0; i < edgeHeights.length; i++) {
      const x = edgeKeys[i*2], z = edgeKeys[i*2+1];
      this.fineSamples.set(this.fineKey(x,z),edgeHeights[i]);
    }
  }

  /** 导出边检查缓存，供 Worker 复用同一份细采样 */
  exposeEdgeSamples(): { edgeKeys: Int32Array; edgeHeights: Float64Array } {
    const size = this.fineSamples.size;
    const edgeKeys = new Int32Array(size*2), edgeHeights = new Float64Array(size);
    let i = 0;
    for (const [sampleKey, sampled] of this.fineSamples) {
      if(typeof sampleKey==='number'){
        const n=this.circumference!,x=((sampleKey+n/2)%n+n)%n-n/2;
        edgeKeys[i*2]=x;edgeKeys[i*2+1]=(sampleKey-x)/n;
      }else{
        const comma=sampleKey.indexOf(',');edgeKeys[i*2]=Number(sampleKey.slice(0,comma));edgeKeys[i*2+1]=Number(sampleKey.slice(comma+1));
      }
      edgeHeights[i] = sampled;
      i++;
    }
    return { edgeKeys, edgeHeights };
  }

  private edgeKey(a:number,b:number):number { return Math.min(a,b)*SAMPLE_SLOTS+Math.max(a,b); }
  private fineKey(x:number,z:number):number|string{return this.circumference?z*this.circumference+x:key(x,z);}

  /** 校验液态排水连边连通性 */
  private passable(a:WatershedCell,b:WatershedCell):boolean{
    const k=this.numericKey?this.edgeKey(a.at,b.at):key(a.x,a.z)<key(b.x,b.z)?`${key(a.x,a.z)}/${key(b.x,b.z)}`:`${key(b.x,b.z)}/${key(a.x,a.z)}`;
    const cached=this.barriers.get(k);if(cached!==undefined)return cached;
    const ar=this.raw(a.x,a.z) as DrainageNode&{plateBase?:number},br=this.raw(b.x,b.z) as DrainageNode&{plateBase?:number};
    // 同板块内仅过渡与大落差做细分检查
    if((!this.circumference||this.circumference>=4096)&&ar.plateBase!==undefined&&ar.plateBase===br.plateBase&&Math.abs(ar.height-br.height)<128){this.barriers.set(k,true);return true;}
    let [ax,az]=this.position(a.x,a.z),[bx,bz]=this.position(b.x,b.z);
    if(this.circumference)bx=ax+((bx-ax+this.circumference/2)%this.circumference+this.circumference)%this.circumference-this.circumference/2;
    const steps=Math.max(Math.abs(bx-ax),Math.abs(bz-az));let previous:number|undefined,allowed=true;
    for(let i=0;i<=steps;i++){
      let x=Math.round(ax+(bx-ax)*i/steps);const z=Math.round(az+(bz-az)*i/steps);
      if(this.circumference)x=((x+this.circumference/2)%this.circumference+this.circumference)%this.circumference-this.circumference/2;
      const fk=this.fineKey(x,z);let n=this.fineSamples.get(fk);
      if(n===undefined){n=this.boundarySample(x,z).height;this.fineSamples.set(fk,n);}
      if(previous!==undefined&&Math.abs(previous-n)>256){allowed=false;break;}
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

  /** 按需采样一格并写入常驻网格，避免重复消耗地形噪声 */
  private sampleAt(at: number, x: number, z: number): DrainageNode & {plateBase?: number} {
    if(this.pendingSamples&&!this.packed)this.decodeSamples(this.pendingSamples);
    const cached = this.grid.get(at);
    if (cached) return cached;
    const batch = this.pendingSamples;
    if(batch){
      const offset=z*this.cols+x,height=batch.heights[offset];
      if(Number.isFinite(height)){
        const sampled:DrainageNode&{plateBase?:number}={height,moisture:batch.moistures[offset],temperature:batch.temperatures[offset]};
        if(Number.isFinite(batch.plateBases[offset]))sampled.plateBase=batch.plateBases[offset];
        if(this.grid.size>=4096)this.grid.delete(this.grid.keys().next().value!);
        this.grid.set(at,sampled);return sampled;
      }
    }
    const [px, pz] = this.position(x, z), sampled = this.sample(px, pz);
    this.grid.set(at, sampled);
    return sampled;
  }
  private decodeSamples(batch:WatershedSamples):void {
    for(let i=0;i<batch.slots;i++){
      const height=batch.heights[i];if(height!==height)continue;
      const node:DrainageNode&{plateBase?:number}={height,moisture:batch.moistures[i],temperature:batch.temperatures[i]};
      if(batch.plateBases[i]===batch.plateBases[i])node.plateBase=batch.plateBases[i];
      this.grid.set(this.skey(i%this.cols,Math.floor(i/this.cols)),node);
    }
    this.pendingSamples=null;
  }

  private raw(x: number, z: number): DrainageNode & {plateBase?: number} {
    if (!this.numericKey) {
      const k = key(x, z), cached = this.samples.get(k);
      if (cached) return cached;
      const [cx, cz] = this.position(x, z), n = this.sample(cx, cz);
      this.samples.set(k, n); return n;
    }
    // 直接返回采样对象，plateBase 等字段一并保留
    return this.sampleAt(this.skey(x, z), x, z);
  }

  /** 写出 8 个邻居的键与坐标，替代逐次数组分配 */
  private fillNeighbors(cx: number, cz: number, spherical: boolean): number {
    // 已算过同样输入时直接复用，热循环里按 c 连续四次调用
    if (cx === this.cachedCx && cz === this.cachedCz && spherical === this.cachedSpherical) return this.cachedCount;
    const keys = this.neighborKeys, xs = this.neighborX, zs = this.neighborZ;
    let count = 0;
    for (let i = 0; i < 8; i++) {
      const d = FLOW_DIRECTIONS[i], nx = cx + d.dx, nz = cz + d.dz;
      if (spherical) {
        // 极区按列折叠，与 normalize 同式
        let x = nx, z = nz;
        if (z < 0) { z = -z - 1; x += this.cols / 2; }
        else if (z >= this.rows) { z = this.rows * 2 - z - 1; x += this.cols / 2; }
        x = ((x % this.cols) + this.cols) % this.cols;
        keys[count] = this.skey(x, z); xs[count] = x; zs[count] = z; count++;
      } else {
        xs[count] = nx; zs[count] = nz; count++;
      }
    }
    this.cachedCx = cx; this.cachedCz = cz; this.cachedSpherical = spherical; this.cachedCount = count;
    return count;
  }

  cell(x: number, z: number): WatershedCell {    [x, z] = this.normalize(x, z);
    const k = this.numericKey ? this.skey(x, z) : key(x, z), cached = this.solved.get(k);
    if (cached) return cached;
    const packed=this.packedIndex?.[z*this.cols+x];
    if(packed!==undefined&&packed>=0)return this.packedCell(packed);
    if (this.raw(x, z).height <= 0) {
      const sea = this.make(x, z); sea.level = 0; sea.rank = 0;
      this.solved.set(k, sea); return sea;
    }
    this.build(x, z);
    return this.solved.get(k)!;
  }
  private make(x: number, z: number): WatershedCell {
    return {x, z, height: this.raw(x, z).height, level: Infinity, distance: Infinity, rank: -1,
      receiver: null, discharge: 0, area: 0, order: 1, channel: false, weakChannel:false, lake: null,riverCapacity:1,riverBlocks:1,
      at: this.skey(x, z)};
  }
  /** 弯曲的 Voronoi 分水区 */
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
  private incisionAt(x: number, z: number): number {
    if (!this.valley) return 0;
    const [px, pz] = this.position(x, z);
    return Math.max(0, Math.min(1, (this.valley(px, pz) + 1) * .5)) * 96;
  }
  private incision(c: WatershedCell): number { return this.incisionAt(c.x, c.z); }
  private outletPotential(x: number, z: number): { bed: number; potential: number } {
    const raw = this.raw(x, z);
    const bed = raw.height <= 0 ? 0 : Math.max(0, raw.height - 1.2 - this.incisionAt(x, z));
    return { bed, potential: raw.height + (liquidRiverAllowed(raw) ? 0 : FROZEN_OUTLET_PENALTY) };
  }
  /** 域边界外向放水口：外部河床即出口水位 */
  private *outlets(cells: Map<number | string, WatershedCell>, queue: FloodQueue,allowMore:()=>boolean):Generator<void> {
    let steps=0;
    for (const c of cells.values()) {
      if((steps++&7)===0&&!allowMore())yield;
      if (c.height <= 0) continue;
      const own = this.outletPotential(c.x, c.z);
      let exit: WatershedCell | null = null, exitPotential = Infinity;
      const count = this.fillNeighbors(c.x, c.z, this.numericKey);
      for (let i = 0; i < count; i++) {
        const nx = this.neighborX[i], nz = this.neighborZ[i];
        const nk = this.numericKey ? this.neighborKeys[i] : key(nx, nz);
        if (cells.has(nk)) continue;
        const target = this.outletPotential(nx, nz);
        if (target.potential > own.potential) continue;
        if (target.potential === own.potential && (nx > c.x || nx === c.x && nz > c.z)) continue;
        if (target.potential >= exitPotential) continue;
        exitPotential = target.potential;
        exit = {...this.make(nx, nz), level: target.bed, channel: true, weakChannel: true};
      }
      if (!exit) continue;
      const level = Math.max(own.bed, exit.level);
      if (level >= c.level) continue;
      c.level = level; c.distance = 0;
      queue.push({cell: c, parent: exit, level, distance: 0});
    }
  }

  private build(x: number, z: number): void {
    if (this.warm) this.settleWarm();
    if (this.solved.has(this.numericKey ? this.skey(x, z) : key(x, z))) return;
    const step = this.solve(x, z, () => true);
    while (!step.next().done) { /* 同步模式一次跑完 */ }
  }

  private packedCell(index:number):WatershedCell {
    const cached=this.packedCells.get(index);if(cached)return cached;
    const state=this.packed!,v=state.values,o=index*12,link=state.links[index*2],lake=state.links[index*2+1];
    const owner=this;
    const cell:WatershedCell={x:v[o],z:v[o+1],height:v[o+2],level:v[o+3],distance:v[o+4],rank:v[o+5],discharge:v[o+6],area:v[o+7],order:v[o+8],riverCapacity:v[o+9] as RiverSize['capacity'],riverBlocks:v[o+10],at:v[o+11],channel:!!(state.flags[index]&1),weakChannel:!!(state.flags[index]&2),
      get receiver(){return link<0?null:owner.packedCell(link);},get lake(){return lake<0?null:owner.packedLake(lake);}};
    if(this.packedCells.size>=4096)this.packedCells.delete(this.packedCells.keys().next().value!);
    this.packedCells.set(index,cell);this.packedIds.set(cell,index);return cell;
  }
  private packedLake(index:number):WatershedLake {
    const cached=this.packedLakes.get(index);if(cached)return cached;
    const state=this.packed!,members=new Set<string>();
    for(let i=state.lakeOffsets[index];i<state.lakeOffsets[index+1];i++)members.add(key(state.lakeCells[i*2],state.lakeCells[i*2+1]));
    const lake={level:state.lakeValues[index*2],discharge:state.lakeValues[index*2+1],cells:members};
    this.packedLakes.set(index,lake);this.packedLakeIds.set(lake,index);return lake;
  }
  private mergePackedState():WatershedState {
    const state=this.packed!;if(!this.solved.size)return state;
    const base=state.flags.length,roots=new Set(this.solved.values()),cells=[...roots];
    const index=new Map(cells.map((cell,i)=>[cell,base+i]));
    for(let i=0;i<cells.length;i++){
      const receiver=cells[i].receiver;
      if(receiver&&!this.packedIds.has(receiver)&&!index.has(receiver)){index.set(receiver,base+cells.length);cells.push(receiver);}
    }
    const lakes=[...new Set(cells.flatMap(c=>c.lake&&!this.packedLakeIds.has(c.lake)?[c.lake]:[]))];
    const lakeBase=state.lakeValues.length/2,lakeIndex=new Map(lakes.map((lake,i)=>[lake,lakeBase+i]));
    const values=new Float64Array((base+cells.length)*12),links=new Int32Array((base+cells.length)*2).fill(-1),flags=new Uint8Array(base+cells.length);
    values.set(state.values);links.set(state.links);flags.set(state.flags);
    cells.forEach((c,j)=>{const i=base+j;values.set([c.x,c.z,c.height,c.level,c.distance,c.rank,c.discharge,c.area,c.order,c.riverCapacity,c.riverBlocks,c.at],i*12);
      links[i*2]=c.receiver?(this.packedIds.get(c.receiver)??index.get(c.receiver)!):-1;
      links[i*2+1]=c.lake?(this.packedLakeIds.get(c.lake)??lakeIndex.get(c.lake)!):-1;
      flags[i]=(c.channel?1:0)|(c.weakChannel?2:0)|(roots.has(c)?4:0);
    });
    const lakeOffsets=new Uint32Array(lakeBase+lakes.length+1),lakeValues=new Float64Array((lakeBase+lakes.length)*2);
    lakeOffsets.set(state.lakeOffsets);lakeValues.set(state.lakeValues);
    let count=state.lakeCells.length/2;
    lakes.forEach((lake,i)=>{lakeOffsets[lakeBase+i]=count;count+=lake.cells.size;lakeValues.set([lake.level,lake.discharge],(lakeBase+i)*2);});lakeOffsets[lakeOffsets.length-1]=count;
    const lakeCells=new Int32Array(count*2);lakeCells.set(state.lakeCells);count=state.lakeCells.length/2;
    for(const lake of lakes)for(const k of lake.cells){lakeCells.set(k.split(',').map(Number),count*2);count++;}
    return {samples:this.exposeSamples(),values,links,flags,lakeOffsets,lakeValues,lakeCells};
  }
  exportState(): WatershedState {
    if(this.packed)return this.mergePackedState();
    const roots = new Set(this.solved.values());
    const cells = [...roots], index = new Map(cells.map((c, i) => [c, i]));
    for (let i = 0; i < cells.length; i++) {
      const c = cells[i].receiver;
      if (c && !index.has(c)) { index.set(c, cells.length); cells.push(c); }
    }
    const lakes = [...new Set(cells.flatMap(c => c.lake ? [c.lake] : []))];
    const lakeIndex = new Map(lakes.map((lake, i) => [lake, i]));
    const values = new Float64Array(cells.length * 12);
    const links = new Int32Array(cells.length * 2).fill(-1), flags = new Uint8Array(cells.length);
    cells.forEach((c, i) => {
      values.set([c.x,c.z,c.height,c.level,c.distance,c.rank,c.discharge,c.area,c.order,c.riverCapacity,c.riverBlocks,c.at], i * 12);
      links[i * 2] = c.receiver ? index.get(c.receiver)! : -1;
      links[i * 2 + 1] = c.lake ? lakeIndex.get(c.lake)! : -1;
      flags[i] = (c.channel ? 1 : 0) | (c.weakChannel ? 2 : 0) | (roots.has(c) ? 4 : 0);
    });
    const lakeOffsets = new Uint32Array(lakes.length + 1), lakeValues = new Float64Array(lakes.length * 2);
    let count = 0;
    lakes.forEach((lake, i) => { lakeOffsets[i] = count; count += lake.cells.size; lakeValues.set([lake.level,lake.discharge], i * 2); });
    lakeOffsets[lakes.length] = count;
    const lakeCells = new Int32Array(count * 2);
    count = 0;
    for (const lake of lakes) for (const k of lake.cells) { lakeCells.set(k.split(',').map(Number), count * 2); count++; }
    return { samples:this.exposeSamples(), values, links, flags, lakeOffsets, lakeCells, lakeValues };
  }

  *restoreState(state: WatershedState): Generator<void> {
    const {values,links,flags,lakeOffsets,lakeCells,lakeValues} = state;
    const count = flags.length;
    if (values.length !== count * 12 || links.length !== count * 2 || lakeOffsets.length !== lakeValues.length / 2 + 1 ||
      lakeOffsets[lakeOffsets.length - 1] * 2 !== lakeCells.length) throw new Error('水文缓存结构无效');
    this.clear(); this.adoptSamples(state.samples);
    if(this.numericKey){
      this.packed=state;this.packedIndex=new Int32Array(this.rows*this.cols).fill(-1);
      for(let i=0;i<count;i++){
        const lake=links[i*2+1],receiver=links[i*2];
        if(receiver < -1 || receiver>=count || lake < -1 || lake>=lakeValues.length/2)throw new Error('水文引用无效');
        if(flags[i]&4){const x=values[i*12],z=values[i*12+1];
          if(!Number.isInteger(x)||!Number.isInteger(z)||x<0||x>=this.cols||z<0||z>=this.rows)throw new Error('水文坐标无效');
          this.packedIndex[z*this.cols+x]=i;this.packedCount++;
        }
        if((i&255)===0)yield;
      }
      this.warm=null;this.warmStep=null;return;
    }
    const lakes: WatershedLake[] = [];
    for (let i = 0; i < lakeValues.length / 2; i++) {
      const members = new Set<string>();
      for (let j = lakeOffsets[i]; j < lakeOffsets[i + 1]; j++) {
        members.add(key(lakeCells[j * 2],lakeCells[j * 2 + 1]));
        if ((j & 255) === 0) yield;
      }
      lakes.push({level:lakeValues[i * 2],discharge:lakeValues[i * 2 + 1],cells:members});
    }
    const cells: WatershedCell[] = [];
    for (let i = 0; i < count; i++) {
      const o = i * 12, lake = links[i * 2 + 1];
      if (links[i * 2] < -1 || links[i * 2] >= count || lake < -1 || lake >= lakes.length) throw new Error('水文引用无效');
      const c: WatershedCell = {x:values[o],z:values[o+1],height:values[o+2],level:values[o+3],distance:values[o+4],rank:values[o+5],
        discharge:values[o+6],area:values[o+7],order:values[o+8],riverCapacity:values[o+9] as RiverSize['capacity'],riverBlocks:values[o+10],at:values[o+11],
        channel:!!(flags[i]&1),weakChannel:!!(flags[i]&2),lake:lake < 0 ? null : lakes[lake],receiver:null};
      cells.push(c);
      if (flags[i] & 4) this.solved.set(this.numericKey ? this.skey(c.x,c.z) : key(c.x,c.z),c);
      if ((i & 255) === 0) yield;
    }
    for (let i = 0; i < count; i++) {
      cells[i].receiver = links[i*2] < 0 ? null : cells[links[i*2]];
      if ((i & 255) === 0) yield;
    }
    this.warm = null; this.warmStep = null;
  }

  adoptState(state: WatershedState): void { for (const _ of this.restoreState(state)) { /* 恢复已解数据 */ } }

  /** 预热：按时间预算分步推进粗网格求解 */
  private buildAsync(x: number, z: number): Generator<void> {
    return this.solve(x, z, () => performance.now() < this.stepDeadline);
  }

  /** 求解过程本身；allowMore 为假时在每个检查点让出 */
  private *solve(x: number, z: number, allowMore: () => boolean): Generator<void, number> {
    const spherical = this.numericKey;
    let cells = new Map<number | string, WatershedCell>(), pending: number[] = [x, z];
    const district = this.circumference ? '' : this.district(x, z);
    let bounded = !this.circumference && this.largeLand.has(key(x, z));
    const inside = (nx: number, nz: number) => !bounded || this.district(nx, nz) === district;
    const start = this.make(x, z);
    cells.set(spherical ? start.at : key(x, z), start);
    for (let i = 0; i < pending.length; i += 2) {
      if ((i & (YIELD_EVERY - 1)) === 0 && !allowMore()) yield;
      if (!this.circumference && !bounded && pending.length > 24000) {
        // 小大陆完整求解
        for (let j = 0; j < pending.length; j += 2) {this.largeLand.add(key(pending[j], pending[j + 1]));if((j&15)===0&&!allowMore())yield;}
        bounded = true; const again = this.make(x, z); cells = new Map([[spherical ? again.at : key(x, z), again]]); pending = [x, z]; i = -2; continue;
      }
      const cx = pending[i], cz = pending[i + 1];
      const count = this.fillNeighbors(cx, cz, spherical);
      for (let k = 0; k < count; k++) {
        const nx = this.neighborX[k], nz = this.neighborZ[k];
        if (!inside(nx, nz)) continue;
        const kk = spherical ? this.neighborKeys[k] : key(nx, nz);
        if (cells.has(kk)) continue;
        const c = this.make(nx, nz); cells.set(kk, c);
        if (c.height > 0) pending.push(nx, nz);
      }
    }
    const queue = new FloodQueue(), sorted: WatershedCell[] = [];
    let steps = 0;
    for (const c of cells.values()) {
      if ((steps++ & 31) === 0 && !allowMore()) yield;
      if (c.height > 0) continue;
      c.level = 0; c.distance = 0; queue.push({cell: c, parent: null, level: 0, distance: 0});
    }
    if (!queue.length) {
      // 无海洋的区域以最低点为内流湖
      let sink = start;
      for (const c of cells.values()) {
        if (c.height < sink.height || c.height === sink.height && (c.z < sink.z || c.z === sink.z && c.x < sink.x)) sink = c;
        if ((steps++ & 31) === 0 && !allowMore()) yield;
      }
      sink.level = Math.max(0, sink.height + 6); sink.distance = 0;
      queue.push({cell: sink, parent: null, level: sink.level, distance: 0});
    }
    // 分区截断会切断真实出口，按域外河床补放水口
    if (bounded) yield* this.outlets(cells, queue,allowMore);
    // 陡坎/封冻屏障会划出内流盆地
    const unsolved=new FloodQueue();
    for(const c of cells.values()){
      if(c.height>0)unsolved.push({cell:c,parent:null,level:c.height,distance:0});
      if((steps++&31)===0&&!allowMore())yield;
    }
    while (true) {
      if ((steps++ & (YIELD_EVERY - 1)) === 0 && !allowMore()) yield;
      if(!queue.length){
        let sink;
        while(unsolved.length){
          const c=unsolved.pop().cell;
          if(c.rank<0){sink=c;break;}
          if((steps++&31)===0&&!allowMore())yield;
        }
        if(!sink)break;
        sink.level=Math.max(0,sink.height+6);sink.distance=0;
        queue.push({cell:sink,parent:null,level:sink.level,distance:0});
      }
      const item = queue.pop(), c = item.cell;
      if ((steps & (YIELD_EVERY - 1)) === 0 && !allowMore()) yield;
      steps++;
      if (c.rank >= 0 || item.level !== c.level || item.distance !== c.distance) continue;
      c.receiver = item.parent; c.rank = sorted.length; sorted.push(c);
      // 已处理的对角边若会交叉
      if (c.receiver && c.x !== c.receiver.x && c.z !== c.receiver.z) {
        const a = cells.get(spherical ? this.skey(c.x, c.receiver.z) : key(c.x, c.receiver.z));
        const b = cells.get(spherical ? this.skey(c.receiver.x, c.z) : key(c.receiver.x, c.z));
        if (a?.receiver === b && b && b.rank >= 0 && b.level <= c.level && this.passable(c,b)) c.receiver = b;
        else if (b?.receiver === a && a && a.rank >= 0 && a.level <= c.level && this.passable(c,a)) c.receiver = a;
      }
      const count = this.fillNeighbors(c.x, c.z, spherical);
      for (let i = 0; i < count; i++) {
        const n = cells.get(spherical ? this.neighborKeys[i] : key(this.neighborX[i], this.neighborZ[i]));
        if (!n || n.rank >= 0 || n.height <= 0 || !this.passable(c,n)) continue;
        const level = Math.max(c.level, n.height - 1.2 - this.incision(n));
        const distance = c.distance + (this.neighborX[i] !== c.x && this.neighborZ[i] !== c.z ? Math.SQRT2 : 1);
        if (level < n.level || level === n.level && distance < n.distance) {
          n.level = level; n.distance = distance;
          queue.push({cell: n, parent: c, level, distance});
        }
      }
    }
    const area = this.step * this.step;
    // 第一遍以默认降水选择主河谷
    for (const c of sorted) {
      if((steps++&31)===0&&!allowMore())yield;
      c.discharge = effectiveRunoff(this.raw(c.x,c.z), .5) * (c.height > 0 ? area : 0);
    }
    for (let i = sorted.length - 1; i >= 0; i--) {
      if((steps++&31)===0&&!allowMore())yield;
      const c = sorted[i]; if (c.receiver) c.receiver.discharge += c.discharge;
    }
    for (const c of sorted) {
      if((steps++&31)===0&&!allowMore())yield;
      if (!c.receiver) continue;
      const original = c.receiver;
      let steepest = 1;
      let count = this.fillNeighbors(c.x, c.z, spherical);
      for (let i = 0; i < count; i++) {
        const n = cells.get(spherical ? this.neighborKeys[i] : key(this.neighborX[i], this.neighborZ[i]));
        if (n && n.rank < c.rank) steepest = Math.max(steepest,c.level-n.level);
      }
      let score = -Infinity;
      count = this.fillNeighbors(c.x, c.z, spherical);
      for (let i = 0; i < count; i++) {
        const n = cells.get(spherical ? this.neighborKeys[i] : key(this.neighborX[i], this.neighborZ[i]));
        if (!n || n.rank >= c.rank || n.level > c.level || !this.passable(c,n)) continue;
        const distance = this.neighborX[i] !== c.x && this.neighborZ[i] !== c.z ? Math.SQRT2 : 1;
        const incoming = Math.max(0,n.discharge - (n === original ? c.discharge : 0));
        const valley = (this.incision(n) - this.incision(c)) / 96;
        const candidate = (c.level-n.level)/steepest/distance * .8 + Math.log1p(incoming/area)*2.2 + valley*2 - distance*.12;
        if (candidate > score) {score = candidate; c.receiver = n;}
      }
      // 改接后仍只允许向已处理节点流去
      if (c.receiver && c.x !== c.receiver.x && c.z !== c.receiver.z) {
        const a = cells.get(spherical ? this.skey(c.x, c.receiver.z) : key(c.x, c.receiver.z));
        const b = cells.get(spherical ? this.skey(c.receiver.x, c.z) : key(c.receiver.x, c.z));
        if (a?.receiver === b && b && b.rank < c.rank && b.level <= c.level && this.passable(c,b)) c.receiver = b;
        else if (b?.receiver === a && a && a.rank < c.rank && a.level <= c.level && this.passable(c,a)) c.receiver = a;
      }
    }
    for (const c of sorted) {
      if((steps++&31)===0&&!allowMore())yield;
      c.area = c.height > 0 ? area : 0;
      c.discharge = effectiveRunoff(this.raw(c.x, c.z), this.precipitation) * c.area;
    }
    const orders = new Map<WatershedCell, number[]>();
    const riverSizes=new Map<WatershedCell,RiverSize[]>();
    for (let i = sorted.length - 1; i >= 0; i--) {
      if((steps++&31)===0&&!allowMore())yield;
      const c = sorted[i], incoming = orders.get(c) ?? [];
      const max = incoming.length ? Math.max(...incoming) : 1;
      c.order = max + (incoming.filter(v => v === max).length > 1 ? 1 : 0);
      // 面积和来水沿 receiver 单调增长
      const eligible=this.precipitation>0&&c.height>0&&c.area>=area*4;
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
    // 深洼地采用刻蚀溢出，逆拓扑传播上游水位上抬
    for (let i = sorted.length - 1; i >= 0; i--) {
      if((steps++&31)===0&&!allowMore())yield;
      const c = sorted[i];
      if (c.height <= 0) continue;
      const retention = Math.min(18, 4 + Math.sqrt(c.discharge) * .15);
      c.level = Math.min(c.level, c.height + retention);
    }
    // 局部下切有界，下游回水向上游传播
    for(const c of sorted){
      if((steps++&31)===0&&!allowMore())yield;
      if(c.height<=0)continue;
      const floor=c.height-1.2-this.incision(c);
      c.level=Math.max(c.level,floor,c.receiver?.level??0);
    }
    // 陡坎保留落差，缓坡水位平抑至下游
    for (let pass = 0; pass < 3; pass++) for (let i = 0; i < sorted.length; i++) {
      if((steps++&31)===0&&!allowMore())yield;
      const c = sorted[i];
      if (!c.receiver || c.receiver.height <= 0 || c.height <= 0) continue;
      const floor=c.height-1.2-this.incision(c);
      if (c.level - c.receiver.level <= WATER_STEP_LIMIT && c.receiver.level>=floor) c.level = c.receiver.level;
    }
    // 同一溢流水位下的连通洼地形成一个静水湖
    const visited = new Set<WatershedCell>();
    for (const c of sorted) {
      if((steps++&31)===0&&!allowMore())yield;
      const raw=this.raw(c.x,c.z);
      // 封冻或临界区不靠来水生成湖泊
      let staticPond=false;
      if(!liquidRiverAllowed(raw)&&raw.moisture>-.2){
        staticPond=true;
        const count=this.fillNeighbors(c.x,c.z,spherical);
        for(let i=0;i<count;i++){
          const n=cells.get(spherical?this.neighborKeys[i]:key(this.neighborX[i],this.neighborZ[i]));
          if(!(n&&n.height>c.height+.5)){staticPond=false;break;}
        }
      }
      if (visited.has(c) || c.height <= 0 || c.level - c.height < .5 || c.discharge < this.threshold&&!staticPond) continue;
      const lakeCells = [c]; visited.add(c);
      for (let i = 0; i < lakeCells.length; i++) {
        if ((steps++ & (YIELD_EVERY - 1)) === 0 && !allowMore()) yield;
        const cell=lakeCells[i], count=this.fillNeighbors(cell.x,cell.z,spherical);
        for (let j = 0; j < count; j++) {
          const n = cells.get(spherical ? this.neighborKeys[j] : key(this.neighborX[j], this.neighborZ[j]));
          if (n && !visited.has(n) && n.height > 0 && n.level === c.level && n.height < c.level) {
            visited.add(n); lakeCells.push(n);
          }
        }
      }
      const lake = {level:c.level,discharge:0,cells:new Set<string>()};
      for(const n of lakeCells){
        lake.discharge=Math.max(lake.discharge,n.discharge);lake.cells.add(key(n.x,n.z));n.lake=lake;
        if((steps++&31)===0&&!allowMore())yield;
      }
    }
    for (const c of sorted) {
      if(c.height>0)this.solved.set(spherical ? c.at : key(c.x,c.z),c);
      if((steps++&31)===0&&!allowMore())yield;
    }
    // 陆块结果保留
    this.samples.clear();
    this.districts.clear();
    this.barriers.clear();this.fineSamples.clear();
    return this.solved.size;
  }
}
