import * as THREE from 'three';
import type { DecorationInput, DecorationPlacement } from './WorldDecoration';
import { buildPlantById } from './vegetation/Plants';
import { buildRockById, getRockDef, resolveRockPalette } from './rocks/Rocks';
import type { MeshData } from './ChunkMesher';
import { appendDecorationCollider } from './DecorationCollisionShapes';
import { CHUNK_SIZE } from '../../core/config';
import { naturalPhysicsLevel, plantWindAttribute, plantWindMaterial } from './PlantWind';

type DecorationTransforms = Float32Array | Float64Array;
export interface DecorationBatch {key:string;kind:'plant'|'rock';geometry:MeshData;transforms:DecorationTransforms}

const MODEL_CACHE_LIMIT = 192;
const MODEL_CACHE_BYTES = 16 * 1024 * 1024;
const modelCache = new Map<string, MeshData>();
let modelBytes = 0;
const modelStats = { builds: 0, hits: 0, evictions: 0 };
const modelSize = (model:MeshData) => model.positions.byteLength+model.colors.byteLength+model.indices.byteLength+(model.normals?.byteLength??0)+(model.plantParts?.byteLength??0);
/** 模型构建缓存统计 */
export function decorationModelStats():{builds:number;hits:number;evictions:number;cached:number;bytes:number}{
  return {...modelStats, cached:modelCache.size, bytes:modelBytes};
}
function cachedModel(key:string, build:()=>MeshData):MeshData{
  const hit=modelCache.get(key);
  if(hit){modelStats.hits++;modelCache.delete(key);modelCache.set(key,hit);return hit;}
  const model=build();
  modelStats.builds++;
  while(modelCache.size&&(modelCache.size>=MODEL_CACHE_LIMIT||modelBytes+modelSize(model)>MODEL_CACHE_BYTES)){
    const oldest=modelCache.keys().next().value!;
    modelBytes-=modelSize(modelCache.get(oldest)!);modelCache.delete(oldest);modelStats.evictions++;
  }
  modelCache.set(key,model);modelBytes+=modelSize(model);
  return model;
}
/** 岩石几何按色板去重避免重复构建 */
function rockVariantKey(p:DecorationPlacement,type:number):string{
  const def=getRockDef(p.id);
  const palette=def?resolveRockPalette(def,p.season,type):undefined;
  return palette?Object.values(palette).join('/'):'';
}
export function decorationInstances(input:DecorationInput,placements:DecorationPlacement[],colliders?:number[]):DecorationBatch[]{
  const groups=new Map<string,DecorationPlacement[]>();
  for(const p of placements){
    const key=p.kind==='rock'
      ?`rock:${p.id}:${p.season}:${p.form}:${rockVariantKey(p,input.type)}`
      :`plant:${p.id}:${p.season}:`;
    const group=groups.get(key)??[];group.push(p);groups.set(key,group);
  }
  return [...groups].map(([key,group])=>{
    const p=group[0];
    const base=cachedModel(key,()=>{
      const built=p.kind==='plant'?buildPlantById(p.id,p.season):buildRockById(p.id,p.season,{form:p.form,chunkType:input.type});
      const model:MeshData={positions:built.positions,colors:built.colors,indices:built.indices,normals:built.normals};
      if('windParts' in built)model.plantParts=built.windParts;
      return model;
    });
    const transforms=new Float64Array(group.length*5);
    group.forEach((p,i)=>{transforms.set([p.x,p.y,p.z,p.yaw,p.scale],i*5);if(colliders)appendDecorationCollider(colliders,p,base);});
    // 传输必须独占缓冲，按批次复制；几何本体留在缓存复用
    return {key,kind:p.kind,geometry:{positions:base.positions.slice(),colors:base.colors.slice(),indices:base.indices.slice(),...(base.normals?{normals:base.normals.slice()}:{}),...(base.plantParts?{plantParts:base.plantParts.slice()}:{})},transforms};
  });
}

interface SlotModel {mesh:THREE.InstancedMesh;capacity:number;items:DecorationTransforms[];density:number;slotX:number;slotZ:number;uid:number;revision:number}
interface SlotEntry {key:string;geometry:MeshData;items:DecorationTransforms[];count:number;kind:string;slotX:number;slotZ:number;distance:number}
interface DrawModel {mesh:THREE.InstancedMesh;capacity:number;levels?:THREE.InstancedBufferAttribute;chunks?:{cx:number;cz:number;start:number;count:number}[];signature:number;levelsDirty:boolean}
const rotateScale=(yaw:number,scale:number)=>{const c=Math.cos(yaw)*scale,s=Math.sin(yaw)*scale;return [c,-s,s,c];};
/** 容量按1.5倍留量，避免2次幂空转 */
const capacityFor=(count:number)=>Math.max(16,Math.ceil(count*1.5));
const needsResize=(capacity:number,count:number)=>capacity<count||capacity>Math.max(16,count*3);

export class DecorationRenderer {
  readonly group = new THREE.Group();
  private readonly chunks = new Map<string,{cx:number;cz:number;batches:DecorationBatch[]}>();
  private readonly models = new Map<string,SlotModel>();
  private readonly prototypes = new Map<string,MeshData>();
  private readonly geometries = new Map<string,THREE.BufferGeometry>();
  private readonly draws = new Map<string,DrawModel>();
  private readonly cullSphere = new THREE.Sphere();
  private readonly frustum = new THREE.Frustum();
  private readonly projection = new THREE.Matrix4();
  private readonly origin = new THREE.Vector3();
  private readonly windTime={value:0};
  private readonly windEnabled={value:0};
  private readonly windPhase={value:0};
  private readonly plantMaterial:THREE.Material;
  private lastCamera?:THREE.PerspectiveCamera;
  private windRevision='';
  private levelRefresh=false;
  private viewHash=0;
  private viewSlots=0;
  private nextUid=1;
  private cullDirty=true;
  private dirty=true;
  private lastRange='';
  private drawOriginX=Number.NaN;
  private drawOriginZ=Number.NaN;
  constructor(private readonly material:THREE.Material) {this.plantMaterial=plantWindMaterial(material,this.windTime,this.windEnabled,this.windPhase);}
  set(cx:number,cz:number,batches:DecorationBatch[]):void {
    for(const batch of batches){const prototype=this.prototypes.get(batch.key);if(prototype)batch.geometry=prototype;else this.prototypes.set(batch.key,batch.geometry);}
    this.chunks.set(cx+','+cz,{cx,cz,batches});this.dirty=true;
  }
  remove(cx:number,cz:number):void {this.chunks.delete(cx+','+cz);this.dirty=true;}
  private geometryFor(key:string,source:MeshData,kind:string):THREE.BufferGeometry {
    let geometry=this.geometries.get(key);
    if(geometry)return geometry;
    geometry=new THREE.BufferGeometry();
    geometry.setAttribute('position',new THREE.BufferAttribute(source.positions,3));
    geometry.setAttribute('color',new THREE.BufferAttribute(source.colors,3,true));
    geometry.setIndex(new THREE.BufferAttribute(source.indices,1));
    if(source.normals)geometry.setAttribute('normal',new THREE.BufferAttribute(source.normals,3));
    if(kind==='plant'){
      geometry.setAttribute('plantWind',new THREE.BufferAttribute(plantWindAttribute(source.positions,source.plantParts,key.startsWith('plant:guan.')),4));
    }
    geometry.computeBoundingSphere();geometry.computeBoundingBox();
    // 风动幅度按几何高度外扩包围球，避免视锥边缘被误剔除
    if(kind==='plant')geometry.boundingSphere!.radius+=geometry.boundingBox!.max.y*.24;
    this.geometries.set(key,geometry);
    return geometry;
  }
  private dropKey(key:string):void {
    const draw=this.draws.get(key);
    if(draw){this.group.remove(draw.mesh);draw.mesh.dispose();this.draws.delete(key);}
    this.geometries.get(key)?.dispose();this.geometries.delete(key);
  }
  sync(cx:number,cz:number,radius:number,vegetation:number,density=1):void {
    const range=[cx,cz,radius,vegetation,density].join(',');
    if(!this.dirty&&range===this.lastRange)return;this.dirty=false;this.lastRange=range;
    this.origin.set(cx*CHUNK_SIZE,0,cz*CHUNK_SIZE);
    this.windPhase.value=(this.origin.x*.17+this.origin.z*.11)%(Math.PI*2);
    const visible = new Map<string,SlotEntry>();
    const retained = new Set<string>();
    for(const chunk of this.chunks.values())for(const batch of chunk.batches){
      retained.add(batch.key);
      const distance=(chunk.cx-cx)**2+(chunk.cz-cz)**2,limit=batch.kind==='plant'?vegetation:radius;
      if(distance>(limit+.5)**2)continue;
      const slotX=Math.floor(chunk.cx/2),slotZ=Math.floor(chunk.cz/2);
      const slot=batch.key+'@'+slotX+','+slotZ;
      const entry=visible.get(slot)??{key:batch.key,geometry:batch.geometry,items:[],count:0,kind:batch.kind,slotX,slotZ,distance:Infinity};
      entry.items.push(batch.transforms);entry.count+=batch.transforms.length/5;entry.distance=Math.min(entry.distance,distance);visible.set(slot,entry);
    }
    for(const [slot,model] of this.models)if(!visible.has(slot)){
      model.mesh.dispose();this.models.delete(slot);
    }
    for(const key of this.prototypes.keys())if(!retained.has(key)){
      this.prototypes.delete(key);this.dropKey(key);
    }
    for(const [slot,entry] of visible){
      const geometry=this.geometryFor(entry.key,entry.geometry,entry.kind);
      const fraction=entry.kind==='plant'&&entry.distance>2?density:1;
      let model=this.models.get(slot);
      if(model&&model.density===fraction&&model.items.length===entry.items.length&&model.items.every((v,i)=>v===entry.items[i]))continue;
      if(!model||needsResize(model.capacity,entry.count)){
        if(model)model.mesh.dispose();
        const capacity=capacityFor(entry.count),mesh=new THREE.InstancedMesh(geometry,this.material,capacity);
        mesh.instanceMatrix.setUsage(THREE.DynamicDrawUsage);mesh.name=slot;
        mesh.position.set(entry.slotX*CHUNK_SIZE*2,0,entry.slotZ*CHUNK_SIZE*2);
        model={mesh,capacity,items:[],density:fraction,slotX:entry.slotX,slotZ:entry.slotZ,uid:this.nextUid++,revision:0};this.models.set(slot,model);
      }
      let index=0;
      const target=model.mesh.instanceMatrix.array;
      const height=geometry.boundingBox!.max.y-geometry.boundingBox!.min.y;
      for(const values of entry.items)for(let i=0;i<values.length;i+=5){
        const hash=(Math.imul(Math.round(values[i]*16),73856093)^Math.imul(Math.round(values[i+2]*16),19349663))>>>0;
        if(fraction<1&&height*values[i+4]<2.5&&hash/4294967296>=fraction)continue;
        const [c,ns,s,nc]=rotateScale(values[i+3],values[i+4]);
        const at=index*16;
        target[at]=c;target[at+2]=ns;target[at+5]=values[i+4];target[at+8]=s;target[at+10]=nc;
        target[at+12]=values[i]-model.mesh.position.x;target[at+13]=values[i+1];target[at+14]=values[i+2]-model.mesh.position.z;
        target[at+15]=1;index++;
      }
      model.items=entry.items;model.density=fraction;model.revision=(model.revision+1)|0;
      model.mesh.count=index;model.mesh.instanceMatrix.needsUpdate=true;model.mesh.computeBoundingSphere();
    }
    this.cullDirty=true;this.updateVisibility(this.lastCamera);
  }
  updateVisibility(camera?:THREE.PerspectiveCamera):void {
    if(camera){
      this.lastCamera=camera;camera.updateMatrixWorld();
      this.projection.multiplyMatrices(camera.projectionMatrix,camera.matrixWorldInverse);
      this.frustum.setFromProjectionMatrix(this.projection);
    }
    const visible = new Map<string,{keys:string[];count:number;kind:string}>();
    let viewHash=0x811c9dc5, viewSlots=0;
    for(const [slot,model] of this.models){
      if(!model.mesh.count)continue;
      if(camera){
        this.cullSphere.copy(model.mesh.boundingSphere!).translate(model.mesh.position);
        if(!this.frustum.intersectsSphere(this.cullSphere))continue;
      }
      viewHash=Math.imul(viewHash^(Math.imul(model.uid,0x9e3779b1)^Math.imul(model.revision,0x27d4eb2d)),0x01000193);
      viewSlots++;
      const key=slot.slice(0,slot.lastIndexOf('@'));
      const entry=visible.get(key)??{keys:[],count:0,kind:key.startsWith('plant:')?'plant':'rock'};
      entry.keys.push(slot);entry.count+=model.mesh.count;visible.set(key,entry);
    }
    if(!this.cullDirty&&viewHash===this.viewHash&&viewSlots===this.viewSlots)return;
    this.cullDirty=false;this.viewHash=viewHash;this.viewSlots=viewSlots;
    const originMoved=this.drawOriginX!==this.origin.x||this.drawOriginZ!==this.origin.z;
    this.drawOriginX=this.origin.x;this.drawOriginZ=this.origin.z;
    // 原点移动使draw过期，可见时重建
    if(originMoved)for(const draw of this.draws.values())draw.signature=Number.NaN;
    for(const draw of this.draws.values())draw.mesh.count=0;
    for(const [key,entry] of visible){
      const sources=entry.keys.map(slot=>this.models.get(slot)!);
      let signature=0x811c9dc5;
      for(const source of sources){
        signature=Math.imul(signature^Math.imul(source.uid,0x9e3779b1)^Math.imul(source.revision,0x27d4eb2d),0x01000193);
        signature=(signature+Math.imul(source.mesh.count,0x85ebca6b))|0;
      }
      const draw=this.draws.get(key);
      if(!draw||draw.signature!==signature||originMoved||needsResize(draw.capacity,entry.count)){
        this.rebuildDraw(key,entry,sources,signature);
      }
      this.draws.get(key)!.mesh.count=entry.count;
    }
  }
  private rebuildDraw(key:string,entry:{count:number;kind:string},sources:SlotModel[],signature:number):void {
    let draw=this.draws.get(key);
    if(!draw||needsResize(draw.capacity,entry.count)){
      if(draw){this.group.remove(draw.mesh);draw.mesh.dispose();}
      const geometry=this.geometries.get(key)!;
      const capacity=capacityFor(entry.count);
      const plant=entry.kind==='plant';
      const mesh=new THREE.InstancedMesh(geometry,plant?this.plantMaterial:this.material,capacity);
      mesh.frustumCulled=false;mesh.instanceMatrix.setUsage(THREE.DynamicDrawUsage);mesh.name=key;
      let levels:THREE.InstancedBufferAttribute|undefined;
      if(plant){
        levels=new THREE.InstancedBufferAttribute(new Uint8Array(capacity),1).setUsage(THREE.DynamicDrawUsage);
        geometry.setAttribute('plantWindLevel',levels);
      }
      draw={mesh,capacity,levels,signature,levelsDirty:plant};
      this.draws.set(key,draw);this.group.add(mesh);
    }else{
      draw.signature=signature;draw.levelsDirty=!!draw.levels;
    }
    draw.mesh.position.copy(this.origin);
    const target=draw.mesh.instanceMatrix.array as Float32Array;
    let at=0;
    for(const source of sources){
      target.set(source.mesh.instanceMatrix.array.subarray(0,source.mesh.count*16),at);
      const dx=source.mesh.position.x-this.origin.x,dz=source.mesh.position.z-this.origin.z;
      for(let i=0;i<source.mesh.count;i++){
        target[at+i*16+12]+=dx;
        target[at+i*16+14]+=dz;
      }
      at+=source.mesh.count*16;
    }
    draw.mesh.count=entry.count;draw.mesh.instanceMatrix.needsUpdate=true;
    if(draw.levels){
      const chunks:{cx:number;cz:number;start:number;count:number}[]=[];
      for(let i=0;i<entry.count;i++){
        const cx=Math.floor((target[i*16+12]+this.origin.x)/CHUNK_SIZE),cz=Math.floor((target[i*16+14]+this.origin.z)/CHUNK_SIZE),last=chunks[chunks.length-1];
        if(last&&last.cx===cx&&last.cz===cz)last.count++;else chunks.push({cx,cz,start:i,count:1});
      }
      draw.chunks=chunks;this.levelRefresh=true;
    }
  }
  updateNaturalPhysics(dt:number,enabled:boolean,weatherAt:(cx:number,cz:number)=>number,revision:string):void {
    this.windEnabled.value=enabled?1:0;
    if('color' in this.material && 'color' in this.plantMaterial)(this.plantMaterial.color as THREE.Color).copy(this.material.color as THREE.Color);
    if(!enabled)return;
    this.windTime.value+=Math.max(0,dt);
    const full=this.windRevision!==revision;
    if(!full&&!this.levelRefresh)return;
    const weather=new Map<string,number>();
    for(const draw of this.draws.values()){
      if(!draw.levels||!draw.mesh.count)continue;
      if(!full&&!draw.levelsDirty)continue;
      const data=draw.levels.array as Uint8Array;
      let changed=false;
      for(const chunk of draw.chunks??[]){
        const key=chunk.cx+','+chunk.cz;
        let level=weather.get(key);
        if(level===undefined){level=naturalPhysicsLevel(weatherAt(chunk.cx,chunk.cz));weather.set(key,level);}
        for(let i=chunk.start;i<chunk.start+chunk.count;i++)if(data[i]!==level){data[i]=level;changed=true;}
      }
      if(changed)draw.levels.needsUpdate=true;
      draw.levelsDirty=false;
    }
    this.windRevision=revision;this.levelRefresh=false;
  }
  dispose():void {
    for(const draw of this.draws.values()){this.group.remove(draw.mesh);draw.mesh.dispose();}
    this.draws.clear();
    this.plantMaterial.dispose();
    for(const geometry of this.geometries.values())geometry.dispose();
    this.geometries.clear();
    for(const model of this.models.values())model.mesh.dispose();
    this.models.clear();this.chunks.clear();this.prototypes.clear();this.group.clear();this.group.removeFromParent();
  }
}
