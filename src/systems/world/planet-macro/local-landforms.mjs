import {macroCellHasWater} from './source-rivers.mjs';
const caches=new WeakMap(),SPACING=240;
const SHAPES={blank:[0,0,.8,.8],hill:[8,25,.9,.8],hummock:[25,60,.8,.65],ridge:[50,120,.95,.3],hollow:[-15,-5,.8,.8],rift:[-50,-15,.95,.42]};
const clamp=v=>Math.max(0,Math.min(1,v)),smooth=v=>v*v*(3-2*v);
function random(x,y,z,seed){let h=Math.imul(x,374761393)^Math.imul(y,668265263)^Math.imul(z,1442695041)^seed;h=Math.imul(h^(h>>>13),1274126177);return ((h^(h>>>16))>>>0)/4294967296;}
function cache(world){let c=caches.get(world);if(!c){c={features:new Map(),masks:new Map(),last:null};caches.set(world,c);}return c;}
function regionNoise(world,p){
  const span=Math.max(SPACING*6,(world.circumference??world.radius*Math.PI*2)/(world.columns??512)*3);
  const q=p.map(v=>v*world.radius/span),i=q.map(Math.floor),t=q.map((v,k)=>smooth(v-i[k]));let value=0;
  for(let z=0;z<2;z++)for(let y=0;y<2;y++)for(let x=0;x<2;x++)value+=random(i[0]+x,i[1]+y,i[2]+z,world.options.seed^0x6c616e64)*(x?t[0]:1-t[0])*(y?t[1]:1-t[1])*(z?t[2]:1-t[2]);
  return value;
}
// 活动场只影响模板概率
export function localLandformProfile(world,p){
  const W=world.columns,H=world.rows;
  const x=(Math.atan2(p[2],p[0])/(Math.PI*2)+.5)*(W??1)-.5,y=(.25-Math.asin(Math.max(-1,Math.min(1,p[1])))/(Math.PI*2))*(W??1)-.5;
  const ix=Math.floor(x),iy=Math.floor(y),tx=smooth(x-ix),ty=smooth(y-iy);
  const node=(a,b)=>{if(b<0){b=-b-1;a+=W/2;}if(b>=H){b=H*2-b-1;a+=W/2;}return b*W+(a%W+W)%W;};
  const field=name=>{
    const values=world[name];if(!values||!W||!H)return 0;
    return clamp((values[node(ix,iy)]*(1-tx)+values[node(ix+1,iy)]*tx)*(1-ty)+(values[node(ix,iy+1)]*(1-tx)+values[node(ix+1,iy+1)]*tx)*ty);
  };
  const uplift=field('uplift'),rift=field('rift'),shear=field('shear'),region=regionNoise(world,p);
  const active=clamp(Math.max(uplift,rift*.85,shear*.7)),rugged=clamp(active*.8+(region-.5)*.5);
  const weights={blank:.52-.36*rugged,hill:.31-.11*rugged,hummock:.08+.22*rugged,ridge:.025+.38*uplift+.12*shear,hollow:.065,rift:.28*rift};
  const total=Object.values(weights).reduce((a,b)=>a+b,0);for(const kind in weights)weights[kind]/=total;
  return {uplift,rift,shear,region,weights};
}
// 球面共享模板，跨格不重置
export function localLandformHeight(world,point){
  const c=cache(world),x=point[0]*world.radius/SPACING,y=point[1]*world.radius/SPACING,z=point[2]*world.radius/SPACING;
  const ix=Math.floor(x),iy=Math.floor(y),iz=Math.floor(z);let nearby=c.last;
  if(!nearby||nearby.x!==ix||nearby.y!==iy||nearby.z!==iz){
    const features=[];
    for(let dz=-1;dz<=1;dz++)for(let dy=-1;dy<=1;dy++)for(let dx=-1;dx<=1;dx++){
      const a=ix+dx,b=iy+dy,d=iz+dz,key=a+','+b+','+d;let f=c.features.get(key);
      if(!f){
        const r=k=>random(a,b,d,world.options.seed^Math.imul(k,0x9e3779b1)),angle=r(1)*Math.PI*2;
        const x=a+.5+(r(2)-.5)*.4,y=b+.5+(r(3)-.5)*.4,z=d+.5+(r(4)-.5)*.4,n=Math.hypot(x,y,z)||1;
        const normal=[x/n,y/n,z/n],profile=localLandformProfile(world,normal);let pick=r(0),kind='blank';
        for(const [name,weight] of Object.entries(profile.weights)){pick-=weight;if(pick<=0){kind=name;break;}}
        const shape=SHAPES[kind];
        const eastLength=Math.hypot(normal[0],normal[2]),east=eastLength>1e-8?[-normal[2]/eastLength,0,normal[0]/eastLength]:[1,0,0];
        const north=[normal[1]*east[2]-normal[2]*east[1],normal[2]*east[0]-normal[0]*east[2],normal[0]*east[1]-normal[1]*east[0]];
        f={x,y,z,kind,normal,east,north,cos:Math.cos(angle),sin:Math.sin(angle),rx:shape[2],rz:shape[3],amplitude:shape[0]+(shape[1]-shape[0])*r(5)};
        c.features.set(key,f);while(c.features.size>512)c.features.delete(c.features.keys().next().value);
      }
      features.push(f);
    }
    nearby=c.last={x:ix,y:iy,z:iz,features};
  }
  let h=0;
  for(const f of nearby.features){
    if(!f.amplitude)continue;
    const dx=x-f.x,dy=y-f.y,dz=z-f.z,e=dx*f.east[0]+dy*f.east[1]+dz*f.east[2],n=dx*f.north[0]+dy*f.north[1]+dz*f.north[2];
    const u=(e*f.cos+n*f.sin)/f.rx,v=(dx*f.normal[0]+dy*f.normal[1]+dz*f.normal[2])/.95,w=(-e*f.sin+n*f.cos)/f.rz,r=u*u+v*v+w*w;
    if(r<1)h+=f.amplitude*(1-r)**3;
  }
  return h;
}
export function localLandformStats(world){const c=cache(world);return {features:c.features.size,maxFeatures:512,checksPerSample:27};}

// 含宏观水系的整格停用模板
export function localLandformWeight(world,context){
  const c=cache(world),W=world.columns,H=world.rows;
  const node=i=>{
    if(c.masks.has(i))return c.masks.get(i);
    const x=i%W,y=Math.floor(i/W);let lake=1;
    for(let dz=-1;dz<=1;dz++)for(let dx=-1;dx<=1;dx++){
      let row=y+dz,col=x+dx;if(row<0){row=-row-1;col+=W/2;}if(row>=H){row=H*2-row-1;col+=W/2;}
      if(world.water[row*W+(col%W+W)%W]===2)lake=0;
    }
    const value={dry:!macroCellHasWater(world,i),lake};
    c.masks.set(i,value);while(c.masks.size>4096)c.masks.delete(c.masks.keys().next().value);return value;
  };
  let distance=Infinity,lakeWeight=0;const tx=smooth(context.tx),ty=smooth(context.ty);
  for(const [i,x,y] of [[context.a,0,0],[context.b,1,0],[context.c,0,1],[context.d,1,1]]){
    const value=node(i);lakeWeight+=value.lake*(x?tx:1-tx)*(y?ty:1-ty);
    if(value.dry)continue;
    distance=Math.min(distance,Math.hypot(Math.max(0,Math.abs(context.tx-x)-.5),Math.max(0,Math.abs(context.ty-y)-.5)));
  }
  return smooth(clamp(distance/.25))*lakeWeight;
}
