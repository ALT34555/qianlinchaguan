import { CHUNK_SIZE } from '../../../core/config';
import { WorldGenerator, PADDED_SIZE, PREVIEW_SIZE, type ChunkInfo, type ChunkGenResult, type PreviewChunk, type RiverPoint, type MacroRiverOverview } from '../WorldContract';
import { normalizeGeneration, temperatureClimate, type ClimateWeights, type WorldGeneration } from '../WorldSettings';
import { Block } from '../Blocks';
import { waterSeasonPhase,seasonTemperature } from '../WaterSeason';
import type {WaterAppearanceUpdate,WaterAppearanceEntry} from '../WaterAppearance';
import type {WaterFreezeRules} from '../WaterFreezing';
import { resolveWaterAppearance } from '../WaterAppearance';
import { FLOW_DIRECTIONS } from '../Hydrology';
import { WaterKind, flowSpeed, type WaterSample, type WaterField } from '../DynamicWater';
import type { TerrainSample } from '../TerrainLayers';
import { generateWorld, normalizeOptions, macroAt, validateWorld, fingerprint, MACRO_RULE, setRuleVersion, type MacroWorld, type MacroSurface } from './backend.mjs';
import { CURRENT_RULE_ID } from '../../../core/WorldRule';
import { macroHydrologyState, type WatershedState } from '../Watershed';
import { samplePlanetSurface } from './surface.mjs';
import {getTributaries,riverChunkLabel} from './source-rivers.mjs';
import {clipRiverSegment} from './river-geometry.mjs';
import {satelliteMap,satelliteColor} from './satellite.mjs';
import { cellAt } from './sphere-frame.mjs';
import { macroSeasonStrength } from './Settings';
import { hash2 } from '../../../core/math/Random';

const TILE_BYTES=8*1024*1024, FIELDS=18, S=CHUNK_SIZE;

export class PlanetMacroGenerator extends WorldGenerator {
  override readonly generation:WorldGeneration;
  private model?:MacroWorld;
  private overviewRivers?:MacroRiverOverview;
  private seasonPhase:number|null=null;
  private readonly appearances=new Map<string,ReturnType<typeof resolveWaterAppearance>>();
  private pending?:WatershedState;
  private readonly tiles=new Map<string,Float64Array>();
  private readonly features=new Map<string,{sample:MacroSurface;wetland:number;wx:number;wz:number}>();
  private tileBytes=0;
  private tileHits=0;
  private tileMisses=0;
  private spawnPoint?:{cx:number;cz:number};
  constructor(seed:number,weights:ClimateWeights,generation:WorldGeneration) {
    super(seed,weights,generation);
    this.generation=normalizeGeneration(generation);
    if(this.generation.terrainRule!=='planet-macro')throw Error('宏观星球规则不符');
    // 规则版本由 TS 侧单一真源注入后端快照
    setRuleVersion(CURRENT_RULE_ID);
  }
  private options() {
    const g=this.generation,p=g.planet;
    return normalizeOptions({seed:this.seed,columns:g.macroColumns??1024,equatorChunks:p.equatorChunks,
      activity:p.tectonicActivity,landRatio:g.landRatio??.3,precipitation:g.precipitation??1,
      equatorTemperature:p.equatorTemperature,poleTemperature:p.poleTemperature,recipe:g.macroTerrain??'balanced',
      flagCount:g.artificial?.enabled?Math.min(32,g.artificial.initialCivilizationLimit??8):0});
  }
  override deferHydrology(state:WatershedState):void {this.pending=state;}
  get macroWorld():MacroWorld {
    if(this.pending){const state=this.pending;this.pending=undefined;this.adoptHydrology(state);}
    return this.model??=generateWorld(this.options());
  }
  override prepareMacroHydrology():void {void this.macroWorld;}
  override get precipitationWorld():MacroWorld {return this.macroWorld;}
  override exposeHydrology():WatershedState {return macroHydrologyState(this.macroWorld);}
  override adoptHydrology(state:WatershedState):void {
    const w=state.planetMacro,o=this.options(),n=o.columns*o.columns/2;
    if(!w||w.rule!==MACRO_RULE||w.ruleVersion!==CURRENT_RULE_ID||JSON.stringify(w.options)!==JSON.stringify(o)||w.columns!==o.columns||w.rows!==o.columns/2||w.height.length!==n)
      throw Error('宏观快照与种子、参数或规则版本不符');
    for(const field of ['height','water','rank','receiver','flow','riverWidth','spill','moisture','temperature','areas','rainfall','evaporation','uplift','rift','shear','boundary','plate','catchment','lakeId','biome'] as const)
      if(!ArrayBuffer.isView(w[field])||w[field].length!==n)throw Error('宏观快照字段长度不符');
    if(w.circumference!==o.equatorChunks*S||!validateWorld(w).ok||fingerprint(w)!==w.fingerprint)throw Error('宏观快照校验失败');
    if(this.model===w)return;
    this.model=w;this.overviewRivers=undefined;this.pending=undefined;this.tiles.clear();this.features.clear();this.tileBytes=0;this.spawnPoint=undefined;
  }
  cacheStats() {return {tiles:this.tiles.size,bytes:this.tileBytes,limitBytes:TILE_BYTES,hits:this.tileHits,misses:this.tileMisses};}

  private coordinates(wx:number,wz:number):[number,number] {
    const C=this.generation.planet.equatorChunks*S,wrap=(v:number)=>((v+C/2)%C+C)%C-C/2;
    wx=wrap(wx);wz=wrap(wz);
    if(wz>C/4){wz=C/2-wz;wx=wrap(wx+C/2);}
    if(wz<-C/4){wz=-C/2-wz;wx=wrap(wx+C/2);}
    return [wx,wz];
  }
  private raw(wx:number,wz:number,cover=true):MacroSurface {
    [wx,wz]=this.coordinates(wx,wz);const w=this.macroWorld;
    if(wx-Math.floor(wx)===.5&&wz-Math.floor(wz)===.5){
      const cx=Math.floor(wx/S),cz=Math.floor(wz/S),tile=this.tiles.get(`${cx},${cz}`);
      if(tile)return this.packedPoint(tile,(Math.floor(wz)-cz*S)*S+Math.floor(wx)-cx*S);
    }
    return samplePlanetSurface(w,wx,wz,true,false,cover);
  }
  override setWaterSeason(phase:number|null):boolean {
    const next=waterSeasonPhase(phase);if(next===this.seasonPhase)return false;
    this.seasonPhase=next;this.appearances.clear();return true;
  }
  override getWaterSeason():number|null {return this.seasonPhase;}
  override getSurfaceTemperature(cx:number,cz:number):number {
    const [x,z]=this.coordinates((cx+.5)*S,(cz+.5)*S);
    return macroAt(this.macroWorld,x,z,'temperature')+macroSeasonStrength(this.generation.macroClimate)*seasonTemperature(this.seasonPhase,-z/this.macroWorld.circumference*Math.PI*2);
  }
  override getChunkWaterState(cx:number,cz:number) {
    const size=this.generation.planet.equatorChunks;
    if(cx< -size/2||cx>=size/2||cz< -size/4||cz>=size/4){
      const [x,z]=this.coordinates((cx+.5)*S,(cz+.5)*S);cx=Math.floor(x/S);cz=Math.floor(z/S);
    }
    const key=`${cx},${cz}`,cached=this.appearances.get(key);if(cached)return cached;
    const appearance=resolveWaterAppearance(this.getSurfaceTemperature(cx,cz),this.waterAppearances.get(key),this.waterFreezeRules);
    appearance.iceThickness=.3;
    if(this.appearances.size>=8192)this.appearances.clear();this.appearances.set(key,appearance);return appearance;
  }
  override setWaterAppearance(cx:number,cz:number,value:WaterAppearanceUpdate|null):void {super.setWaterAppearance(cx,cz,value);this.appearances.clear();}
  override replaceWaterAppearanceOverrides(entries:readonly WaterAppearanceEntry[]):void {super.replaceWaterAppearanceOverrides(entries);this.appearances.clear();}
  override setWaterFreezeRules(rules:Partial<WaterFreezeRules>):void {super.setWaterFreezeRules(rules);this.appearances.clear();}
  private packedPoint(tile:Float64Array,i:number):MacroSurface {
    i*=FIELDS;const kind=tile[i+3],world=this.macroWorld,cell=tile[i+9]%world.height.length;
    return {height:tile[i],water:tile[i+1],temperature:tile[i+2],kind,wetland:tile[i+4],cell,chunkCode:String(tile[i+15]).padStart(3,'0'),surfaceBlock:tile[i+16],surfaceColor:tile[i+17],
      river:tile[i+14]?{cell:tile[i+12],receiver:world.receiver[tile[i+12]],source:tile[i+9]>=world.height.length?'source':'macro',dischargeRole:tile[i+9]>=world.height.length?'decorative-estimate':'macro',visualWet:tile[i+10],visualDry:tile[i+11],directionEast:tile[i+5],directionNorth:tile[i+6],dischargeM3s:tile[i+7],widthM:tile[i+8],waterfallDropM:tile[i+13]}:null};
  }

  private packPoint(tile:Float64Array,i:number,s:MacroSurface):void {
    tile[i]=s.height;tile[i+1]=s.water;tile[i+2]=s.temperature;tile[i+3]=s.kind;tile[i+4]=s.wetland;
    tile[i+5]=s.river?.directionEast??0;tile[i+6]=s.river?.directionNorth??0;tile[i+7]=s.river?.dischargeM3s??0;tile[i+8]=s.river?.widthM??0;
    tile[i+9]=s.cell+(s.river?.source==='source'?this.macroWorld.height.length:0);tile[i+10]=s.river?.visualWet??s.water;tile[i+11]=s.river?.visualDry??s.water;
    tile[i+12]=s.river?.cell??s.cell;tile[i+13]=s.river?.waterfallDropM??0;tile[i+14]=s.river?1:0;tile[i+15]=Number(s.chunkCode??0);tile[i+16]=(s as any).surfaceBlock;tile[i+17]=(s as any).surfaceColor;
  }
  private rawTile(cx:number,cz:number):Float64Array {
    const k=`${cx},${cz}`,hit=this.tiles.get(k);
    if(hit){this.tiles.delete(k);this.tiles.set(k,hit);this.tileHits++;return hit;}
    this.tileMisses++;const tile=new Float64Array(S*S*FIELDS);let river:MacroSurface|undefined,wet:MacroSurface|undefined,maxWet=0,riverX=32.5,riverZ=32.5,wetX=32.5,wetZ=32.5;
    for(let z=0;z<S;z++)for(let x=0;x<S;x++){
      const s=this.raw(cx*S+x+.5,cz*S+z+.5),i=(z*S+x)*FIELDS;
      if(s.river&&(!river||s.river.dischargeM3s>river.river!.dischargeM3s)){river=s;riverX=x+.5;riverZ=z+.5;}
      if(s.wetland>maxWet){maxWet=s.wetland;wet=s;wetX=x+.5;wetZ=z+.5;}
      this.packPoint(tile,i,s);
    }
    this.features.set(k,{sample:river??wet??this.packedPoint(tile,32*S+32),wetland:maxWet,wx:cx*S+(river?riverX:wet?wetX:32.5),wz:cz*S+(river?riverZ:wet?wetZ:32.5)});
    this.tiles.set(k,tile);this.tileBytes+=tile.byteLength;
    while(this.tileBytes>TILE_BYTES){const oldest=this.tiles.keys().next().value!;this.tileBytes-=this.tiles.get(oldest)!.byteLength;this.tiles.delete(oldest);this.features.delete(oldest);}
    return tile;
  }
  override getClimate(cx:number,cz:number) {
    const [x,z]=this.coordinates((cx+.5)*S,(cz+.5)*S),w=this.macroWorld;
    const temperature=macroAt(w,x,z,'temperature')+Math.max(0,macroAt(w,x,z))*this.getWaterFreezeRules().altitudeLapseRate;
    return {index:temperatureClimate(temperature),temperature};
  }
  override getTerrainSample(cx:number,cz:number):TerrainSample {
    const [x,z]=this.coordinates((cx+.5)*S,(cz+.5)*S),w=this.macroWorld,h=macroAt(w,x,z),uplift=macroAt(w,x,z,'uplift');
    return {height:h,uplift,plateau:0,valley:0,rift:macroAt(w,x,z,'rift'),moisture:macroAt(w,x,z,'moisture')*2-1,
      coverage:h>0?1:-1,baseCoverage:h>0?1:-1,largeLandmass:h>0,elevationTier:h>1024?3:h>384?2:h>96?1:0,
      plateBase:h,plateEdge:0,hillRelief:uplift*32,highlandRelief:uplift*64,mountainRelief:uplift*128};
  }
  override getSatelliteColor(cx:number,cz:number,relief=true):number {
    const w=this.macroWorld,[x,z]=this.coordinates((cx+.5)*S,(cz+.5)*S),i=cellAt(w,x,z),map=satelliteMap(w);
    const rgb=relief?map.pixels.subarray(i*4,i*4+3):satelliteColor(map.codes[i],w.rainfall[i]);
    return (rgb[0]<<16)|(rgb[1]<<8)|rgb[2];
  }
  override getMacroRiverOverview():MacroRiverOverview {
    const w=this.macroWorld;
    return this.overviewRivers??={columns:w.columns,rows:w.rows,receiver:w.receiver,widths:w.riverWidth,discharge:w.flow,water:w.water};
  }
  override getOverviewInfo(cx:number,cz:number):ChunkInfo {
    const n=this.getTerrainSample(cx,cz),c=this.getClimate(cx,cz),[x,z]=this.coordinates((cx+.5)*S,(cz+.5)*S),w=this.macroWorld,i=cellAt(w,x,z);
    const h=macroAt(w,x,z),water=w.water[i],s={height:h,water:water===1?0:water===2?w.spill[i]:-Infinity,kind:water},flow=-1;
    const type=satelliteMap(w).codes[i];
    return {cx,cz,type,climate:c.index,temperature:c.temperature,elevation:s.height,elevationTier:n.elevationTier,largeLandmass:n.largeLandmass,
      plateBase:n.plateBase,hillRelief:n.hillRelief,highlandRelief:n.highlandRelief,mountainRelief:n.mountainRelief,waterfallDrop:0,wetland:0,
      flow,displayFlow:flow,discharge:w.flow[i],riverWidth:0,riverDepth:0,riverBlocks:0,lakeLevel:s.kind===2?s.water:null,downstream:null,courseVal:0};
  }
  override getChunkInfo(cx:number,cz:number):ChunkInfo {
    const info=this.getOverviewInfo(cx,cz),features=this.features.get(`${cx},${cz}`),s=features?.sample??this.raw((cx+.5)*S,(cz+.5)*S);
    if(!s.river){const path=this.getRiverPath(cx,cz);if(path.length){const p=path[0],q=path[path.length-1],at=this.raw((p.x+q.x)/2,(p.z+q.z)/2);if(at.river)Object.assign(s,at);}}
    info.elevation=s.height;
    info.largeLandmass=s.height>0;info.elevationTier=s.height>1024?3:s.height>384?2:s.height>96?1:0;
    const w=this.macroWorld,[x,z]=this.coordinates((cx+.5)*S,(cz+.5)*S),network=getTributaries(w,cellAt(w,x,z)),local=network.frame.local(x,z);
    const label=riverChunkLabel(network,Math.floor((local.x+network.frame.size/2)/S),Math.floor((local.z+network.frame.size/2)/S));
    info.type=label.id;info.waterfallDrop=label.waterfallDropM??0;Object.assign(info,{hasDecorativeStream:!!label.hasDecorativeStream});
    info.flow=info.displayFlow=-1;info.riverWidth=info.riverDepth=info.riverBlocks=0;info.downstream=null;
    info.wetland=features?.wetland??s.wetland;info.lakeLevel=s.kind===2?s.water:null;
    if(s.river){
      const r=s.river,dx=r.directionEast,dz=-r.directionNorth;
      info.discharge=r.dischargeM3s;info.riverWidth=r.widthM/2;info.riverBlocks=r.source==='source'?r.widthM/S:Math.max(1,Math.ceil(r.widthM/S));
      info.riverDepth=Math.max(0,s.water-s.height);info.flow=info.displayFlow=this.direction(dx,dz);
      const d=FLOW_DIRECTIONS[info.flow];info.downstream={cx:cx+d.dx,cz:cz+d.dz};
    }
    return info;
  }
  private direction(dx:number,dz:number):number {
    const dirs=FLOW_DIRECTIONS;
    let best=-Infinity,result=-1;dirs.forEach(({dx:x,dz:z},i)=>{const dot=(dx*x+dz*z)/Math.hypot(x,z);if(dot>best){best=dot;result=i;}});return result;
  }
  override getBaseChunkInfo(cx:number,cz:number):ChunkInfo {return this.getOverviewInfo(cx,cz);}
  override cachedChunkInfo():ChunkInfo|null {return null;}
  override getChunkType(cx:number,cz:number):number {return this.getChunkInfo(cx,cz).type;}
  override getAtlasNodeInfo(cx:number,cz:number):ChunkInfo {return this.getChunkInfo(cx,cz);}
  getTerrainWeights(wx:number,wz:number) {return [{type:this.getChunkType(Math.floor(wx/S),Math.floor(wz/S)),weight:1}];}
  getTerrainBlend(wx:number,wz:number) {
    const c=this.columnState(wx,wz),color:[number,number,number]=[(c.color>>>16)&255,(c.color>>>8)&255,c.color&255];
    return {color,materials:new Map([[c.block,1]]),roughness:1,wetness:c.state.hasWater?1:0};
  }
  getAlluvialFans():readonly [] {return [];}
  override adoptMapSummary():void {}
  override hasRiverPaths(cx:number,cz:number):boolean {return this.getRiverPath(cx,cz).length>0;}
  override getRiverPaths(cx:number,cz:number):readonly (readonly RiverPoint[])[] {
    const w=this.macroWorld,[x,z]=this.coordinates((cx+.5)*S,(cz+.5)*S),cell=cellAt(w,x,z),paths:RiverPoint[][]=[];
    const append=(a:RiverPoint,b:RiverPoint)=>{const path=clipRiverSegment(a,b,x-S/2,z-S/2,x+S/2,z+S/2);if(path)paths.push(path);};
    const network=getTributaries(w,cell);
    for(const edge of [...network.mainEdges,...network.edges])for(let k=1;k<edge.points.length;k++){
      const point=(p:typeof edge.points[number]):RiverPoint=>{const g=network.frame.coordinates(p.x,p.z),C=w.circumference,gx=x+((g.wx-x+C/2)%C+C)%C-C/2;
        return {x:gx,z:g.wz,level:p.level,width:edge.halfWidth,depth:0,discharge:edge.dischargeM3s,mouth:0};};
      append(point(edge.points[k-1]),point(edge.points[k]));
    }
    return paths.sort((a,b)=>b[0].discharge-a[0].discharge);
  }
  override getRiverPath(cx:number,cz:number):readonly RiverPoint[] {
    return this.getRiverPaths(cx,cz)[0]??[];
  }
  private columnState(wx:number,wz:number,raw?:MacroSurface):{height:number;block:number;state:WaterSample;color:number} {
    const x=Math.floor(wx),z=Math.floor(wz),cx=Math.floor(x/S),cz=Math.floor(z/S),s=raw??this.raw(x+.5,z+.5);
    const h=Math.fround(s.height);
    const appearance=this.getChunkWaterAppearance(cx,cz),r=s.river,speed=r?flowSpeed(.01,r.dischargeM3s,r.widthM):0;
    let baseDisplay=s.water;
    if(appearance.form===2)baseDisplay=-Infinity;else if(appearance.form===3&&Number.isFinite(baseDisplay))baseDisplay+=2;
    const water={level:baseDisplay,kind:s.kind===4?WaterKind.STATIC:s.kind,
      vx:(r?.directionEast??0)*speed,vz:-(r?.directionNorth??0)*speed,discharge:r?.dischargeM3s??0};
    let level=water.level,kind=water.kind;
    const bed=h,waterSurfaceLevel=level>h?level:-Infinity;
    let iceCoverage=0,iceLevel=-Infinity,iceBottomLevel=-Infinity;
    if(Number.isFinite(waterSurfaceLevel)&&appearance.freezing){
      iceCoverage=appearance.freezing===2?1:appearance.iceAmount;
      iceLevel=Math.fround(level);iceBottomLevel=Math.fround(Math.max(bed,level-appearance.iceThickness));
      if(iceCoverage>=.5)level=iceBottomLevel;
    }
    if(!Number.isFinite(waterSurfaceLevel)){level=-Infinity;kind=0;}
    if(level<=bed)level=-Infinity;
    const frozen=iceCoverage>=.5,block=(s as any).surfaceBlock??Block.GRASS;
    const state:WaterSample={level,kind,vx:water.vx,vz:water.vz,discharge:r?.dischargeM3s??0,form:appearance.form,freezing:appearance.freezing,temperature:appearance.temperature,
      hasWater:Number.isFinite(waterSurfaceLevel),frozen,iceCoverage,iceLevel,iceBottomLevel,iceThickness:frozen?iceLevel-iceBottomLevel:0,
      bedLevel:bed,liquidDepth:Number.isFinite(level)?level-bed:0,hasLiquidWater:Number.isFinite(level),waterSurfaceLevel};
    return {height:h,block,state,color:(s as any).surfaceColor??0};
  }
  getSurfaceColumn(wx:number,wz:number,cover=true) {
    const x=Math.floor(wx),z=Math.floor(wz),raw=this.raw(x+.5,z+.5,cover),value=this.columnState(x,z,raw);
    const bodyKind=raw.kind===1?'sea':raw.kind===2?'lake':raw.river?'river':'unknown';
    const lake=this.macroWorld.lakeId[raw.cell];
    return {...value,temperature:raw.temperature,chunkCode:raw.chunkCode??0,
      waterBodyKind:bodyKind,waterBodyId:bodyKind==='sea'?'sea':bodyKind==='lake'&&lake>=0?`lake:${lake}`:raw.river?.id??null};
  }
  override getHeight(wx:number,wz:number):number {const c=this.columnState(wx,wz);return Math.max(c.height,c.state.frozen?c.state.iceLevel:-Infinity);}
  override getWaterLevel(wx:number,wz:number):number {return this.columnState(wx,wz).state.level;}
  override getWaterState(wx:number,wz:number):WaterSample {return this.columnState(wx,wz).state;}
  override findSpawnChunk():{cx:number;cz:number} {
    const w=this.macroWorld;
    if(this.spawnPoint)return {...this.spawnPoint};
    if(this.generation.landRatio===0)return {cx:0,cz:0};
    const candidates:{cx:number;cz:number;tier:number;key:number;flag:boolean}[]=[];
    const add=(i:number,cx:number,cz:number,flag=false)=>{
      const temperature=w.temperature[i],height=w.height[i];
      if(!Number.isFinite(temperature)||!Number.isFinite(height)||height<12||w.water[i]||w.riverWidth[i])return;
      const row=Math.floor(i/w.columns),latitude=Math.PI/2-(row+.5)*Math.PI/w.rows;
      const tier=temperature>=8&&temperature<=28?0:temperature>=0&&temperature<=35?1:2;
      const moisture=Math.max(0,Math.min(1,w.moisture[i]));
      const comfort=1/(1+Math.abs(temperature-18)/12);
      const lowland=1/(1+Math.max(0,height-200)/1200);
      const weight=Math.max(.001,Math.cos(latitude))*comfort*lowland*(.5+moisture*.5);
      const random=Math.max(Number.EPSILON,hash2(cx,cz,this.seed^0x53504157));
      candidates.push({cx,cz,tier,key:-Math.log(random)/weight,flag});
    };
    for(const site of w.flags)add(site.cell,site.cx,site.cz,true);
    for(let i=0;i<w.height.length;i++){
      const cx=Math.floor(((i%w.columns+.5)/w.columns-.5)*w.circumference/S);
      const cz=Math.floor(((Math.floor(i/w.columns)+.5)/w.rows*.5-.25)*w.circumference/S);
      add(i,cx,cz);
    }
    candidates.sort((a,b)=>a.tier-b.tier||Number(b.flag)-Number(a.flag)||a.key-b.key||a.cz-b.cz||a.cx-b.cx);
    const checked=[0,0,0];
    for(const candidate of candidates){
      if(checked[candidate.tier]++>=512)continue;
      const {cx,cz}=candidate,x=cx*S+32.5,z=cz*S+32.5,center=this.raw(x,z);
      if(center.kind||center.river||center.wetland>.25||!(center.height>center.water))continue;
      let safe=true;
      for(const [dx,dz] of [[-2,0],[2,0],[0,-2],[0,2]]){
        const s=this.raw(x+dx,z+dz);
        if(s.kind||s.river||s.wetland>.25||!(s.height>s.water)||Math.abs(s.height-center.height)>=1){safe=false;break;}
      }
      if(safe){this.spawnPoint={cx,cz};return {...this.spawnPoint};}
    }
    throw Error('未找到安全出生地，请调整陆占比、气温或种子。');
  }
  override findSpawnOrigin():{cx:number;cz:number} {return this.findSpawnChunk();}
  override getInitialTerritorySites():readonly {cx:number;cz:number}[] {return this.macroWorld.flags;}
  override spawnSurface(cx:number,cz:number) {const x=cx*S+32,z=cz*S+32;return {height:this.getHeight(x,z),water:this.getWaterLevel(x,z)};}
  override generateChunk(cx:number,cz:number):ChunkGenResult {
    const P=PADDED_SIZE,N=P*P,tile=this.rawTile(cx,cz),heights=new Float32Array(N),waterLevels=new Float32Array(N),surfaces=new Uint32Array(S*S),surfaceColors=new Uint8Array(N*3);
    const waterField:WaterField={appearance:this.getChunkWaterAppearance(cx,cz),levels:waterLevels,bedLevels:new Float32Array(N),iceLevels:new Float32Array(N),iceBottomLevels:new Float32Array(N),iceCoverages:new Float32Array(N),kinds:new Uint8Array(N),velocities:new Float32Array(N*2),discharge:new Float32Array(N)};
    for(let z=-1;z<=S;z++)for(let x=-1;x<=S;x++){
      const inner=x>=0&&x<S&&z>=0&&z<S,raw=inner?this.packedPoint(tile,z*S+x):undefined;
      const c=this.columnState(cx*S+x,cz*S+z,raw),s=c.state,i=(z+1)*P+x+1;
      heights[i]=c.height;waterLevels[i]=s.level;waterField.bedLevels![i]=s.bedLevel;waterField.iceLevels![i]=s.iceLevel;waterField.iceBottomLevels![i]=s.iceBottomLevel;waterField.iceCoverages![i]=s.iceCoverage;
      waterField.kinds[i]=s.kind;waterField.velocities[i*2]=s.vx;waterField.velocities[i*2+1]=s.vz;waterField.discharge[i]=s.discharge;
      for(let ch=0;ch<3;ch++)surfaceColors[i*3+ch]=(c.color>>>((2-ch)*8))&255;
      if(inner)surfaces[z*S+x]=c.block;
    }
    return {cx,cz,type:this.getChunkType(cx,cz),heights,surfaces,surfaceColors,waterLevels,waterField};
  }
  override generatePreviewChunk(cx:number,cz:number,size=PREVIEW_SIZE):PreviewChunk {
    const heights=new Float32Array(size*size),waterLevels=new Float32Array(size*size),surfaceColors=new Uint8Array(size*size*3),step=S/size;
    for(let z=0;z<size;z++)for(let x=0;x<size;x++){const i=z*size+x,c=this.columnState(cx*S+(x+.5)*step,cz*S+(z+.5)*step);
      heights[i]=c.state.frozen?c.state.iceLevel:c.height;waterLevels[i]=c.state.level;
      const ice=c.state.frozen?this.getChunkWaterState(Math.floor((cx*S+(x+.5)*step)/S),Math.floor((cz*S+(z+.5)*step)/S)).colors.ice:null;
      for(let ch=0;ch<3;ch++)surfaceColors[i*3+ch]=ice?ice[ch]:(c.color>>>((2-ch)*8))&255;}
    return {cx,cz,size,step,heights,waterLevels,surfaceColors};
  }
}
