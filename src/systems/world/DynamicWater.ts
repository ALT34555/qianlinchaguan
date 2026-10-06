/** 自然水是连续高度/来水/速度场 */
export const WaterKind = {DRY:0, SEA:1, LAKE:2, RIVER:3, FALL:4, STATIC:5} as const;
export interface WaterState {level: number; kind: number; vx: number; vz: number; discharge: number}
export interface WaterField {
  levels: Float32Array;
  kinds: Uint8Array;
  /** 每列两个分量，方块/秒；静水为零。 */
  velocities: Float32Array;
  discharge: Float32Array;
}
export const flowSpeed = (grade: number, discharge: number, width: number): number =>
  Math.min(8, .12 + Math.sqrt(Math.max(0,grade)) * 3 + Math.log1p(discharge / Math.max(1,width)) * .14);

/** 供未来建筑/坝体与模拟层提交区块边界 */
export interface ChunkWaterBoundary {minLevel?: number; maxLevel?: number; flowScale?: number}
export class ChunkWaterBoundaries {
  private readonly entries = new Map<string, ChunkWaterBoundary>();
  set(cx: number, cz: number, boundary: ChunkWaterBoundary | null): void {
    if (!Number.isInteger(cx) || !Number.isInteger(cz)) throw new Error('水力边界坐标须为整数区块');
    if (boundary && (Object.values(boundary).some(v=>!Number.isFinite(v)) ||
      (boundary.minLevel ?? -Infinity) > (boundary.maxLevel ?? Infinity) || (boundary.flowScale ?? 1) < 0)) throw new Error('水力边界无效');
    if (boundary) this.entries.set(`${cx},${cz}`,{...boundary}); else this.entries.delete(`${cx},${cz}`);
  }
  resolve(cx: number, cz: number, height: number, state: WaterState): WaterState {
    const b=this.entries.get(`${cx},${cz}`);
    if (b) {
      state.level=Math.min(b.maxLevel ?? Infinity,Math.max(b.minLevel ?? -Infinity,state.level));
      state.vx*=b.flowScale ?? 1; state.vz*=b.flowScale ?? 1;
      if (state.kind===WaterKind.DRY && Number.isFinite(state.level)) state.kind=WaterKind.LAKE;
    }
    if (height >= state.level-.12 || !Number.isFinite(state.level)) return {level:-Infinity,kind:WaterKind.DRY,vx:0,vz:0,discharge:0};
    return state;
  }
}
