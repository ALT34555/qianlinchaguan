import * as THREE from 'three';
import type { DecorationInput, DecorationPlacement } from './WorldDecoration';
import { buildPlantById } from './vegetation/Plants';
import { buildRockById } from './rocks/Rocks';
import type { MeshData } from './ChunkMesher';

export interface DecorationBatch {key:string;kind:'plant'|'rock';geometry:MeshData;transforms:Float32Array}
const modelCache = new Map<string, MeshData>();
let modelBytes = 0;
const modelSize = (model:MeshData) => model.positions.byteLength+model.colors.byteLength+model.indices.byteLength+(model.normals?.byteLength??0);
export function decorationInstances(input:DecorationInput,placements:DecorationPlacement[]):DecorationBatch[]{
  const groups=new Map<string,DecorationPlacement[]>();
  for(const p of placements){
    const key=`${p.kind}:${p.id}:${p.season}:${p.kind==='rock'?`${p.form}:${input.type}`:''}`;
    const group=groups.get(key)??[];group.push(p);groups.set(key,group);
  }
  return [...groups].map(([key,group])=>{
    const p=group[0];
    let base=modelCache.get(key);
    if(!base){
      const built=p.kind==='plant'?buildPlantById(p.id,p.season):buildRockById(p.id,p.season,{form:p.form,chunkType:input.type});
      const geometry=new THREE.BufferGeometry();
      geometry.setAttribute('position',new THREE.BufferAttribute(built.positions,3));
      geometry.setIndex(new THREE.BufferAttribute(built.indices,1));
      geometry.computeVertexNormals();
      base={positions:built.positions,colors:built.colors,indices:built.indices,normals:geometry.getAttribute('normal').array as Float32Array};
      geometry.dispose();
      while(modelCache.size&&(modelCache.size>=192||modelBytes+modelSize(base)>16*1024*1024)){
        const oldest=modelCache.keys().next().value!;
        modelBytes-=modelSize(modelCache.get(oldest)!);modelCache.delete(oldest);
      }
      modelCache.set(key,base);
      modelBytes+=modelSize(base);
    }
    const transforms=new Float32Array(group.length*5);
    group.forEach((p,i)=>transforms.set([p.x,p.y,p.z,p.yaw,p.scale],i*5));
    return {key,kind:p.kind,geometry:{positions:base.positions.slice(),colors:base.colors.slice(),indices:base.indices.slice(),normals:base.normals!.slice()},transforms};
  });
}

export class DecorationRenderer {
  readonly group = new THREE.Group();
  private readonly chunks = new Map<string,{cx:number;cz:number;batches:DecorationBatch[]}>();
  private readonly models = new Map<string,{mesh:THREE.InstancedMesh;capacity:number;items:Float32Array[];density:number;distance:number}>();
  private readonly prototypes = new Map<string,MeshData>();
  private readonly geometries = new Map<string,THREE.BufferGeometry>();
  private readonly dummy = new THREE.Object3D();
  private readonly draws = new Map<string,{mesh:THREE.InstancedMesh;capacity:number}>();
  private readonly frustum = new THREE.Frustum();
  private readonly projection = new THREE.Matrix4();
  private viewKey = '';
  private cullDirty = true;
  private dirty = true;
  private lastRange = '';
  constructor(private readonly material:THREE.Material) {}
  set(cx:number,cz:number,batches:DecorationBatch[]):void {
    for(const batch of batches){const prototype=this.prototypes.get(batch.key);if(prototype)batch.geometry=prototype;else this.prototypes.set(batch.key,batch.geometry);}
    this.chunks.set(cx+','+cz,{cx,cz,batches});this.dirty=true;
  }
  remove(cx:number,cz:number):void {this.chunks.delete(cx+','+cz);this.dirty=true;}
  sync(cx:number,cz:number,radius:number,vegetation:number,density=1):void {
    const range=[cx,cz,radius,vegetation,density].join(',');
    if(!this.dirty&&range===this.lastRange)return;this.dirty=false;this.lastRange=range;
    const visible = new Map<string,{key:string;geometry:MeshData;items:Float32Array[];count:number;distance:number;kind:string}>();
    const retained = new Set<string>();
    for(const chunk of this.chunks.values())for(const batch of chunk.batches){
      retained.add(batch.key);
      const distance=(chunk.cx-cx)**2+(chunk.cz-cz)**2,limit=batch.kind==='plant'?vegetation:radius;
      if(distance>(limit+.5)**2)continue;
      const slot=batch.key+'@'+Math.floor(chunk.cx/2)+','+Math.floor(chunk.cz/2);
      const entry=visible.get(slot)??{key:batch.key,geometry:batch.geometry,items:[],count:0,distance:Infinity,kind:batch.kind};
      entry.items.push(batch.transforms);entry.count+=batch.transforms.length/5;entry.distance=Math.min(entry.distance,distance);visible.set(slot,entry);
    }
    for(const [slot,model] of this.models)if(!visible.has(slot)){
      model.mesh.dispose();this.models.delete(slot);
    }
    for(const key of this.prototypes.keys())if(!retained.has(key)){
      this.prototypes.delete(key);this.geometries.get(key)?.dispose();this.geometries.delete(key);
    }
    for(const [slot,entry] of visible){
      let geometry=this.geometries.get(entry.key);
      if(!geometry){
        geometry=new THREE.BufferGeometry();
        geometry.setAttribute('position',new THREE.BufferAttribute(entry.geometry.positions,3));
        geometry.setAttribute('color',new THREE.BufferAttribute(entry.geometry.colors,3,true));
        geometry.setIndex(new THREE.BufferAttribute(entry.geometry.indices,1));
        if(entry.geometry.normals)geometry.setAttribute('normal',new THREE.BufferAttribute(entry.geometry.normals,3));else geometry.computeVertexNormals();
        geometry.computeBoundingSphere();geometry.computeBoundingBox();this.geometries.set(entry.key,geometry);
      }
      const fraction=entry.kind==='plant'&&entry.distance>2?density:1;
      let model=this.models.get(slot);
      if(model&&model.density===fraction&&model.items.length===entry.items.length&&model.items.every((v,i)=>v===entry.items[i]))continue;
      if(!model||model.capacity<entry.count||model.capacity>Math.max(16,entry.count*4)){
        if(model)model.mesh.dispose();
        const capacity=2**Math.ceil(Math.log2(Math.max(1,entry.count))),mesh=new THREE.InstancedMesh(geometry,this.material,capacity);
        mesh.instanceMatrix.setUsage(THREE.DynamicDrawUsage);mesh.name=slot;
        model={mesh,capacity,items:[],density:fraction,distance:entry.distance};this.models.set(slot,model);
      }
      let index=0;
      const height=geometry.boundingBox!.max.y-geometry.boundingBox!.min.y;
      for(const values of entry.items)for(let i=0;i<values.length;i+=5){
        const hash=(Math.imul(Math.round(values[i]*16),73856093)^Math.imul(Math.round(values[i+2]*16),19349663))>>>0;
        if(fraction<1&&height*values[i+4]<2.5&&hash/4294967296>=fraction)continue;
        this.dummy.position.set(values[i],values[i+1],values[i+2]);this.dummy.rotation.set(0,values[i+3],0);this.dummy.scale.setScalar(values[i+4]);
        this.dummy.updateMatrix();model.mesh.setMatrixAt(index++,this.dummy.matrix);
      }
      model.items=entry.items;model.density=fraction;model.distance=entry.distance;
      model.mesh.count=index;model.mesh.instanceMatrix.needsUpdate=true;model.mesh.computeBoundingSphere();
    }
    this.cullDirty=true;this.updateVisibility();
  }
  updateVisibility(camera?:THREE.PerspectiveCamera):void {
    if(camera){camera.updateMatrixWorld();this.projection.multiplyMatrices(camera.projectionMatrix,camera.matrixWorldInverse);this.frustum.setFromProjectionMatrix(this.projection);}
    const visible = new Map<string,{sources:THREE.InstancedMesh[];count:number}>();
    let viewKey='';
    for(const [slot,model] of this.models){
      if(!model.mesh.count||camera&&!this.frustum.intersectsSphere(model.mesh.boundingSphere!))continue;
      viewKey+=slot+';';const key=slot.slice(0,slot.lastIndexOf('@'));
      const entry=visible.get(key)??{sources:[],count:0};entry.sources.push(model.mesh);entry.count+=model.mesh.count;visible.set(key,entry);
    }
    if(!this.cullDirty&&viewKey===this.viewKey)return;this.cullDirty=false;this.viewKey=viewKey;
    for(const [key,draw] of this.draws){draw.mesh.count=0;if(!this.geometries.has(key)){this.group.remove(draw.mesh);draw.mesh.dispose();this.draws.delete(key);}}
    for(const [key,entry] of visible){
      let draw=this.draws.get(key);
      if(!draw||draw.capacity<entry.count||draw.capacity>Math.max(16,entry.count*4)){
        if(draw){this.group.remove(draw.mesh);draw.mesh.dispose();}
        const capacity=2**Math.ceil(Math.log2(Math.max(1,entry.count))),mesh=new THREE.InstancedMesh(this.geometries.get(key)!,this.material,capacity);
        mesh.frustumCulled=false;mesh.instanceMatrix.setUsage(THREE.DynamicDrawUsage);mesh.name=key;
        draw={mesh,capacity};this.draws.set(key,draw);this.group.add(mesh);
      }
      let at=0;
      for(const source of entry.sources){draw.mesh.instanceMatrix.array.set(source.instanceMatrix.array.subarray(0,source.count*16),at);at+=source.count*16;}
      draw.mesh.count=entry.count;draw.mesh.instanceMatrix.needsUpdate=true;
    }
  }
  dispose():void {
    for(const model of this.models.values())model.mesh.dispose();
    for(const draw of this.draws.values())draw.mesh.dispose();this.draws.clear();
    for(const geometry of this.geometries.values())geometry.dispose();
    this.models.clear();this.geometries.clear();this.chunks.clear();this.prototypes.clear();this.group.clear();this.group.removeFromParent();
  }
}
