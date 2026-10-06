import { createNoise2D, type NoiseFunction2D } from 'simplex-noise';
import { CHUNK_SIZE, SEA_LEVEL, WORLD_MAX_Y, WORLD_MIN_Y } from '../../core/config';
import { mulberry32 } from '../../core/math/Random';
import { Block, BLOCK_RGB } from './Blocks';
import {applyGeology, geologicalRelief} from './Geology';
import {WaterKind, flowSpeed, ChunkWaterBoundaries, type WaterField, type WaterState} from './DynamicWater';
import { isRiverType } from './ChunkTypes';
import { mixTerrainProfiles, overlayTerrain, type TerrainWeight, type TerrainProfile } from './TerrainBlend';
import { CLIMATES, DEFAULT_CLIMATE_WEIGHTS, normalizeClimateWeights, normalizeGeneration, type ClimateWeights, type WorldGeneration } from './WorldSettings';
import { PlanetTerrain } from './PlanetTerrain';
import { BasinLakes, type BasinLake } from './BasinLakes';
import { ClimateLayer } from './ClimateLayer';
import { TerrainLayers, CONTINENT_SCALE, altitudeSnow, freezeState, clamp, smooth, type TerrainSample } from './TerrainLayers';
import type {PlateTier} from './PlateUplift';
import { RiverNetwork, FLOW_DIRECTIONS, type AlluvialFan } from './RiverNetwork';
import {riverGuide,riverCurve,MAX_RIVER_BEND,RIVER_NODE_NOISE,RIVER_BANK_BLEND} from './RiverGeometry';
import {wetlandPond} from './WetlandSurface';
import { MAX_BRAID_OFFSET, MAX_MOUTH_LENGTH, MAX_RIVER_RADIUS, RIVER_BED_CORE,
  riverBankShape, riverBedFlat, riverDepth, riverFloodplain, riverInfluence, riverInfluenceMargin, riverShore, riverWidth,
  RIVER_EDGE_NOISE, RIVER_LAKE_EDGE_NOISE, type RiverPoint } from './RiverChannels';
export type { RiverPoint } from './RiverChannels';
export { FLOW_DIRECTIONS } from './RiverNetwork';

export const PADDED_SIZE = CHUNK_SIZE + 2;
const MAX_VALLEY_SHOULDER = 192;
/** 达到此落差才算大瀑布，跌水收拢到河段中段；此落差以下按地势连续下降。 */
const FALL_CONCENTRATION_DROP = 64;
const RIVER_SEARCH_RADIUS = Math.ceil((riverInfluence(MAX_RIVER_RADIUS * 1.18) + riverInfluenceMargin(MAX_RIVER_RADIUS, true) + MAX_VALLEY_SHOULDER + CHUNK_SIZE + 14 + MAX_BRAID_OFFSET + MAX_MOUTH_LENGTH+MAX_RIVER_BEND) / CHUNK_SIZE);
/** 河谷基面探测偏移：避开河道自身的下切 */
const VALLEY_PROBES: readonly (readonly [number, number])[] = [[-128, 0], [128, 0], [0, -128], [0, 128]];
/** 邻域无地势支撑时仍保留的分级下切比例 */
const RIVER_BED_MIN_SHARE = .6;
/** 供水河段床面下限的邻近距离权重（格） */
const RIVER_BED_OWNER = 6;
const key = (x: number, z: number) => `${x},${z}`;
const mix = (a: number, b: number, t: number) => a + (b - a) * t;
// Catmull–Rom
const cubic = (a: number, b: number, c: number, d: number, t: number) =>
  b + 0.5 * t * (c - a + t * (2 * a - 5 * b + 4 * c - d + t * (3 * (b - c) + d - a)));
interface TerrainPatch { rows: number[][]; lakes: BasinLake[] }

type LandNode = TerrainSample & {cx: number; cz: number; temperature: number};
export interface ChunkInfo {
  cx: number; cz: number; type: number; climate: number; temperature: number;
  elevation: number; flow: number; discharge: number; riverWidth: number; riverDepth: number;riverBlocks:number;
  lakeLevel: number | null;
  elevationTier: PlateTier; largeLandmass: boolean;
  plateBase:number;hillRelief:number;highlandRelief:number;waterfallDrop:number;wetland:number;
  downstream: { cx: number; cz: number } | null;
  /** 舆图/调试面板用的河区块流向；主河同 flow，河槽带取邻近主河方向 */
  displayFlow: number;
  /** 内部：未加湿地闸门的走廊值，供 displayFlow 迟到补算 */
  courseVal: number;
}
interface ColumnSample { height: number; water: number; bank: number; channel: number; flowGrade: number; mouth: number; alluvium: number; kind: number; vx: number; vz: number; discharge: number;staticSurface:number;weights:TerrainWeight[] }
interface RiverSegment {
  ax: number; az: number; bx: number; bz: number;
  aLevel: number; bLevel: number; aWidth: number; bWidth: number;
  aDepth: number; bDepth: number;
  aMouth: number; bMouth: number;
  length2: number; minX: number; maxX: number; minZ: number; maxZ: number;
  lake: boolean; grade: number;
  shoulder: number; discharge: number;
}
interface HydraulicNode { x: number; z: number; level: number; confluence:boolean }
interface RiverReach { segments: RiverSegment[]; start: HydraulicNode; end: HydraulicNode }
export interface ChunkGenResult {
  cx: number; cz: number; type: number;
  heights: Float32Array;
  surfaces: Uint32Array;
  /** 外扩一圈的地表混合 RGB */
  surfaceColors: Uint8Array;
  /** 外扩一圈 */
  waterLevels: Float32Array;
  waterField: WaterField;
}

export class WorldGenerator {
  readonly seed: number;
  /** 舆图/调试面板绘制期间置真，才计算 ChunkInfo.displayFlow 的邻近主河查询 */
  displayFlowWanted = false;
  readonly climateWeights: ClimateWeights;
  readonly generation: WorldGeneration;
  readonly waterBoundaries = new ChunkWaterBoundaries();
  private readonly planet: PlanetTerrain | null;
  private readonly warp: NoiseFunction2D;
  private readonly detail: NoiseFunction2D;
  private readonly climate: ClimateLayer;
  private readonly layers: TerrainLayers;
  private readonly lakes: BasinLakes;
  private readonly rivers: RiverNetwork;
  private readonly patches = new Map<string, TerrainPatch>();
  private readonly nodes = new Map<string, LandNode>();
  private readonly infos = new Map<string, ChunkInfo>();
  private readonly hydraulicNodes = new Map<string, HydraulicNode>();
  private readonly riverPositions=new Map<string,{x:number;z:number}>();
  private readonly channelPaths = new Map<string, readonly (readonly RiverPoint[])[]>();
  private readonly profileCells = new Map<string, number[]>();
  private readonly segments = new Map<string, RiverReach[]>();
  private readonly fanCells = new Map<string, AlluvialFan[]>();
  private readonly pondLevels=new Map<string,number>();
  private readonly waterMargins = new Map<string, number>();
  private readonly basinDepths = new Map<string, number>();

  constructor(seed: number, climateWeights: ClimateWeights = DEFAULT_CLIMATE_WEIGHTS, generation?: WorldGeneration) {
    this.seed = seed >>> 0;
    this.climateWeights = normalizeClimateWeights(climateWeights);
    this.generation = normalizeGeneration(generation);
    this.planet = this.generation.mode === 'planet' ? new PlanetTerrain(this.generation.planet, this.seed, this.generation.landRatio) : null;
    const rng = mulberry32(this.seed);
    const make = () => createNoise2D(mulberry32(Math.floor(rng() * 4294967296)));
    this.warp = make(); this.detail = make();
    this.climate = new ClimateLayer(this.seed, this.climateWeights, this.generation);
    this.layers = this.planet?.layers ?? new TerrainLayers(this.seed, false, this.generation.landRatio ?? .5);
    // 大尺度流域先求解
    this.rivers = new RiverNetwork((x, z) => ({...this.getTerrainSample(x, z), temperature: this.getClimate(x, z).temperature}),
      this.generation.precipitation, this.planet ? this.generation.planet.equatorChunks : undefined,
      (x, z) => this.periodic(this.warp, x / 32, z / 32, 32) * .7 + this.periodic(this.detail, x / 96, z / 96, 96) * .3,
      (x,z)=>({...this.getTerrainSample(x,z,false),temperature:this.getClimate(x,z).temperature}),this.seed);
    this.lakes = new BasinLakes(this.rivers);
  }

  getClimate(cx: number, cz: number): {index: number; temperature: number} {
    return this.climate.sample(cx, cz);
  }

  getTerrainSample(cx: number, cz: number, rainfall = true): TerrainSample {
    return this.planet?.sample(cx, cz, rainfall) ?? this.layers.sample([cx / CONTINENT_SCALE, 0, cz / CONTINENT_SCALE]);
  }

  private node(cx: number, cz: number): LandNode {
    const k = key(cx, cz), cached = this.nodes.get(k);
    if (cached) return cached;
    const result = {cx, cz, ...this.getTerrainSample(cx, cz), temperature: this.getClimate(cx, cz).temperature};
    this.nodes.set(k, result);
    return result;
  }

  getChunkInfo(cx: number, cz: number): ChunkInfo {
    // 在完整查询之间释放缓存
    if (this.nodes.size > 120000) {
      this.nodes.clear(); this.rivers.clear(); this.infos.clear();
      this.segments.clear(); this.patches.clear(); this.profileCells.clear(); this.hydraulicNodes.clear();
      this.riverPositions.clear();
      this.lakes.clear();
      this.channelPaths.clear();
      this.fanCells.clear();
      this.pondLevels.clear();
      this.waterMargins.clear(); this.basinDepths.clear();
    }
    const k = key(cx, cz);
    const cached = this.infos.get(k);
    if (cached) {
      // 命中缓存时才按需补算显示流向
      if (cached.displayFlow < 0 && this.displayFlowWanted && cached.flow < 0 && cached.lakeLevel === null && cached.type !== 11 &&
          isRiverType(cached.type) && cached.courseVal > 0) cached.displayFlow = this.rivers.corridorDirection(cx, cz);
      return cached;
    }
    const climate = this.getClimate(cx, cz);
    const n = this.node(cx, cz);
    const zone = CLIMATES[climate.index].zone;
    const flow = this.rivers.flow(cx, cz);
    const candidateLake = this.lakes.get(cx, cz);
    const lake = candidateLake && n.height < candidateLake.level ? candidateLake : null;
    const phase=freezeState(n.height,climate.temperature),wetland=this.rivers.wetland(cx,cz);
    const main=n.height>0&&this.rivers.isChannel(cx,cz);
    // 走廊值只查一次；湿地闸门改判 011 时仍保留该值，河区块才有流向
    const rawCourse=n.height>0?this.rivers.corridor(cx,cz):0;
    const corridor=wetland>.7?0:rawCourse;
    const course=corridor>0?corridor:wetland>.7?rawCourse:0;
    const discharge=n.height>0?Math.max(this.rivers.accumulation(cx,cz),corridor):0;
    const river=main||corridor>0;
    const riverBlocks=river?this.rivers.widthBlocks(cx,cz):0;
    const fall=main?this.rivers.waterfall(cx,cz):0;
    // 基础地形在所有气候带都可出现
    const basic = this.periodic(this.detail, cx / 5 + 20, cz / 5 - 20, 5) > 0.36 || zone === 0;
    const prefix = basic ? 0 : zone;
    let type: number;
    if (n.height <= 0) type = prefix + 2;
    else if (phase==='frozen') type=n.hillRelief>64?406:409;
    else if (lake) type = prefix + 2;
    else if (river) {
      type=phase!=='liquid'?prefix+8:fall?10:5;
    }
    else if(phase==='liquid'&&wetland>.12)type=11;
    else if(climate.temperature>=10&&n.moisture<-.32)type=104;
    else if (n.rift > 0.65 && this.waterMargin(cx, cz) > .5) type = 9;
    else if (n.plateau > 0.5) type = prefix + 8;
    else if (n.hillRelief > 32) type = prefix + 3;
    else if (n.valley > 0.5) type = prefix + 7;
    else if (basic) type = n.moisture < -0.28 ? 4 : n.moisture > 0.22 ? 6 : 1;
    else if (zone === 100) type = n.height < 14 && n.moisture > 0.15 ? 105 : n.moisture < -0.35 ? 104 : n.moisture < 0 ? 101 : n.moisture < 0.35 ? 109 : 106;
    else if (zone === 400) type = n.moisture > 0.4 ? 409 : n.moisture < -0.3 ? 404 : n.moisture < 0.1 ? 401 : 405;
    else type = zone + (n.moisture < -0.3 ? 4 : n.moisture < 0.05 ? 1 : n.moisture < 0.36 ? 5 : 6);
    const d = flow < 0 ? null : FLOW_DIRECTIONS[flow];
    // 河槽带区块沿用相邻主河道流向，边缘河格也有流向（011 沼泽静水仍无流向）
    const courseFlow = main ? flow : this.displayFlowWanted && course > 0 && type !== 11 && isRiverType(type) ? this.rivers.corridorDirection(cx, cz) : -1;
    const info = {
      cx, cz, type, climate: climate.index, temperature: climate.temperature,
      elevation: n.height, elevationTier: n.elevationTier, largeLandmass: n.largeLandmass, flow: main && !lake ? flow : -1, discharge, lakeLevel: lake?.level ?? null,
      plateBase:n.plateBase,hillRelief:n.hillRelief,highlandRelief:n.highlandRelief,waterfallDrop:fall,wetland,
      riverWidth: lake ? riverWidth(lake.discharge) * 2 : river ? riverWidth(discharge,riverBlocks) * 2 : 0,
      riverDepth: river && !lake ? this.bedDepth(discharge, riverBlocks, (cx + .5) * CHUNK_SIZE, (cz + .5) * CHUNK_SIZE, this.rivers.level(cx, cz)) : 0,riverBlocks,
      downstream: main&&d ? { cx: cx + d.dx, cz: cz + d.dz } : null,
      displayFlow: courseFlow,
      courseVal: rawCourse,
    };
    this.infos.set(k, info);
    return info;
  }

  getChunkType(cx: number, cz: number): number { return this.getChunkInfo(cx, cz).type; }

  /** 全球 LOD 不追踪汇水 */
  getOverviewInfo(cx: number, cz: number): ChunkInfo {
    const c = this.getClimate(cx, cz), n = this.getTerrainSample(cx, cz, false);
    const zone = CLIMATES[c.index].zone;
    const type = n.height <= 0 ? zone + 2 : freezeState(n.height,c.temperature)==='frozen'?409
      :c.temperature>=10&&n.moisture<-.32?104:n.hillRelief>32?zone+3:n.plateau>.5?zone+8:n.moisture>.2?zone+6:zone+1;
    return { cx, cz, type, climate: c.index, temperature: c.temperature, elevation: n.height,
      elevationTier: n.elevationTier, largeLandmass: n.largeLandmass,plateBase:n.plateBase,hillRelief:n.hillRelief,highlandRelief:n.highlandRelief,waterfallDrop:0,wetland:0,
      flow: -1, discharge: 0, riverWidth: 0, riverDepth: 0,riverBlocks:0, lakeLevel: null, downstream: null, displayFlow: -1, courseVal: 0 };
  }

  /** 噪声在经度方向平滑闭合 */
  private periodic(noise: NoiseFunction2D, x: number, z: number, scale: number, blocks = false): number {
    if (!this.planet) return noise(x, z);
    const period = this.generation.planet.equatorChunks * (blocks ? CHUNK_SIZE : 1) / scale;
    const wrapped = ((x % period) + period) % period;
    return mix(noise(wrapped, z), noise(wrapped - period, z), smooth(wrapped / period));
  }

  private wrapBlockX(x: number): number {
    if (!this.planet) return x;
    const period = this.generation.planet.equatorChunks * CHUNK_SIZE;
    return ((x + period / 2) % period + period) % period - period / 2;
  }

  private riverPosition(cx: number, cz: number): { x: number; z: number } {
    const k=key(cx,cz),cached=this.riverPositions.get(k);if(cached)return cached;
    const guide=riverGuide(cx,cz,{
      upstream:(x,z)=>this.rivers.dominantUpstream(x,z),
      downstream:(x,z)=>{const f=this.rivers.flow(x,z);return f<0?null:[x+FLOW_DIRECTIONS[f].dx,z+FLOW_DIRECTIONS[f].dz];},
    });
    const info=this.getChunkInfo(cx,cz),next=info.downstream;
    const grade=next?Math.max(0,this.riverLevel(info)-this.riverLevel(this.getChunkInfo(next.cx,next.cz)))/CHUNK_SIZE:0;
    const amplitude=RIVER_NODE_NOISE/(1+grade*6);
    // 连续世界场只扰动本区块内的细节
    const signal=this.periodic(this.warp,guide.x/11+71,guide.z/11,11)*.65+
      this.periodic(this.detail,guide.x/21,guide.z/21-53,21)*.35;
    const offset=amplitude*signal;
    const dx=(guide.x-cx)*CHUNK_SIZE-guide.tz*offset,dz=(guide.z-cz)*CHUNK_SIZE+guide.tx*offset;
    const result={x:(cx+.5)*CHUNK_SIZE+dx,z:(cz+.5)*CHUNK_SIZE+dz};this.riverPositions.set(k,result);return result;
  }

  /** 主支流共享节点切线 */
  private riverTangent(cx: number, cz: number): { x: number; z: number } {
    const guide=riverGuide(cx,cz,{
      upstream:(x,z)=>this.rivers.dominantUpstream(x,z),
      downstream:(x,z)=>{const f=this.rivers.flow(x,z);return f<0?null:[x+FLOW_DIRECTIONS[f].dx,z+FLOW_DIRECTIONS[f].dz];},
    });
    return {x:guide.tx,z:guide.tz};
  }

  /** 河宽受已分配的河流/湿地走廊约束；普通陆地仅保留8格岸线混合。 */
  private corridorWidth(wx:number,wz:number,width:number):number{
    const cx=Math.floor(wx/CHUNK_SIZE),cz=Math.floor(wz/CHUNK_SIZE),radius=Math.ceil(width/CHUNK_SIZE)+1;
    let available=width*1.08;
    for(let z=cz-radius;z<=cz+radius;z++)for(let x=cx-radius;x<=cx+radius;x++){
      const left=x*CHUNK_SIZE+RIVER_BANK_BLEND,right=(x+1)*CHUNK_SIZE-RIVER_BANK_BLEND;
      const top=z*CHUNK_SIZE+RIVER_BANK_BLEND,bottom=(z+1)*CHUNK_SIZE-RIVER_BANK_BLEND;
      const distance=Math.hypot(Math.max(left-wx,0,wx-right),Math.max(top-wz,0,wz-bottom));
      if(distance>=available)continue;
      const info=this.getChunkInfo(x,z);
      if(isRiverType(info.type)||info.type===11||info.elevation<=SEA_LEVEL||info.lakeLevel!==null)continue;
      available=distance;
    }
    // 留足逐列7%岸线噪声的余量
    return Math.max(.8,Math.min(width,available/1.08));
  }

  /** 真实河道中心线 */
  getRiverPath(cx: number, cz: number): readonly RiverPoint[] {
    return this.getRiverPaths(cx, cz)[0] ?? [];
  }

  /** 一个河段可有多条分汊 */
  getRiverPaths(cx: number, cz: number): readonly (readonly RiverPoint[])[] {
    const k = key(cx, cz), cached = this.channelPaths.get(k);
    if (cached) return cached;
    const center = this.centerRiverPath(cx, cz);
    if (!center.length) { this.channelPaths.set(k, []); return []; }
    // 分汊需要跨多个区块的真实分流结构
    const result = [center];
    this.channelPaths.set(k, result); return result;
  }

  private centerRiverPath(cx: number, cz: number): readonly RiverPoint[] {
    const a = this.getChunkInfo(cx, cz);
    if (a.elevation <= 0 || !isRiverType(a.type) || a.flow<0 || a.lakeLevel !== null || freezeState(a.elevation,a.temperature)!=='liquid') return [];
    const next=a.downstream?this.getChunkInfo(a.downstream.cx,a.downstream.cz):a;
    const b=next.elevation<=SEA_LEVEL||next.lakeLevel!==null||isRiverType(next.type)||next.type===11?next:a;
    const start = this.riverPosition(cx, cz), end = this.riverPosition(b.cx, b.cz);
    const bLevel = this.riverLevel(b), aLevel = this.riverLevel(a);
    if (a === b) {
      const result = [{ ...start, level: aLevel, width: this.corridorWidth(start.x,start.z,riverWidth(a.discharge,a.riverBlocks)), depth: this.bedDepth(a.discharge,a.riverBlocks,start.x,start.z,aLevel), discharge: a.discharge }];
      return result;
    }
    const length = Math.hypot(end.x - start.x, end.z - start.z);
    const ta = this.riverTangent(cx, cz), tb = this.riverTangent(b.cx, b.cz);
    const grade = (aLevel - bLevel) / length;
    const receiver = b.elevation <= SEA_LEVEL || b.lakeLevel !== null;
    const calm = receiver && a.type !== 10 ? 1 - smooth(grade / .3) : 0;
    const hasRiverUpstream = this.rivers.upstream(cx, cz).some(([x, z]) => this.rivers.isChannel(x, z));
    const startWidth = hasRiverUpstream ? riverWidth(a.discharge,a.riverBlocks) : .8;
    const aBed=this.bedDepth(a.discharge,a.riverBlocks,start.x,start.z,aLevel);
    const bBed=this.bedDepth(Math.max(a.discharge,b.discharge),Math.max(a.riverBlocks,b.riverBlocks),end.x,end.z,bLevel);
    const startDepth = hasRiverUpstream ? aBed : .8;
    const fallDrop = aLevel - bLevel;
    const fallBand = a.type === 10 && fallDrop >= FALL_CONCENTRATION_DROP
      ? clamp(fallDrop / (CHUNK_SIZE * 2.4), .3, .6) : 0;
    const points: RiverPoint[] = [];
    for (let i = 0; i <= 32; i++) {
      const t = i / 32;
      // 支流在靠近汇入口时才展开
      const mouth = smooth((t - .72) / .28), source = smooth(t / .6);
      const discharge = receiver ? a.discharge : mix(a.discharge, Math.max(a.discharge, b.discharge), mouth);
      const estuary = calm * smooth((t - .4) / .6);
      const width = mix(mix(startWidth, riverWidth(a.discharge,a.riverBlocks), source), riverWidth(discharge,Math.max(a.riverBlocks,b.riverBlocks)), mouth);
      const depth = mix(mix(startDepth, aBed, source), bBed, mouth);
      const position=riverCurve(start,end,ta,tb,t,CHUNK_SIZE);
      points.push({
        ...position,
        level: i === 0 ? aLevel : i === 32 ? bLevel : mix(aLevel, bLevel,
          fallBand ? smooth((t - .5) / fallBand + .5) : receiver ? 1 - (1 - t) ** 2 : t),
        width: this.corridorWidth(position.x,position.z,Math.min(MAX_RIVER_RADIUS, width * (1 + estuary * .5))),
        depth: mix(depth, Math.max(1.2, depth * .45 + .8), estuary) * (receiver && a.type === 10 ? 1 + mouth * .15 : 1),
        discharge, mouth: estuary,
      });
    }
    if (receiver) {
      // 水下河床沿入水方向延续
      const endpoint = points[32], previous = points[28];
      const terminalLength = Math.hypot(endpoint.x - previous.x, endpoint.z - previous.z);
      const dx = terminalLength ? (endpoint.x - previous.x) / terminalLength : (end.x - start.x) / length;
      const dz = terminalLength ? (endpoint.z - previous.z) / terminalLength : (end.z - start.z) / length;
      const apronLength = Math.min(MAX_MOUTH_LENGTH, 14 + riverWidth(a.discharge,a.riverBlocks) * 1.4);
      for (let i = 1; i <= 8; i++) {
        const t = i / 8, x = endpoint.x + dx * apronLength * t, z = endpoint.z + dz * apronLength * t;
        const cell = this.getChunkInfo(Math.floor(x / CHUNK_SIZE), Math.floor(z / CHUNK_SIZE));
        if (b.lakeLevel !== null ? cell.lakeLevel !== b.lakeLevel : cell.elevation > SEA_LEVEL) break;
        points.push({...endpoint, x, z, level: bLevel,
          width: Math.min(MAX_RIVER_RADIUS, endpoint.width * (1 + calm * .2 * Math.sin(Math.PI * t))),
          mouth: calm * (1 - smooth(t))});
      }
    }
    return points;
  }

  private riverLevel(info: ChunkInfo): number {
    return info.elevation <= SEA_LEVEL ? SEA_LEVEL : info.lakeLevel ?? this.rivers.level(info.cx, info.cz);
  }

  /** 邻域宏观地势高出水面的量：河床可下切的上限由它决定 */
  private valleyRelief(x: number, z: number, level: number): number {
    let high = -Infinity;
    for (const [dx, dz] of VALLEY_PROBES) {
      const fx = (x + .5 + dx) / CHUNK_SIZE - .5, fz = (z + .5 + dz) / CHUNK_SIZE - .5;
      const ix = Math.floor(fx), iz = Math.floor(fz), tx = fx - ix, tz = fz - iz;
      const rows = this.patch(ix, iz).rows;
      const at = (i: number) => cubic(rows[i][0], rows[i][1], rows[i][2], rows[i][3], tx);
      high = Math.max(high, cubic(at(0), at(1), at(2), at(3), tz));
    }
    return Math.max(0, high - level);
  }

  /** 分级上限随地势缩减：平地大河不成深渊 */
  private bedDepth(discharge: number, blocks: number, x: number, z: number, level: number): number {
    const depth = riverDepth(discharge, blocks);
    const relief = this.valleyRelief(x, z, level);
    return depth * (RIVER_BED_MIN_SHARE + (1 - RIVER_BED_MIN_SHARE) * clamp(relief / (depth * 1.6)));
  }

  private hydraulicNode(cx: number, cz: number): HydraulicNode {
    const k = key(cx, cz), cached = this.hydraulicNodes.get(k);
    if (cached) return cached;
    const node = {...this.riverPosition(cx, cz), level: this.riverLevel(this.getChunkInfo(cx, cz)),confluence:this.rivers.upstream(cx,cz).length>1};
    this.hydraulicNodes.set(k, node); return node;
  }

  private riverSegments(cx: number, cz: number): RiverReach[] {
    const k = key(cx, cz), cached = this.segments.get(k);
    if (cached) return cached;
    const result: RiverReach[] = [];
    // 查询范围由最宽河道、分汊和完整滩地半径决定
    for (let z = cz - RIVER_SEARCH_RADIUS; z <= cz + RIVER_SEARCH_RADIUS; z++) for (let x = cx - RIVER_SEARCH_RADIUS; x <= cx + RIVER_SEARCH_RADIUS; x++) {
      for (const points of this.getRiverPaths(x, z)) {
        const segments: RiverSegment[] = [];
        for (let i = 0; i < Math.max(0, points.length - 1) || i === 0 && points.length === 1; i++) {
          const a = points[i], b = points[Math.min(i + 1, points.length - 1)];
          const lake = points.length === 1;
          const relief = Math.max(0,this.node(x,z).height-Math.min(a.level,b.level));
          const shoulder = Math.min(MAX_VALLEY_SHOULDER,Math.sqrt(relief)*9);
          const nominal = Math.max(a.width, b.width);
          const radius = riverInfluence(nominal * (lake ? 1.18 : 1.07)) + riverInfluenceMargin(nominal, lake) + shoulder;
          const minX = Math.min(a.x, b.x) - radius, maxX = Math.max(a.x, b.x) + radius;
          const minZ = Math.min(a.z, b.z) - radius, maxZ = Math.max(a.z, b.z) + radius;
          if (maxX < cx * CHUNK_SIZE - 1 || minX > (cx + 1) * CHUNK_SIZE + 1 ||
              maxZ < cz * CHUNK_SIZE - 1 || minZ > (cz + 1) * CHUNK_SIZE + 1) continue;
          const length2 = (b.x - a.x) ** 2 + (b.z - a.z) ** 2;
          segments.push({ ax: a.x, az: a.z, bx: b.x, bz: b.z, aLevel: a.level, bLevel: b.level,
            aWidth: a.width, bWidth: b.width, length2, minX, maxX, minZ, maxZ, lake,
            aDepth: a.depth, bDepth: b.depth,
            aMouth: a.mouth ?? 0, bMouth: b.mouth ?? 0,
            grade: length2 ? (a.level - b.level) / Math.sqrt(length2) : 0, shoulder, discharge: a.discharge });
        }
        if (segments.length) {
          const info = this.getChunkInfo(x, z), downstream = info.downstream ?? info;
          result.push({ segments, start: this.hydraulicNode(x, z), end: this.hydraulicNode(downstream.cx, downstream.cz) });
        }
      }
    }
    this.segments.set(k, result); return result;
  }

  /** 所有地形使用相同的噪声扰动边界、平滑权重 */
  getTerrainWeights(wx: number, wz: number): TerrainWeight[] {
    wx = this.wrapBlockX(wx);
    const qx = (wx + .5 + this.periodic(this.warp, wx / 170 + 8, wz / 170, 170, true) * 13) / CHUNK_SIZE - .5;
    const qz = (wz + .5 + this.periodic(this.detail, wx / 170, wz / 170 - 8, 170, true) * 13) / CHUNK_SIZE - .5;
    const ix = Math.floor(qx), iz = Math.floor(qz), k = key(ix, iz);
    let types = this.profileCells.get(k);
    if (!types) {
      types = [this.getChunkType(ix, iz), this.getChunkType(ix + 1, iz),
        this.getChunkType(ix, iz + 1), this.getChunkType(ix + 1, iz + 1)];
      this.profileCells.set(k, types);
    }
    const tx = smooth(qx - ix), tz = smooth(qz - iz);
    return [{ type: types[0], weight: (1 - tx) * (1 - tz) }, { type: types[1], weight: tx * (1 - tz) },
      { type: types[2], weight: (1 - tx) * tz }, { type: types[3], weight: tx * tz }];
  }

  getTerrainBlend(wx: number, wz: number): TerrainProfile { return mixTerrainProfiles(this.getTerrainWeights(wx, wz)); }

  /** 地图与生成列都可读取真实扇面 */
  getAlluvialFans(cx: number, cz: number): readonly AlluvialFan[] {
    const k = key(cx, cz), cached = this.fanCells.get(k);
    if (cached) return cached;
    const watershed = this.rivers.watershed, [gx, gz] = watershed.coordinates(cx, cz);
    const radius = Math.ceil(24 / watershed.step) + 1, result: AlluvialFan[] = [];
    for (let z = gz - radius; z <= gz + radius; z++) for (let x = gx - radius; x <= gx + radius; x++) {
      const [px, pz] = watershed.position(x, z), fan = this.rivers.fan(px, pz);
      if (!fan) continue;
      let dx = fan.x - (cx + .5) * CHUNK_SIZE;
      if (this.planet) {
        const period = this.generation.planet.equatorChunks * CHUNK_SIZE;
        dx = ((dx + period / 2) % period + period) % period - period / 2;
      }
      const copy = {...fan, x: (cx + .5) * CHUNK_SIZE + dx};
      const midX = copy.x + copy.dx * copy.length * .5, midZ = copy.z + copy.dz * copy.length * .5;
      if (Math.abs(midX - (cx + .5) * CHUNK_SIZE) > copy.length / 2 + copy.width + CHUNK_SIZE ||
          Math.abs(midZ - (cz + .5) * CHUNK_SIZE) > copy.length / 2 + copy.width + CHUNK_SIZE) continue;
      if (!result.some(existing => existing.x === copy.x && existing.z === copy.z)) result.push(copy);
    }
    this.fanCells.set(k, result); return result;
  }

  private patch(cx: number, cz: number): TerrainPatch {
    const k = key(cx, cz);
    const cached = this.patches.get(k);
    if (cached) return cached;
    const rows: number[][] = [], lakes = new Set<BasinLake>();
    for (let z = -1; z <= 2; z++) {
      const row: number[] = [];
      for (let x = -1; x <= 2; x++) {
        row.push(this.node(cx + x, cz + z).height);
        const lake = this.lakes.get(cx + x, cz + z);
        if (lake) lakes.add(lake);
      }
      rows.push(row);
    }
    const patch = { rows, lakes: [...lakes] }; this.patches.set(k, patch); return patch;
  }

  private waterMargin(cx: number, cz: number): number {
    const k = key(cx, cz), cached = this.waterMargins.get(k);
    if (cached !== undefined) return cached;
    let distance = 4;
    for (let dz = -4; dz <= 4; dz++) for (let dx = -4; dx <= 4; dx++) {
      const d = Math.hypot(dx, dz);
      if (d >= distance) continue;
      const x = cx + dx, z = cz + dz;
      if (this.node(x, z).height <= 0 || this.lakes.get(x, z) ||
          this.rivers.isChannel(x, z) || this.rivers.corridor(x, z) > 0 || this.rivers.wetland(x, z) > .12) distance = d;
    }
    const margin = smooth((distance - 1) / 3);
    this.waterMargins.set(k, margin); return margin;
  }

  private basinDepth(cx: number, cz: number, level: number, discharge: number): number {
    const k = `${cx},${cz},${level},${discharge}`, cached = this.basinDepths.get(k);
    if (cached !== undefined) return cached;
    let depth = Math.max(8, riverDepth(discharge) * 1.25);
    for (let dz = -8; dz <= 8; dz++) for (let dx = -8; dx <= 8; dx++) {
      const distance = Math.hypot(dx, dz);
      if (distance >= 8) continue;
      const x = cx + dx, z = cz + dz;
      if (!this.rivers.isChannel(x, z)) continue;
      const riverLevel = this.rivers.level(x, z);
      if (riverLevel < level - 1 || riverLevel > level + 8) continue;
      const target = riverDepth(this.rivers.accumulation(x, z), this.rivers.widthBlocks(x, z)) * 1.25;
      depth = Math.max(depth, target * (1 - smooth((distance - 4) / 4)));
    }
    this.basinDepths.set(k, depth); return depth;
  }

  /** 双三次地势保持坡度连续 */
  private pondLevel(wx:number,wz:number):number{
    const x=this.wrapBlockX(wx),z=wz,k=key(x,z),cached=this.pondLevels.get(k);if(cached!==undefined)return cached;
    const fx=(x+.5)/CHUNK_SIZE-.5,fz=(z+.5)/CHUNK_SIZE-.5,ix=Math.floor(fx),iz=Math.floor(fz),tx=fx-ix,tz=fz-iz;
    const rows=this.patch(ix,iz).rows,row=(i:number)=>cubic(rows[i][0],rows[i][1],rows[i][2],rows[i][3],tx);
    let level=Math.max(1,cubic(row(0),row(1),row(2),row(3),tz)-.7),nearest=Infinity;
    for(const reach of this.riverSegments(Math.floor(x/CHUNK_SIZE),Math.floor(z/CHUNK_SIZE)))for(const s of reach.segments){
      const dx=s.bx-s.ax,dz=s.bz-s.az,t=s.length2?clamp(((x-s.ax)*dx+(z-s.az)*dz)/s.length2):0;
      const distance=Math.hypot(x-mix(s.ax,s.bx,t),z-mix(s.az,s.bz,t));
      if(distance>=nearest||distance>riverInfluence(mix(s.aWidth,s.bWidth,t)))continue;
      nearest=distance;level=Math.min(Math.max(1,cubic(row(0),row(1),row(2),row(3),tz)-.7),mix(s.aLevel,s.bLevel,t)+.45);
    }
    this.pondLevels.set(k,level);return level;
  }

  /** 岸线形态场：两侧共用，天然左右不对称。 */
  private bankCharacter(x: number, z: number): number {
    const regional = this.periodic(this.warp, x / 260 + 113, z / 260 - 71, 260, true);
    const reach = this.periodic(this.detail, x / 96 - 37, z / 96 + 59, 96, true);
    const bar = this.periodic(this.warp, x / 42 + 17, z / 42 - 23, 42, true);
    const ripple = this.periodic(this.detail, x / 17 - 91, z / 17 + 43, 17, true);
    return Math.tanh((regional * .56 + reach * .26 + bar * .12 + ripple * .06) * 2.4);
  }

  /** 河槽之外生成独立浅潭与露滩。 */
  private column(wx: number, wz: number): ColumnSample {
    const x = this.wrapBlockX(Math.floor(wx)), z = Math.floor(wz);
    const fx = (x + .5) / CHUNK_SIZE - .5, fz = (z + .5) / CHUNK_SIZE - .5;
    const ix = Math.floor(fx), iz = Math.floor(fz), tx = fx - ix, tz = fz - iz;
    const patch = this.patch(ix, iz), rows = patch.rows;
    const row = (i: number) => cubic(rows[i][0], rows[i][1], rows[i][2], rows[i][3], tx);
    // 权重一次算出，随样本交给 generateChunk 复用（地表混合与地质层都需要）
    const weights = this.getTerrainWeights(x, z);
    const blend = mixTerrainProfiles(weights);
    const regional = this.periodic(this.warp,x/1536+29,z/1536-19,1536,true);
    const strata = this.periodic(this.detail,x/384-61,z/384+37,384,true);
    const macroHeight = cubic(row(0), row(1), row(2), row(3), tz);
    const weather = this.periodic(this.detail,x/73-16,z/73+63,73,true);
    const baseHeight = macroHeight + geologicalRelief(regional,strata,macroHeight,weather) * Math.sin(Math.PI*tx)**2*Math.sin(Math.PI*tz)**2 +
      this.periodic(this.detail, x / 44, z / 44, 44, true) * blend.roughness * Math.sin(Math.PI * tx) ** 2 * Math.sin(Math.PI * tz) ** 2;
    let height = baseHeight, alluvium = 0;
    const interpolate = (sample: (cx: number, cz: number) => number) =>
      mix(mix(sample(ix, iz), sample(ix + 1, iz), smooth(tx)),
        mix(sample(ix, iz + 1), sample(ix + 1, iz + 1), smooth(tx)), smooth(tz));
    const rift = interpolate((cx, cz) => {
      const n = this.node(cx, cz), coast = smooth(n.coverage / .12);
      return coast > 0 ? n.rift / coast : 0;
    });
    if (rift > .65 && baseHeight > 1) {
      const clearance = interpolate((cx, cz) => this.waterMargin(cx, cz));
      height -= Math.min(64, baseHeight - 1) * smooth((rift - .65) / .3) * clearance;
    }
    const temperature=this.getClimate((x+.5)/CHUNK_SIZE-.5,(z+.5)/CHUNK_SIZE-.5).temperature;
    const phase=freezeState(baseHeight,temperature),wetland=phase==='liquid'?this.rivers.wetland(Math.floor(x/CHUNK_SIZE),Math.floor(z/CHUNK_SIZE)):0;
    // 出山口由窄谷展开为缓坡扇面
    for (const fan of this.getAlluvialFans(Math.floor(x / CHUNK_SIZE), Math.floor(z / CHUNK_SIZE))) {
      const dx = x + .5 - fan.x, dz = z + .5 - fan.z;
      const along = dx * fan.dx + dz * fan.dz, lateral = Math.abs(dx * fan.dz - dz * fan.dx);
      if (along <= 0 || along >= fan.length) continue;
      const spread = 24 + fan.width * smooth(along / fan.length);
      if (lateral >= spread) continue;
      const weight = (1 - smooth(lateral / spread)) * smooth(along / 64) * (1 - smooth((along / fan.length - .65) / .35));
      const surface = Math.max(1.5, fan.level - fan.grade * along + 1.8 + lateral * .004);
      height = mix(height, surface, weight * .92);
      alluvium = Math.max(alluvium, weight);
    }
    let water = baseHeight < 0 ? SEA_LEVEL : -Infinity;
    let kind: number = baseHeight < 0 ? WaterKind.SEA : WaterKind.DRY, vx=0, vz=0, discharge=0;
    const floodplainHeight=height;
    let nearest = Infinity, bank = 0, channel = 0, flowGrade = 0, mouth = 0, deposition = -Infinity, bedLimit = 0, limitWeight = 0, ownerBed = 0, ownerWeight = 0;
    const px = x + .5, pz = z + .5;
    const localInfo=this.getChunkInfo(Math.floor(x/CHUNK_SIZE),Math.floor(z/CHUNK_SIZE));
    const landCore=!isRiverType(localInfo.type)&&localInfo.type!==11&&localInfo.elevation>SEA_LEVEL&&localInfo.lakeLevel===null;
    const edgeDistance=Math.min(px-Math.floor(px/CHUNK_SIZE)*CHUNK_SIZE,(Math.floor(px/CHUNK_SIZE)+1)*CHUNK_SIZE-px,
      pz-Math.floor(pz/CHUNK_SIZE)*CHUNK_SIZE,(Math.floor(pz/CHUNK_SIZE)+1)*CHUNK_SIZE-pz);
    const coreWeight=landCore?smooth((edgeDistance-RIVER_BANK_BLEND*.5)/(RIVER_BANK_BLEND*.5)):0;
    const noise = this.periodic(this.detail, x / 37 + 21, z / 37 - 31, 37, true);
    let shape: ReturnType<typeof riverBankShape> | undefined;
    for (const reach of phase==='liquid'?this.riverSegments(Math.floor(x / CHUNK_SIZE), Math.floor(z / CHUNK_SIZE)):[]) {
      let chosen: RiverSegment | null = null, distance = Infinity, t = 0;
      // 每条曲线只使用最近投影
      for (const part of reach.segments) {
        if (px < part.minX || px > part.maxX || pz < part.minZ || pz > part.maxZ) continue;
        const dx = part.bx - part.ax, dz = part.bz - part.az;
        const u = part.length2 ? clamp(((px - part.ax) * dx + (pz - part.az) * dz) / part.length2) : 0;
        const d = Math.hypot(px - mix(part.ax, part.bx, u), pz - mix(part.az, part.bz, u));
        if (d < distance) { distance = d; t = u; chosen = part; }
      }
      if (!chosen) continue;
      const s = chosen;
      // 岸形只作用于断面外侧，深槽与水位不动
      shape ??= riverBankShape(this.bankCharacter(x, z));
      const targetWidth = mix(s.aWidth, s.bWidth, t) *
        (1 + noise * (s.lake ? RIVER_LAKE_EDGE_NOISE : RIVER_EDGE_NOISE) + shape.edgeShift);
      // 曲线离散与宽度插值在角点会有微小误差；连续收岸保证普通陆地核心不被淹没。
      const width=mix(targetWidth,Math.min(targetWidth,distance),coreWeight);
      const shore = riverShore(width) * shape.shoreScale;
      const floodplain = riverFloodplain(width) * shape.floodScale + s.shoulder;
      if (distance > width + shore + floodplain) continue;
      let level = mix(s.aLevel, s.bLevel, t);
      // 真正汇流口的小范围共用水位，避免主支流末端重叠时选择了不同断面。
      for (const node of [reach.start, reach.end]) {
        const d = Math.hypot(px - node.x, pz - node.z);
        const radius=node.confluence?12:3;
        if (d >= radius) continue;
        level = mix(level, node.level, (1 - smooth(d / radius)) * (node.confluence?1:.35));
      }
      const depth = mix(s.aDepth, s.bDepth, t);
      const r = distance / width;
      // 圆弧形深槽逐渐抬升至浅滩
      let bed: number;
      if (r < RIVER_BED_CORE) {
        const flat = riverBedFlat(width), rise = smooth(clamp((r - flat) / (RIVER_BED_CORE - flat)));
        // 河床地形：深潭浅滩、深泓偏移、床面沙丘
        const pool = this.periodic(this.warp, x / 210 + 61, z / 210 - 43, 210, true) * .85 +
          this.periodic(this.detail, x / 76 - 29, z / 76 + 83, 76, true) * .15;
        const swirl = this.periodic(this.detail, x / 118 + 97, z / 118 - 11, 118, true);
        const dune = this.periodic(this.warp, x / 15 + 37, z / 15 - 59, 15, true) * .7 +
          this.periodic(this.detail, x / 34 - 13, z / 34 + 71, 34, true) * .3;
        // 深潭与沙脊只抬升床面，分级深度上限永不被突破
        const relief = clamp(Math.max(0, pool * .3 + swirl * .15) - Math.min(0, dune) * .06, 0, .5);
        const groove = Math.max(.85, depth * (1 - relief));
        bed = level - .85 - (groove - .85) * (1 - rise);
      }
      else if (r < 1) bed = level - .85 + 1.35 * smooth((r - RIVER_BED_CORE) / (1 - RIVER_BED_CORE));
      else {
        const bankT = clamp((distance - width) / shore);
        const crest = 1.3 * shape.crestScale;
        bed = level + .5 + crest * smooth(bankT + shape.beachShift * Math.sin(Math.PI * bankT));
        const outer = smooth((distance - width - shore) / floodplain);
        bed = mix(bed, Math.max(bed, height), outer);
      }
      height = Math.min(height, bed);
      // 各河段河床按中心线邻近度叠加，供重叠处抬回被过度下切的地面。
      const limitWeightHere = Math.exp(-((distance / (riverShore(width) + 1)) ** 2));
      bedLimit += limitWeightHere * bed; limitWeight += limitWeightHere;
      // 供水河段的床面另按最近中心线加权，邻河深槽不得挖穿小河
      const ownerHere = Math.exp(-((distance / RIVER_BED_OWNER) ** 2));
      ownerBed += ownerHere * bed; ownerWeight += ownerHere;
      // 湿地是真实浅缓河岸
      if(wetland>.12&&r>.75&&distance<width+shore+floodplain){
        const weight=wetland*(1-smooth((distance-width)/Math.max(1,shore+floodplain)));
        height=mix(height,Math.min(height,level+.35+2.5*smooth((r-.8)/1.7)),weight*.9);
        alluvium=Math.max(alluvium,weight);
      }
      bank = Math.max(bank, 1 - smooth((distance - width * .75) / (shore + width * .5)));
      // 最近中心线提供唯一水位
      if (coreWeight<1&&distance < width && (distance < nearest - 1e-6 || Math.abs(distance - nearest) < 1e-6 && level < water)) {
        nearest = distance; water = level; channel = clamp(1 - r); flowGrade = s.grade;
        const length = Math.sqrt(s.length2), speed = flowSpeed(s.grade,s.discharge,width);
        vx = length ? (s.bx-s.ax)/length*speed : 0; vz = length ? (s.bz-s.az)/length*speed : 0;
        discharge = s.discharge; kind = s.grade > 2 ? WaterKind.FALL : WaterKind.RIVER;
        mouth = mix(s.aMouth, s.bMouth, t);
        // 河口两侧形成水下沉积浅滩
        deposition = baseHeight < level && mouth > 0 && r > .2
          ? mix(Math.min(baseHeight, bed), Math.min(level - .85, Math.max(baseHeight, bed)),
            mouth * smooth((r - .2) / .55) * .85) : -Infinity;
      }
    }
    // 瀑布上下游的宽断面会在平面上重叠
    // 否则下游低水位圆帽会把上游床挖深数十格
    if(limitWeight>0){
      const limit=Math.min(floodplainHeight,bedLimit/limitWeight,Number.isFinite(water)?water-.6:Infinity);
      if(limit>height)height=limit;
    }
    // 供水河段自己的床面是下限：邻河深槽不得把小河河道挖穿
    if(ownerWeight>0){
      const floor=Math.min(floodplainHeight,ownerBed/ownerWeight,Number.isFinite(water)?water-.6:Infinity);
      if(floor>height)height=floor;
    }
    if(localInfo.type===11&&phase==='liquid'){
      const cover=smooth((wetland-.12)/.4)*smooth(edgeDistance/8);
      alluvium=Math.max(alluvium,cover*(.65+noise*.2));bank=Math.max(bank,cover*.5);
      if(channel<.28&&cover>.2&&kind!==WaterKind.SEA){
        if(Number.isFinite(water))height=mix(height,Math.max(height,water+.3+noise*.2),cover*smooth((.28-channel)/.28));
        if(!Number.isFinite(water)||water-height<=.12){
          water=-Infinity;kind=WaterKind.DRY;channel=0;vx=0;vz=0;discharge=0;
          const pond=wetlandPond(px,pz,this.seed,this.planet?this.generation.planet.equatorChunks*CHUNK_SIZE:0);
          const ratio=pond.ratio*(1+this.periodic(this.detail,x/9+81,z/9-47,9,true)*.12);
          const level=this.pondLevel(pond.x,pond.z);
          if(ratio<1.45&&Math.abs(height-level)<6){
            const weight=cover*(1-smooth((ratio-1)/.45));
            height=mix(height,level-.95+1.6*smooth(ratio),weight);
            if(ratio<1&&height<level-.12&&level-height<=1.2){water=level;kind=WaterKind.STATIC;}
          }
        }
      }
    }
    // 湖面共用水平水位；同一水体跨粗格时取格窗最低溢出口
    for (const lake of patch.lakes) {
      const mask = (dx: number, dz: number) => this.lakes.contains(lake, ix + dx, iz + dz) ? 1 : 0;
      const coverage = mix(mix(mask(0, 0), mask(1, 0), tx), mix(mask(0, 1), mask(1, 1), tx), tz);
      if (coverage <= 0 || baseHeight >= lake.level || channel > 0 && water < lake.level - 1e-6) continue;
      let level = lake.level;
      for (let dz = 0; dz <= 1; dz++) for (let dx = 0; dx <= 1; dx++) {
        if (!mask(dx, dz)) continue;
        const cell = this.lakes.get(ix + dx, iz + dz);
        if (cell) level = Math.min(level, cell.level);
      }
      water = level; flowGrade = 0; kind = WaterKind.LAKE; vx=0; vz=0; discharge=lake.discharge;
      bank = Math.max(bank, 1 - smooth((level - baseHeight) / 5));
    }
    if (Number.isFinite(deposition)) height = Math.max(height, Math.min(deposition, water - .85));
    if ((water === SEA_LEVEL || kind === WaterKind.LAKE) && baseHeight < water) {
      const basinDischarge = water === SEA_LEVEL ? 0 : discharge;
      const depth = interpolate((cx, cz) => this.basinDepth(cx, cz, water, basinDischarge));
      height = Math.min(height, baseHeight - depth * smooth((water - baseHeight) / 1.5));
    }
    if (channel>0&&(localInfo.type!==11||channel>.28)&&Number.isFinite(water))height=Math.min(height,water-.85);
    // 保留连续高度
    // 主线程与 Worker 都使用 Float32
    height = Math.fround(clamp(height, WORLD_MIN_Y + 1, WORLD_MAX_Y));
    if (height < SEA_LEVEL && water < SEA_LEVEL) {water=SEA_LEVEL;kind=WaterKind.SEA;vx=0;vz=0;discharge=0;}
    let staticSurface=0;
    const waterPhase=freezeState(Math.max(baseHeight,Number.isFinite(water)?water:baseHeight),temperature);
    if(waterPhase!=='liquid'){
      vx=0;vz=0;discharge=0;
      if(Number.isFinite(water)&&height<water){
        staticSurface=waterPhase==='frozen'||noise>.15?Block.ICE:Block.WATER;
        if(staticSurface===Block.ICE){height=Math.fround(water);water=-Infinity;kind=WaterKind.DRY;}
        else kind=WaterKind.STATIC;
      }else if(waterPhase==='frozen')staticSurface=Block.SNOW;
    }
    const state=this.waterBoundaries.resolve(Math.floor(x/CHUNK_SIZE),Math.floor(z/CHUNK_SIZE),height,{level:water,kind,vx,vz,discharge});
    return { height, water:state.level, bank, channel, flowGrade, mouth, alluvium, kind:state.kind,vx:state.vx,vz:state.vz,discharge:state.discharge,staticSurface,weights };
  }

  getHeight(wx: number, wz: number): number { return this.column(wx, wz).height; }
  getWaterLevel(wx: number, wz: number): number { return this.column(wx, wz).water; }
  getWaterState(wx: number, wz: number): WaterState {
    const c=this.column(wx,wz);return {level:c.water,kind:c.kind,vx:c.vx,vz:c.vz,discharge:c.discharge};
  }

  findSpawnChunk(maxRadius = 64): { cx: number; cz: number } {
    if (this.generation.landRatio === 0 && (this.generation.mode === 'plane' || this.generation.planet.map !== 'earth')) return {cx: 0, cz: 0};
    // 地球从东亚附近寻找出生点
    let originX = 0, originZ = 0;
    if (!this.planet && this.getOverviewInfo(0, 0).elevation < 8) {
      search: for (let r = 1; r <= 32; r++) for (let z = -r; z <= r; z++) for (let x = -r; x <= r; x++) {
        if (Math.max(Math.abs(x), Math.abs(z)) !== r) continue;
        if (this.getOverviewInfo(x * 64, z * 64).elevation > 16) { originX = x * 64; originZ = z * 64; break search; }
      }
    }
    if (this.planet) {
      const size = this.generation.planet.equatorChunks;
      if (this.generation.planet.map === 'earth') { originX = Math.round(size * 115 / 360); originZ = Math.round(-size * 30 / 360); }
      else if (this.getOverviewInfo(0, 0).elevation < 8) {
        search: for (let z = -size / 8; z <= size / 8; z += size / 32) for (let x = -size / 2; x < size / 2; x += size / 32) {
          const cx = Math.round(x), cz = Math.round(z);
          if (this.getOverviewInfo(cx, cz).elevation > 15) { originX = cx; originZ = cz; break search; }
        }
      }
    }
    for (let r = 0; r <= maxRadius; r++) for (let z = -r; z <= r; z++) for (let x = -r; x <= r; x++) {
      if (Math.max(Math.abs(x), Math.abs(z)) !== r) continue;
      const cx = x + originX, cz = z + originZ;
      const info=this.getChunkInfo(cx,cz);
      // 出生安全以真实坡面和水体判断
      if(info.elevation<=0||info.riverWidth>0||info.lakeLevel!==null||info.type===11)continue;
      const wx = (cx + 0.5) * CHUNK_SIZE + 0.5, wz = (cz + 0.5) * CHUNK_SIZE + 0.5;
      const h = this.getHeight(wx, wz);
      if (h > this.getWaterLevel(wx, wz) && Math.abs(h - this.getHeight(wx + 2, wz)) <= 1 &&
          Math.abs(h - this.getHeight(wx, wz + 2)) <= 1) return { cx, cz };
    }
    throw new Error('未找到安全出生点，请更换种子。');
  }

  generateChunk(cx: number, cz: number): ChunkGenResult {
    const S = CHUNK_SIZE, P = PADDED_SIZE;
    const heights = new Float32Array(P * P), waterLevels = new Float32Array(P * P);
    const surfaces = new Uint32Array(S * S), surfaceColors = new Uint8Array(P * P * 3);
    const waterField: WaterField = {levels:waterLevels,kinds:new Uint8Array(P*P),velocities:new Float32Array(P*P*2),discharge:new Float32Array(P*P)};
    const samples: ColumnSample[] = [];
    for (let z = -1; z <= S; z++) for (let x = -1; x <= S; x++) {
      const column = this.column(cx * S + x, cz * S + z), i = (z + 1) * P + x + 1;
      heights[i] = column.height; waterLevels[i] = column.water; samples[i] = column;
      waterField.kinds[i]=column.kind;waterField.velocities[i*2]=column.vx;waterField.velocities[i*2+1]=column.vz;waterField.discharge[i]=column.discharge;
    }
    const heightAt = (x: number, z: number) => x >= -1 && x <= S && z >= -1 && z <= S
      ? heights[(z + 1) * P + x + 1] : this.getHeight(cx * S + x, cz * S + z);
    for (let z = -1; z <= S; z++) for (let x = -1; x <= S; x++) {
      const wx = this.wrapBlockX(cx * S + x), wz = cz * S + z, i = (z + 1) * P + x + 1;
      const sample = samples[i], h = sample.height;
      const slope = Math.max(Math.abs(h - heightAt(x - 1, z)), Math.abs(h - heightAt(x + 1, z)),
        Math.abs(h - heightAt(x, z - 1)), Math.abs(h - heightAt(x, z + 1)));
      const blend = mixTerrainProfiles(sample.weights);
      const rock = smooth((slope - 1) / 4);
      overlayTerrain(blend, rock, [129, 132, 122], [[Block.STONE, .8], [Block.GRAVEL, .2]]);
      const climate = this.getClimate((wx + .5) / S - .5, (wz + .5) / S - .5);
      const cold = 1 - smooth((climate.temperature + 3) / 6);
      const snow = Math.max(cold, altitudeSnow(h, climate.temperature)) * (1 - rock * .65);
      overlayTerrain(blend, snow, [226, 234, 226], [[Block.SNOW, 1]]);
      // 河岸、海岸、湖岸也采用同一环境层混合
      const coast = h < 5 ? 1 - smooth(Math.max(0, h) / 5) : 0;
      const shore = Math.max(sample.bank, coast);
      const wet = blend.wetness;
      const sediment: [number, number, number] = [mix(186, 127, wet), mix(174, 134, wet), mix(127, 99, wet)];
      overlayTerrain(blend, shore * .75 * (1 - snow * .65), sediment,
        [[Block.SAND, .45 * (1 - wet)], [Block.GRAVEL, .25], [Block.MUD, .3 + wet * .45]]);
      overlayTerrain(blend, sample.alluvium * .65 * (1 - snow), [165, 164, 113],
        [[Block.GRASS, .4], [Block.MUD, .35], [Block.SAND, .15], [Block.GRAVEL, .1]]);
      if (h < sample.water) {
        const coarse = smooth(sample.flowGrade / .25);
        const depth = sample.water - h;
        overlayTerrain(blend, .9, [mix(156, 116, coarse), mix(148, 124, coarse), mix(116, 119, coarse)],
          [[Block.SAND, (1 - coarse) * .6], [Block.MUD, (1 - coarse) * .4], [Block.GRAVEL, coarse]]);
        overlayTerrain(blend, sample.mouth * .65, [170, 157, 117], [[Block.SAND, .7], [Block.MUD, .3]]);
        const darken = 1 - Math.min(.16, depth * .018);
        blend.color = blend.color.map(v => v * darken) as TerrainProfile['color'];
      }
      const variation = 1 + this.periodic(this.detail, wx / 11 + 19, wz / 11, 11, true) * .035;
      const province=this.periodic(this.warp,wx/1536+29,wz/1536-19,1536,true), strata=this.periodic(this.detail,wx/384-61,wz/384+37,384,true);
      applyGeology(blend,sample.weights,province,strata,h,slope,h<sample.water,snow);
      if(sample.staticSurface){const id=sample.staticSurface;overlayTerrain(blend,1,[BLOCK_RGB[id*3],BLOCK_RGB[id*3+1],BLOCK_RGB[id*3+2]],[[id,1]]);}
      const materialColor=[0,0,0];
      for(const [id,weight] of blend.materials) for(let c=0;c<3;c++) materialColor[c]+=BLOCK_RGB[id*3+c]*weight;
      for (let c = 0; c < 3; c++) surfaceColors[i * 3 + c] = clamp(Math.round((blend.color[c]*.4+materialColor[c]*.6) * variation), 0, 255);
      if (x >= 0 && x < S && z >= 0 && z < S) {
        const pick = clamp(.5 + this.periodic(this.detail, wx / 6 - 17, wz / 6 + 33, 6, true) * .48);
        let cumulative = 0, block = Block.GRASS as number;
        for (const [id,weight] of blend.materials) {
          cumulative += weight;
          if (pick <= cumulative && weight > 0) { block = id; break; }
        }
        surfaces[z * S + x] = block;
      }
    }
    return { cx, cz, type: this.getChunkType(cx, cz), heights, surfaces, waterLevels, surfaceColors,waterField };
  }
}
