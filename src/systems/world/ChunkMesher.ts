import {clipShore} from './planet-macro/water-geometry.mjs';
import { DEFAULT_WATER_PALETTE } from './WaterAppearance';
import { findMistEmitters, type WaterfallSampler } from './WaterfallSpray';
export { findMistEmitters, MIST_EMITTER_STRIDE } from './WaterfallSpray';
/** 连续 low poly 地表、水面岸线和瀑布网格 */
import { CHUNK_SIZE, SEA_LEVEL } from '../../core/config';
import { BLOCK_RGB, Block } from './Blocks';
import { PADDED_SIZE } from './WorldContract';
import { TERRAIN_GRID, terrainCell } from './TerrainSurface';
import { WATER_OFFSET, waterVertexLevel, shallowVertexLimit, SHALLOW_STATIC_DEPTH, iceVertexLevel, iceVertexCoverage, clipIce } from './WaterSurface';
import { WaterKind, type WaterField } from './DynamicWater';

export interface MeshData {
  flagWind?:Float32Array;
  plantParts?: Uint8Array;
  normals?: Float32Array;
  positions: Int16Array | Float32Array;
  /** RGB，Uint8 归一化 */
  colors: Uint8Array;
  indices: Uint16Array | Uint32Array;
  /** 水深驱动透明度与岸边细浪 */
  waterDepths?: Float32Array;
  waterFlows?: Float32Array;
  waterFalls?: Float32Array;
  waterFoams?: Uint8Array;
}
export interface ChunkMeshes {
  terrain: MeshData | null;
  water: MeshData | null;
  minimap: Uint8ClampedArray<ArrayBuffer>;
  mist?: Float32Array;
}
const WATER_SHALLOW = [96, 166, 158];
const WATER_DEEP = [28, 70, 104];
const WATER_DEPTH_RANGE = 14;
/** 8米扇面仅用于足够深处，浅水保持逐格 */
const FAN_MIN_DEPTH = 2;
/** 近岸水位外扩带宽（米）与半径（格） */
const SHORE_LEVEL_BAND = .04;
const SHORE_LEVEL_RADIUS = 20;

/** 把湿格水位外扩到近岸，供等深线裁剪 */
function extendShoreLevels(level: Float32Array, bed: Float32Array | Int16Array, P: number): Float32Array | undefined {
  const dist = new Int16Array(P * P).fill(-1);
  const queue: number[] = [];
  for (let i = 0; i < level.length; i++) if (Number.isFinite(level[i])) { dist[i] = 0; queue.push(i); }
  if (queue.length === level.length) return undefined;
  const out = Float32Array.from(level);
  let grew = false;
  for (let head = 0; head < queue.length; head++) {
    const i = queue[head], x = i % P, z = (i - x) / P, depth = dist[i];
    if (depth >= SHORE_LEVEL_RADIUS) continue;
    for (const j of [x > 0 ? i - 1 : -1, x < P - 1 ? i + 1 : -1, z > 0 ? i - P : -1, z < P - 1 ? i + P : -1]) {
      if (j < 0 || dist[j] >= 0 || bed[j] >= out[i] + SHORE_LEVEL_BAND) continue;
      dist[j] = depth + 1; out[j] = out[i]; queue.push(j); grew = true;
    }
  }
  return grew ? out : undefined;
}

class GeoBuilder {
  private pos: Float32Array;
  private col: Uint8Array;
  private idx: Uint32Array;
  private depth: Float32Array | undefined;
  private flow: Float32Array | undefined;
  private fall: Float32Array | undefined;
  private vc = 0;
  private ic = 0;

  constructor(capVerts: number, withDepth = false) {
    this.pos = new Float32Array(capVerts * 3);
    this.col = new Uint8Array(capVerts * 3);
    this.idx = new Uint32Array(capVerts * 2);
    if (withDepth) {this.depth = new Float32Array(capVerts);this.flow=new Float32Array(capVerts*2);this.fall=new Float32Array(capVerts);}
  }
  private grow(): void {
    const cap = this.pos.length / 3 * 2;
    const pos = new Float32Array(cap * 3); pos.set(this.pos); this.pos = pos;
    const col = new Uint8Array(cap * 3); col.set(this.col); this.col = col;
    const idx = new Uint32Array(cap * 2); idx.set(this.idx); this.idx = idx;
    if (this.depth) { const depth = new Float32Array(cap); depth.set(this.depth); this.depth = depth; }
    if (this.flow) {const flow=new Float32Array(cap*2);flow.set(this.flow);this.flow=flow;}
    if (this.fall) {const fall=new Float32Array(cap);fall.set(this.fall);this.fall=fall;}
  }
  vertex(x: number, y: number, z: number, r: number, g: number, b: number, depth = 0,vx=0,vz=0,fall=0): void {
    if (this.vc * 3 + 3 > this.pos.length) this.grow();
    const o = this.vc * 3;
    this.pos[o] = x; this.pos[o + 1] = y; this.pos[o + 2] = z;
    this.col[o] = Math.max(0, Math.min(255, r));
    this.col[o + 1] = Math.max(0, Math.min(255, g));
    this.col[o + 2] = Math.max(0, Math.min(255, b));
    if (this.depth) this.depth[this.vc] = Math.max(0, depth);
    if (this.flow) {this.flow[this.vc*2]=vx;this.flow[this.vc*2+1]=vz;}
    if (this.fall) this.fall[this.vc]=fall;
    this.vc++;
  }
  quad(flip = false): void {
    if (this.ic + 6 > this.idx.length) this.grow();
    const b = this.vc - 4;
    const indices = flip ? [b, b + 1, b + 3, b + 1, b + 2, b + 3] : [b, b + 1, b + 2, b, b + 2, b + 3];
    this.idx.set(indices, this.ic); this.ic += 6;
  }
  triangle(): void {
    if (this.ic + 3 > this.idx.length) this.grow();
    this.idx.set([this.vc - 3, this.vc - 2, this.vc - 1], this.ic); this.ic += 3;
  }
  fan(rim:number):void {
    while(this.ic+rim*3>this.idx.length)this.grow();
    const base=this.vc-rim-1;
    for(let i=0;i<rim;i++){this.idx[this.ic++]=base;this.idx[this.ic++]=base+1+i;this.idx[this.ic++]=base+1+(i+1)%rim;}
  }
  finish(): MeshData | null {
    if (!this.vc) return null;
    return {
      positions: this.pos.slice(0, this.vc * 3), colors: this.col.slice(0, this.vc * 3),
      indices: this.vc <= 65535 ? Uint16Array.from(this.idx.subarray(0, this.ic)) : this.idx.slice(0, this.ic),
      ...(this.depth ? { waterDepths: this.depth.slice(0, this.vc) } : {}),
      ...(this.flow ? {waterFlows:this.flow.slice(0,this.vc*2)} : {}),
      ...(this.fall ? {waterFalls:this.fall.slice(0,this.vc)} : {}),
    };
  }
}

type WaterVertex = { x: number; y: number; z: number; depth: number };
/** 岸边颜色过渡带宽（米） */
const SHORE_COLOR_BAND = .6;
/** 1 米色场一次 3x3 平滑，供岸边过渡 */
function shoreColorField(colors: Uint8Array | undefined, P: number): Uint8Array | undefined {
  if (!colors) return undefined;
  const clamp = (v: number) => v < 0 ? 0 : v >= P ? P - 1 : v;
  const pass = (source: Uint8Array, horizontal: boolean): Uint8Array => {
    const out = new Uint8Array(P * P * 3);
    const at = (px: number, pz: number, c: number) => source[(clamp(pz) * P + clamp(px)) * 3 + c];
    for (let z = 0; z < P; z++) for (let x = 0; x < P; x++) for (let c = 0; c < 3; c++)
      out[(z * P + x) * 3 + c] = horizontal
        ? (at(x - 1, z, c) + 2 * at(x, z, c) + at(x + 1, z, c)) >> 2
        : (at(x, z - 1, c) + 2 * at(x, z, c) + at(x, z + 1, c)) >> 2;
    return out;
  };
  return pass(pass(colors, true), false);
}
export function buildChunkMeshes(
  cx: number, cz: number, heights: Int16Array | Float32Array, surfaces: Uint32Array | Uint16Array | Uint8Array, seed: number,
  waterLevels?: Float32Array, surfaceColors?: Uint8Array,waterField?: WaterField, waterfallSampler?: WaterfallSampler, waterSurfaceOffset = WATER_OFFSET, limitShallow = true,
): ChunkMeshes {
  const S = CHUNK_SIZE, P = PADDED_SIZE, ox = cx * S, oz = cz * S;
  const H = (x: number, z: number) => waterField?.bedLevels?.[(z + 1) * P + x + 1] ?? heights[(z + 1) * P + x + 1];
  const W = (x: number, z: number) => waterLevels?.[(z + 1) * P + x + 1] ?? SEA_LEVEL;
  /** 近岸水位场：外扩后按真实等深线裁剪 */
  const shoreLevels = limitShallow || !waterLevels ? undefined
    : extendShoreLevels(waterLevels, waterField?.bedLevels ?? heights, P);
  const WL = shoreLevels ? (x: number, z: number) => shoreLevels[(z + 1) * P + x + 1] : W;
  const terrain = new GeoBuilder((S / TERRAIN_GRID) ** 2 * 4);
  const water = new GeoBuilder(S * S * 4, true);
  const minimap = new Uint8ClampedArray(S * S * 4);
  // 一次计算每个共享格角
  const V = S + 1;
  const vertexHeights = new Float32Array(V * V);
  const vertexLevels = new Float32Array(V * V);
  const vertexColors = new Uint8Array(V * V * 3);
  const shallowStatic = (x: number, z: number) => {
    const i = (z + 1) * P + x + 1;
    return waterField?.kinds[i] === WaterKind.STATIC && !(waterField.iceCoverages?.[i]! >= .5);
  };
  let staticLimits:Float32Array|undefined;
  if(limitShallow&&waterField?.kinds.includes(WaterKind.STATIC))for(let i=0;i<waterField.kinds.length;i++){
    if(waterField.kinds[i]!==WaterKind.STATIC||waterField.iceCoverages?.[i]!>=.5||!Number.isFinite(waterField.levels[i]))continue;
    const depth=waterField.levels[i]-(waterField.bedLevels?.[i]??heights[i]);
    if(depth<=0||depth>SHALLOW_STATIC_DEPTH+1e-5)continue;
    staticLimits??=new Float32Array(V*V).fill(Infinity);
    const x=i%P-1,z=Math.floor(i/P)-1;
    for(let dz=0;dz<=1;dz++)for(let dx=0;dx<=1;dx++){
      if(x+dx>=0&&x+dx<=S&&z+dz>=0&&z+dz<=S)staticLimits[(z+dz)*V+x+dx]=NaN;
    }
  }
  const cellSide=S/TERRAIN_GRID,refinements=new Uint8Array(cellSide*cellSide),flips=new Uint8Array(cellSide*cellSide);
  for(let z=0;z<S;z+=TERRAIN_GRID)for(let x=0;x<S;x+=TERRAIN_GRID){
    const cell=terrainCell(ox+x+.5,oz+z+.5,seed,(wx,wz)=>H(wx-ox,wz-oz)),i=z/TERRAIN_GRID*cellSide+x/TERRAIN_GRID;
    refinements[i]=Number(cell.refined);flips[i]=Number(cell.flip);
    for(let dz=0;dz<=TERRAIN_GRID;dz++)for(let dx=0;dx<=TERRAIN_GRID;dx++)vertexHeights[(z+dz)*V+x+dx]=cell.heights[dz*3+dx];
  }
  for (let z = 0; z <= S; z++) for (let x = 0; x <= S; x++) {
    const i = z * V + x;
    vertexLevels[i] = waterVertexLevel(ox + x + .5, oz + z + .5, (wx, wz) => H(wx - ox, wz - oz), (wx, wz) => WL(wx - ox, wz - oz), seed, !waterField?.bedLevels, waterSurfaceOffset);
    if (staticLimits&&Number.isNaN(staticLimits[i])) staticLimits[i] = shallowVertexLimit(x, z, H, W, shallowStatic);
  }
  // 岸边色带平滑，压掉逐格分类条纹
  const shoreColors = shoreColorField(surfaceColors, P);
  for (let z = 0; z <= S; z++) for (let x = 0; x <= S; x++) {
    const i = z * V + x, depth = Number.isFinite(vertexLevels[i]) ? vertexLevels[i] - vertexHeights[i] : -Infinity;
    const blend = shoreColors ? Math.max(0, 1 - Math.abs(depth) / SHORE_COLOR_BAND) : 0;
    for (let c = 0; c < 3; c++) {
      const block = surfaces[Math.min(S - 1, z) * S + Math.min(S - 1, x)];
      const raw = surfaceColors?.[((z + 1) * P + x + 1) * 3 + c] ?? BLOCK_RGB[block * 3 + c];
      vertexColors[i * 3 + c] = blend > 0 ? Math.round(raw + (shoreColors![((z + 1) * P + x + 1) * 3 + c] - raw) * blend) : raw;
    }
  }
  const iceAt = (field: Float32Array | undefined, x: number, z: number): number => field
    ? iceVertexLevel(x+.5,z+.5,(px,pz)=>field[(pz+1)*P+px+1]) : -Infinity;
  const iceColor = waterField?.appearance?.colors.ice ?? DEFAULT_WATER_PALETTE.ice;
  const emitIce = (v: WaterVertex) => terrain.vertex(v.x+.5,v.y,v.z+.5,...iceColor);
  const shallow = waterField?.appearance?.colors.shallow ?? WATER_SHALLOW;
  const deep = waterField?.appearance?.colors.deep ?? WATER_DEEP;
  const emitWater = (v: WaterVertex) => {
    const t = Math.min(1, Math.max(0, v.depth) / WATER_DEPTH_RANGE);
    let vx=0,vz=0,fall=0;
    if(waterField)for(const [dx,dz] of [[-1,-1],[0,-1],[-1,0],[0,0]]){
      const i=(Math.floor(v.z)+dz+1)*P+Math.floor(v.x)+dx+1;
      vx+=waterField.velocities[i*2]*.25;vz+=waterField.velocities[i*2+1]*.25;
      if(waterField.kinds[i]===WaterKind.FALL)fall=1;
    }
    water.vertex(v.x+.5, v.y, v.z+.5,
      shallow[0] + (deep[0] - shallow[0]) * t,
      shallow[1] + (deep[1] - shallow[1]) * t,
      shallow[2] + (deep[2] - shallow[2]) * t, v.depth,vx,vz,fall);
  };
  let minWater=Infinity,maxWater=-Infinity;
  if(!limitShallow&&waterLevels)for(const level of waterLevels){minWater=Math.min(minWater,level);maxWater=Math.max(maxWater,level);}
  const fullIce=!!waterField?.iceCoverages?.every(v=>v>=.999);
  const flatIce=fullIce&&waterField!.iceLevels!.every(v=>v===waterField!.iceLevels![0])&&waterField!.iceBottomLevels!.every(v=>v===waterField!.iceBottomLevels![0]);
  const flatWaterStep=!limitShallow&&waterField&&Number.isFinite(minWater)&&maxWater===minWater
    &&waterField.kinds.every(k=>k===WaterKind.SEA||k===WaterKind.LAKE||k===WaterKind.RIVER||k===WaterKind.STATIC)
    &&(!waterField.iceCoverages?.some(v=>v>=.5)||flatIce)&&vertexHeights.every(v=>v<minWater-waterSurfaceOffset)?8:1;
  const waterBlocks=new Uint8Array(64);
  if(flatWaterStep===1&&!limitShallow&&waterField&&(!waterField.iceCoverages?.some(v=>v>=.5)||fullIce)){
    for(let bz=0;bz<8;bz++)for(let bx=0;bx<8;bx++){
      const ox=bx*8,oz=bz*8,a=vertexLevels[oz*V+ox],b=vertexLevels[(oz+8)*V+ox],c=vertexLevels[(oz+8)*V+ox+8],d=vertexLevels[oz*V+ox+8];
      const low=Math.min(a,b,c,d),high=Math.max(a,b,c,d),floor=low-FAN_MIN_DEPTH;
      let valid=Number.isFinite(low)&&Number.isFinite(high)&&Number.isFinite(floor)&&high-low<=2;
      for(let z=0;z<=8&&valid;z++)for(let x=0;x<=8;x++){
        const i=(oz+z)*V+ox+x,p=(a*(8-x)+d*x)*(8-z)/64+(b*(8-x)+c*x)*z/64;
        if(!Number.isFinite(vertexLevels[i])||vertexHeights[i]>=floor||Math.abs(vertexLevels[i]-p)>.01){valid=false;break;}
      }
      if(valid)waterBlocks[bz*8+bx]=1;
    }
  }
  for (let z = 0; z < S; z++) for (let x = 0; x < S; x++) {
    if (surfaces[z * S + x] === Block.AIR) continue;
    const h = H(x, z), level = W(x, z);
    const corners = [z * V + x, (z + 1) * V + x, (z + 1) * V + x + 1, z * V + x + 1];
    const coordinates = [[x, z], [x, z + 1], [x + 1, z + 1], [x + 1, z]];
    const cellIndex=Math.floor(z/TERRAIN_GRID)*cellSide+Math.floor(x/TERRAIN_GRID),flip=!!flips[cellIndex];
    if (x % TERRAIN_GRID === 0 && z % TERRAIN_GRID === 0) {
      const refined=!!refinements[cellIndex];
      const step=refined?1:TERRAIN_GRID;
      for(let dz=0;dz<TERRAIN_GRID;dz+=step)for(let dx=0;dx<TERRAIN_GRID;dx+=step) {
        for(const [px,pz] of [[x+dx,z+dz],[x+dx,z+dz+step],[x+dx+step,z+dz+step],[x+dx+step,z+dz]]) {
          const i=pz*V+px;
          terrain.vertex(px+.5,vertexHeights[i],pz+.5,vertexColors[i*3],vertexColors[i*3+1],vertexColors[i*3+2]);
        }
        terrain.quad(flip);
      }
    }

    const wet = Number.isFinite(level) && h < level;
    // 干格也检查格角
    const finiteLevels = corners.map(i => vertexLevels[i]).filter(Number.isFinite);
    if(flatWaterStep>1){
      if(x%flatWaterStep===0&&z%flatWaterStep===0){
        for(const [px,pz] of [[x,z],[x,z+flatWaterStep],[x+flatWaterStep,z+flatWaterStep],[x+flatWaterStep,z]]){
          const i=pz*V+px,y=vertexLevels[i];emitWater({x:px,z:pz,y,depth:y-vertexHeights[i]});
        }
        water.quad(flip);
      }
    }else if(waterBlocks[(z>>3)*8+(x>>3)]){
      if(x%8===0&&z%8===0){
        const emit=(px:number,pz:number)=>{const i=pz*V+px,y=vertexLevels[i];emitWater({x:px,z:pz,y,depth:y-vertexHeights[i]});};
        emit(x+4,z+4);
        for(let k=0;k<8;k++)emit(x,z+k);
        for(let k=0;k<8;k++)emit(x+k,z+8);
        for(let k=0;k<8;k++)emit(x+8,z+8-k);
        for(let k=0;k<8;k++)emit(x+8-k,z);
        water.fan(32);
      }
    }else if (wet || finiteLevels.length) {
      const depthLimit = staticLimits && corners.every(i => !Number.isFinite(vertexLevels[i]) || Number.isFinite(staticLimits[i]))
        && (!wet || shallowStatic(x, z) && level - h <= SHALLOW_STATIC_DEPTH + 1e-5) ? SHALLOW_STATIC_DEPTH : Infinity;
      const fallback = wet ? level - waterSurfaceOffset : Math.min(...finiteLevels);
      const vertices = corners.map((i, j) => {
        const y = Number.isFinite(vertexLevels[i]) ? vertexLevels[i] : fallback;
        return { x: coordinates[j][0], y, z: coordinates[j][1], depth: y - vertexHeights[i] };
      });
      if (vertices.every(v => v.depth > 0 && v.depth <= depthLimit)) { vertices.forEach(emitWater); water.quad(flip); }
      else {
        const triangles = flip ? [[0, 1, 3], [1, 2, 3]] : [[0, 1, 2], [0, 2, 3]];
        for (const tri of triangles) {
          let polygon = clipShore(tri.map(j => vertices[j]));
          if (Number.isFinite(depthLimit)) polygon = clipShore(polygon, depthLimit);
          for (let j = 1; j + 1 < polygon.length; j++) {
            if ([polygon[0], polygon[j], polygon[j + 1]].every(v => v.depth === 0)) continue;
            emitWater(polygon[0]); emitWater(polygon[j]); emitWater(polygon[j + 1]); water.triangle();
          }
        }
      }
    }
    const coverageAt=(px:number,pz:number)=>waterField?.iceCoverages?.[(pz+1)*P+px+1]
      ?? (Number.isFinite(waterField?.iceLevels?.[(pz+1)*P+px+1])?1:0);
    const amounts=coordinates.map(([px,pz])=>iceVertexCoverage(px+.5,pz+.5,coverageAt));
    const iced=amounts.some(v=>v>=.5);
    if(iced&&(flatIce&&flatWaterStep>1||fullIce&&waterBlocks[(z>>3)*8+(x>>3)])){
      if(x%8===0&&z%8===0){
        const points:[[number,number]]|number[][]=[[x+4,z+4]];
        for(let k=0;k<8;k++)points.push([x,z+k]);
        for(let k=0;k<8;k++)points.push([x+k,z+8]);
        for(let k=0;k<8;k++)points.push([x+8,z+8-k]);
        for(let k=0;k<8;k++)points.push([x+8-k,z]);
        const values=points.map(([px,pz])=>({x:px,z:pz,y:iceAt(waterField?.iceLevels,px,pz),depth:1,coverage:1,bottom:iceAt(waterField?.iceBottomLevels,px,pz)}));
        values.forEach(emitIce);terrain.fan(32);
        [values[0],...values.slice(1).reverse()].forEach(v=>emitIce({...v,y:v.bottom}));terrain.fan(32);
      }
    }else if (iced) {
      const levels=coordinates.map(([px,pz])=>iceAt(waterField?.iceLevels,px,pz));
      const finite=levels.filter(Number.isFinite);
      if(!finite.length)continue;
      const fallback=Math.min(...finite);
      const top=coordinates.map(([px,pz],j) => {
        const y=Number.isFinite(levels[j])?levels[j]:fallback;
        const bottom=Math.min(y,Math.max(vertexHeights[corners[j]],Number.isFinite(iceAt(waterField?.iceBottomLevels,px,pz))?iceAt(waterField?.iceBottomLevels,px,pz):y));
        return {x:px,y,z:pz,depth:y-vertexHeights[corners[j]],coverage:amounts[j],bottom};
      });
      for (const tri of flip ? [[0,1,3],[1,2,3]] : [[0,1,2],[0,2,3]]) {
        const polygon=clipIce(clipIce(tri.map(j=>top[j]),'coverage'),'depth');
        if(polygon.length<3)continue;
        for(let j=1;j+1<polygon.length;j++) {
          [polygon[0],polygon[j],polygon[j+1]].forEach(emitIce);terrain.triangle();
          [polygon[j+1],polygon[j],polygon[0]].forEach(v=>emitIce({...v,y:v.bottom}));terrain.triangle();
        }
        for(let j=0;j<polygon.length;j++) {
          const a=polygon[j],b=polygon[(j+1)%polygon.length];
          if(Math.abs(a.coverage-.5)>1e-6||Math.abs(b.coverage-.5)>1e-6)continue;
          [b,a,{...a,y:a.bottom},{...b,y:b.bottom}].forEach(emitIce);terrain.quad();
        }
      }
    }
    const mi = (z * S + x) * 4;
    if (iced) {
      for (let c=0;c<3;c++) minimap[mi+c]=iceColor[c];
    } else if (wet) {
      const t = Math.min(1, (level - h) / WATER_DEPTH_RANGE);
      for (let c = 0; c < 3; c++) minimap[mi + c] = shallow[c] + (deep[c] - shallow[c]) * t;
    } else {
      const slope = (H(x, z - 1) - H(x, z + 1)) * .12 + (H(x - 1, z) - H(x + 1, z)) * .08;
      const shade = Math.max(.65, Math.min(1.15, .95 + slope));
      for (let c = 0; c < 3; c++) minimap[mi + c] = vertexColors[corners[0] * 3 + c] * shade;
    }
    minimap[mi + 3] = 255;
  }
  const mist = waterField ? findMistEmitters(S, waterField, waterfallSampler) : undefined;
  const waterMesh = water.finish();
  if (waterMesh) {
    const foam = waterField?.appearance?.colors.foam ?? DEFAULT_WATER_PALETTE.foam;
    waterMesh.waterFoams = new Uint8Array(waterMesh.positions.length);
    for (let i = 0; i < waterMesh.waterFoams.length; i++) waterMesh.waterFoams[i] = foam[i % 3];
  }
  return { terrain: terrain.finish(), water: waterMesh, minimap, ...(mist ? { mist } : {}) };
}
