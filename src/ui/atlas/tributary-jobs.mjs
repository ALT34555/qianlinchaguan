import {tributaryCandidate} from '../../systems/world/planet-macro/source-rivers.mjs';
// 装饰支流按修订与宏观格合并
export const TRIBUTARY_LEVEL = 7;
export const TRIBUTARY_LAKE_LEVEL = 16;
// 整格走廊含所有源点和选路，另留岸池余量
export function tributaryCorridorIntersects(world,cell,tile){
  if(world.water[cell]===2)return true;
  const C=world.circumference,span=C/2**tile.z,size=C/world.columns;
  const row=Math.floor(cell/world.columns),col=cell%world.columns;
  const top=row*size-C/4,bottom=top+size;
  const tileTop=tile.y*span-C/4,tileBottom=tileTop+span;
  const pad=64+span*8/128;
  if(bottom+pad<tileTop||top-pad>tileBottom)return false;
  const latitude=Math.min(Math.PI/2,Math.max(Math.abs(top),Math.abs(bottom))/C*2*Math.PI+pad/C*2*Math.PI);
  if(latitude>=Math.PI/2)return true;
  const longitudePad=pad/Math.max(.001,Math.cos(latitude));
  const center=(col+.5)*size-C/2,tileCenter=(tile.x+.5)*span-C/2;
  const dx=tileCenter-center,wrapped=dx-Math.round(dx/C)*C;
  return Math.abs(wrapped)<=size/2+span/2+longitudePad;
}
const MAX_CELLS = 512, CELL_BUDGET = 8 * 2 ** 20;

// 视野覆盖的宏观格，外扩一格避免漏掉跨格支流
export function tributaryCells(world, tiles, logicalLevel) {
  if (logicalLevel < TRIBUTARY_LEVEL) return [];
  const columns = world.columns, rows = world.rows;
  const candidates=new Map();
  const lakeTier = logicalLevel >= TRIBUTARY_LAKE_LEVEL ? 2 : 1, seen = new Set(), out = [];
  for (const tile of tiles) {
    const n = 2 ** tile.z;
    if (n < columns) continue;
    const x0 = Math.floor(tile.x * columns / n) - 1, x1 = Math.ceil((tile.x + 1) * columns / n) + 1;
    const y0 = Math.max(0, Math.floor(tile.y * columns / n) - 1), y1 = Math.min(rows - 1, Math.ceil((tile.y + 1) * columns / n) + 1);
    for (let row = y0; row <= y1; row++) for (let col = x0; col <= x1; col++) {
      const cell = row * columns + ((col % columns) + columns) % columns;
      if (seen.has(cell)) continue;
      if(world.flow&&world.rainfall&&world.evaporation&&world.areas&&world.receiver&&world.riverWidth){
        if(!candidates.has(cell))candidates.set(cell,tributaryCandidate(world,cell));
        if(!candidates.get(cell))continue;
      }
      if(!tributaryCorridorIntersects(world,cell,tile))continue;
      seen.add(cell);
      const lake = world.water[cell] === 2;
      if (lake && logicalLevel < TRIBUTARY_LAKE_LEVEL) continue;
      out.push({cell, tier: lake ? lakeTier : 1, distance: tile.distance ?? 0});
    }
  }
  return out;
}

export class TributaryTasks {
  constructor() {
    this.revision = '';
    this.cells = new Map();
    this.pending = new Map();
    this.queue = [];
    this.required = new Set();
    this.bytes = 0;
    this.serial = 0;
    this.version = 0;
    this.stats = {cells: 0, pending: 0, queued: 0, bytes: 0};
  }

  setRevision(revision) {
    if (this.revision === revision) return;
    this.revision = revision;
    this.cells.clear();
    this.pending.clear();
    this.queue = [];
    this.required.clear();
    this.bytes = 0;
    this.version++;
  }

  // 逐帧登记视野所需宏观格，已完成的格子不重复排队
  sync(list) {
    this.queue = [];
    this.required = new Set();
    const wanted = list.filter(({cell, tier}) => {
      this.required.add(cell);
      const done = this.cells.get(cell);
      return !(done && done.tier >= tier) && !this.pending.has(cell);
    });
    wanted.sort((a, b) => a.distance - b.distance || a.cell - b.cell);
    this.queue = wanted;
    this.evict();
    this.stats = {cells: this.cells.size, pending: this.pending.size, queued: this.queue.length, bytes: this.bytes};
    return wanted.length;
  }

  // 优先取归属格，取走即登记进行中
  next(owner = -1, poolSize = 1) {
    for (let i = 0; i < this.queue.length; i++) {
      const job = this.queue[i];
      if (owner >= 0 && job.cell % poolSize !== owner) continue;
      if (this.pending.has(job.cell) || !this.required.has(job.cell)) continue;
      const done = this.cells.get(job.cell);
      if (done && done.tier >= job.tier) continue;
      this.queue.splice(i, 1);
      const pending = {...job, id: ++this.serial};
      this.pending.set(job.cell, pending);
      return pending;
    }
    return null;
  }

  // 完成或取消后释放进行中登记
  settle(cell) {
    this.pending.delete(cell);
  }

  complete(job, vectors) {
    this.pending.delete(job.cell);
    if (!vectors) return false;
    const bytes = vectors.reduce((sum, v) => sum + v.coordinates.byteLength + 64, 0);
    this.bytes -= this.cells.get(job.cell)?.bytes ?? 0;
    this.cells.delete(job.cell);
    this.cells.set(job.cell, {tier: job.tier, vectors, bytes});
    this.bytes += bytes;
    this.version++;
    this.evict();
    return true;
  }

  // 需求之外的格按最久未用淘汰
  evict() {
    if (this.cells.size <= MAX_CELLS && this.bytes <= CELL_BUDGET) return;
    for (const [cell, entry] of this.cells) {
      if (this.cells.size <= MAX_CELLS && this.bytes <= CELL_BUDGET) break;
      if (this.required.has(cell)) continue;
      this.cells.delete(cell);
      this.bytes -= entry.bytes;
    }
  }

  vectors() {
    const out = [];
    for (const entry of this.cells.values()) for (const vector of entry.vectors) out.push(vector);
    return out;
  }

  dispose() {
    this.cells.clear();
    this.pending.clear();
    this.queue = [];
    this.required.clear();
    this.bytes = 0;
    this.version++;
  }
}
