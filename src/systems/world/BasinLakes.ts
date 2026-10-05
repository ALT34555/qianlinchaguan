/** 按自然汇水盆地蓄水。仅缓存查询结果，不按区块加载顺序改写地势。 */
export interface BasinLake {
  cx: number; cz: number; level: number; spillLevel: number; discharge: number;
  cells: ReadonlySet<string>;
}
interface BasinNode { height: number }
interface Outlet { cx: number; cz: number }
interface FloodCell extends Outlet { height: number }
const key = (x: number, z: number) => `${x},${z}`;
const NEIGHBORS = [[0, -1], [1, -1], [1, 0], [1, 1], [0, 1], [-1, 1], [-1, 0], [-1, -1]];

// 优先洪泛总是先处理最低的盆地边缘，首个分水岭即最低溢出口。
class FloodQueue {
  private cells: FloodCell[] = [];
  get length(): number { return this.cells.length; }
  push(cell: FloodCell): void {
    let i = this.cells.length;
    this.cells.push(cell);
    while (i > 0) {
      const parent = (i - 1) >> 1;
      if (this.cells[parent].height <= cell.height) break;
      this.cells[i] = this.cells[parent]; i = parent;
    }
    this.cells[i] = cell;
  }
  pop(): FloodCell {
    const first = this.cells[0], last = this.cells.pop()!;
    if (this.cells.length) {
      let i = 0;
      while (i * 2 + 1 < this.cells.length) {
        let child = i * 2 + 1;
        if (child + 1 < this.cells.length && this.cells[child + 1].height < this.cells[child].height) child++;
        if (last.height <= this.cells[child].height) break;
        this.cells[i] = this.cells[child]; i = child;
      }
      this.cells[i] = last;
    }
    return first;
  }
}

export class BasinLakes {
  private readonly outlets = new Map<string, Outlet | null>();
  private readonly basins = new Map<string, BasinLake | null>();
  constructor(
    private readonly node: (cx: number, cz: number) => BasinNode,
    private readonly flow: (cx: number, cz: number) => number,
    private readonly accumulation: (cx: number, cz: number) => number,
    private readonly threshold: number,
  ) {}

  clear(): void { this.outlets.clear(); this.basins.clear(); }

  private outlet(cx: number, cz: number): Outlet | null {
    const path: string[] = [];
    let outlet: Outlet | null;
    // 严格下降，不设置会截断长河网的步数上限。
    while (true) {
      const k = key(cx, cz);
      if (this.outlets.has(k)) { outlet = this.outlets.get(k)!; break; }
      path.push(k);
      if (this.node(cx, cz).height <= 0) { outlet = null; break; }
      const direction = this.flow(cx, cz);
      if (direction < 0) { outlet = { cx, cz }; break; }
      const [dx, dz] = NEIGHBORS[direction]; cx += dx; cz += dz;
    }
    for (const k of path) this.outlets.set(k, outlet);
    return outlet;
  }

  private basin(sink: Outlet): BasinLake | null {
    const k = key(sink.cx, sink.cz);
    if (this.basins.has(k)) return this.basins.get(k)!;
    const discharge = this.accumulation(sink.cx, sink.cz);
    if (discharge < this.threshold) { this.basins.set(k, null); return null; }
    const bottom = this.node(sink.cx, sink.cz).height;
    const target = bottom + Math.min(24, 4 + Math.sqrt(discharge) * .55);
    const queue = new FloodQueue(), visited = new Set<string>([k]), flooded: FloodCell[] = [];
    queue.push({ ...sink, height: bottom });
    let spillLevel = target;
    while (queue.length) {
      const cell = queue.pop();
      const outlet = this.outlet(cell.cx, cell.cz);
      // 水位在越过分水岭/有限搜索边缘前停止；不会产生方形截断湖岸。
      if (cell.height >= target || Math.max(Math.abs(cell.cx - sink.cx), Math.abs(cell.cz - sink.cz)) >= 12 ||
          !outlet || outlet.cx !== sink.cx || outlet.cz !== sink.cz) {
        spillLevel = cell.height; break;
      }
      flooded.push(cell);
      for (const [dx, dz] of NEIGHBORS) {
        const cx = cell.cx + dx, cz = cell.cz + dz, nk = key(cx, cz);
        if (visited.has(nk)) continue;
        visited.add(nk);
        queue.push({ cx, cz, height: Math.max(cell.height, this.node(cx, cz).height) });
      }
    }
    const level = Math.min(target, spillLevel - .15);
    const lake = level > bottom + .2 ? { ...sink, level, spillLevel, discharge,
      cells: new Set(flooded.filter(cell => cell.height < level).map(cell => key(cell.cx, cell.cz))) } : null;
    this.basins.set(k, lake); return lake;
  }

  get(cx: number, cz: number): BasinLake | null {
    const lake = this.catchment(cx, cz);
    return lake?.cells.has(key(cx, cz)) ? lake : null;
  }

  catchment(cx: number, cz: number): BasinLake | null {
    const sink = this.outlet(cx, cz);
    return sink ? this.basin(sink) : null;
  }
}
