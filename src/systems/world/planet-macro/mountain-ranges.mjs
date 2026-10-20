// 连通山脊与区域抬升
const TAU=Math.PI*2;
const clamp=(v,a,b)=>Math.max(a,Math.min(b,v));

// 一次 8 邻域平滑：把山脊揉进地形
export function smoothHeights(g,heights,weight=.28){
  const source=Float32Array.from(heights);
  for(let i=0;i<g.n;i++){
    let sum=0;
    for(let d=0;d<8;d++)sum+=source[g.neighbors[i*8+d]];
    heights[i]=source[i]*(1-weight)+sum*.125*weight;
  }
}

// 软顶：高处平滑压缩，避免削出平台
export function softCap(heights,start=3000,span=300){
  for(let i=0;i<heights.length;i++){
    const h=heights[i];
    if(h>start)heights[i]=start+span*Math.tanh((h-start)/span);
  }
}

const smooth=v=>{const t=clamp(v,0,1);return t*t*(3-2*t);};
const wrap=(x,w)=>(x%w+w)%w;
const delta=(x,w)=>wrap(x+w/2,w)-w/2;
const random=(i,seed)=>{let v=Math.imul(i^seed,0x45d9f3b);v=Math.imul(v^v>>>16,0x45d9f3b);return ((v^v>>>16)>>>0)/4294967296;};
const latitude=(g,y)=>Math.max(.22,Math.sin((y+.5)*Math.PI/g.h));
const cell=(g,x,y)=>clamp(Math.round(y),0,g.h-1)*g.w+wrap(Math.round(x),g.w);

// 周期空间索引
function spatial(g,size){
  const cols=Math.max(1,Math.floor(g.w/size)),width=g.w/cols,buckets=new Map();
  return {
    add(i,value){const key=Math.floor(i/g.w/size)*cols+Math.floor(i%g.w/width);if(!buckets.has(key))buckets.set(key,[]);buckets.get(key).push(value);},
    nearby(i,radius){
      const x=i%g.w,y=Math.floor(i/g.w),found=[];
      const rx=radius/Math.min(latitude(g,Math.max(0,y-radius)),latitude(g,Math.min(g.h-1,y+radius)));
      const minX=Math.floor((x-rx)/width),maxX=Math.floor((x+rx)/width);
      const minY=Math.max(0,Math.floor((y-radius)/size)),maxY=Math.min(Math.floor((g.h-1)/size),Math.floor((y+radius)/size));
      for(let gy=minY;gy<=maxY;gy++)for(let dx=0;dx<Math.min(cols,maxX-minX+1);dx++){
        const key=gy*cols+wrap(minX+dx,cols);
        const bucket=buckets.get(key);if(bucket)found.push(...bucket);
      }
      return found;
    }
  };
}

function distance(g,a,b){
  const ay=Math.floor(a/g.w),by=Math.floor(b/g.w);
  return Math.hypot(delta(b%g.w-a%g.w,g.w)*latitude(g,(ay+by)/2),by-ay);
}

// 沿构造带提取稀疏节点
function beltNodes(g,heights,uplift,boundary,gap){
  const candidates=[],at=(x,y)=>uplift[cell(g,x,y)];
  for(let i=0;i<g.n;i++){
    if(heights[i]<=0||uplift[i]<.16)continue;
    const x=i%g.w,y=Math.floor(i/g.w),c=latitude(g,y);
    const gx=(at(x+1,y)-at(x-1,y))/c,gy=at(x,y+1)-at(x,y-1),length=Math.hypot(gx,gy);
    const ox=length>1e-6?gx/length*gap/c:0,oy=length>1e-6?gy/length*gap:0;
    if(boundary[i]===1||(uplift[i]>=at(x+ox,y+oy)&&uplift[i]>=at(x-ox,y-oy)))candidates.push(i);
  }
  candidates.sort((a,b)=>uplift[b]-uplift[a]||a-b);
  const index=spatial(g,gap),nodes=[],limit=Math.round(g.w*1.4);
  for(const i of candidates){
    if(index.nearby(i,gap).some(j=>distance(g,i,j)<gap))continue;
    nodes.push(i);index.add(i,i);if(nodes.length>=limit)break;
  }
  return nodes;
}

// 连边不跨海，山系不强制闭环
function beltChains(g,nodes,heights,uplift,gap){
  const index=spatial(g,gap*3),edges=[],parents=nodes.map((_,i)=>i),adj=nodes.map(()=>[]);
  nodes.forEach((i,k)=>index.add(i,k));
  const find=k=>{while(parents[k]!==k){parents[k]=parents[parents[k]];k=parents[k];}return k;};
  for(let a=0;a<nodes.length;a++){
    const i=nodes[a],x=i%g.w,y=Math.floor(i/g.w),near=[];
    for(const b of index.nearby(i,gap*3.8)){
      if(a===b)continue;
      const j=nodes[b],length=distance(g,i,j);if(length>gap*3.8)continue;
      const dx=delta(j%g.w-x,g.w),dy=Math.floor(j/g.w)-y,steps=Math.ceil(Math.max(Math.abs(dx),Math.abs(dy)));
      let valid=true,strength=0;
      for(let s=0;s<=steps;s++){
        const k=cell(g,x+dx*s/steps,y+dy*s/steps);
        if(heights[k]<=0){valid=false;break;}strength+=uplift[k];
      }
      if(valid)near.push({a,b,score:length/(.4+strength/(steps+1))});
    }
    near.sort((a,b)=>a.score-b.score||a.b-b.b);edges.push(...near.slice(0,4));
  }
  edges.sort((a,b)=>a.score-b.score||a.a-b.a||a.b-b.b);
  const crowded=(a,b)=>{
    const i=nodes[a],j=nodes[b],y=Math.floor(i/g.w),c=latitude(g,y);
    const dx=delta(j%g.w-i%g.w,g.w)*c,dy=Math.floor(j/g.w)-y,length=Math.hypot(dx,dy);
    return adj[a].some(k=>{const q=nodes[k],qx=delta(q%g.w-i%g.w,g.w)*c,qy=Math.floor(q/g.w)-y;return (qx*dx+qy*dy)/Math.hypot(qx,qy)/length>.72;});
  };
  for(const {a,b} of edges){
    const pa=find(a),pb=find(b);
    if(pa===pb||adj[a].length>=3||adj[b].length>=3||crowded(a,b)||crowded(b,a))continue;
    parents[pa]=pb;adj[a].push(b);adj[b].push(a);
  }
  const used=new Set(),chains=[],key=(a,b)=>Math.min(a,b)*nodes.length+Math.max(a,b);
  for(let start=0;start<nodes.length;start++){
    if(adj[start].length===2)continue;
    for(const first of adj[start]){
      if(used.has(key(start,first)))continue;
      let previous=start,current=first;const chain=[nodes[start]];
      for(;;){
        used.add(key(previous,current));chain.push(nodes[current]);
        if(adj[current].length!==2)break;
        const next=adj[current].find(v=>v!==previous);previous=current;current=next;
      }
      chains.push({cells:chain,startJoined:adj[start].length>1,endJoined:adj[current].length>1,
        level:chain.reduce((sum,i)=>sum+uplift[i],0)/chain.length});
    }
  }
  return chains.sort((a,b)=>b.level-a.level||a.cells[0]-b.cells[0]);
}

function densify(g,nodes){
  const path=[nodes[0]];
  for(let k=1;k<nodes.length;k++){
    const a=nodes[k-1],b=nodes[k],x=a%g.w,y=Math.floor(a/g.w),dx=delta(b%g.w-x,g.w),dy=Math.floor(b/g.w)-y;
    const steps=Math.max(1,Math.ceil(Math.max(Math.abs(dx),Math.abs(dy))));
    for(let s=1;s<=steps;s++){const i=cell(g,x+dx*s/steps,y+dy*s/steps);if(i!==path.at(-1))path.push(i);}
  }
  return path;
}

function inland(g,heights,i,tx,ty,pushX,pushY,scale){
  const x=i%g.w,y=Math.floor(i/g.w),c=latitude(g,y);
  let nx=-ty,ny=tx;
  const score=sign=>[5,12,22].reduce((sum,s)=>sum+heights[cell(g,x+nx*s*scale*sign/c,y+ny*s*scale*sign)],0);
  if(score(-1)>score(1)){nx=-nx;ny=-ny;}
  const px=pushX?.[i]??0,py=pushY?.[i]??0,pn=Math.hypot(px,py);
  if(pn>1e-6){const sign=px*nx+py*ny<0?-1:1;nx=nx*.8+px/pn*.2*sign;ny=ny*.8+py/pn*.2*sign;}
  const length=Math.hypot(nx,ny);return [nx/length,ny/length];
}

// 分支从主脊原格连续延伸
function branch(g,heights,root,angle,length,seed,bend){
  let x=root%g.w,y=Math.floor(root/g.w);const path=[root];
  const phase=random(root,seed)*TAU;
  for(let s=1;s<=Math.ceil(length*2);s++){
    const t=s/(length*2),direction=angle+bend*t+.13*Math.sin(t*5+phase);
    const nx=x+Math.cos(direction)*.5/latitude(g,y),ny=y+Math.sin(direction)*.5;
    if(ny<1||ny>=g.h-1)break;
    const segment=densify(g,[cell(g,x,y),cell(g,nx,ny)]);
    if(segment.some(i=>heights[i]<=0))break;
    for(const i of segment)if(i!==path.at(-1))path.push(i);
    x=nx;y=ny;
  }
  return path;
}

// 缓存横截面，交汇取最大值
function rasterRidges(g,ridges,field,heights){
  const kernels=new Map();let samples=0;
  for(const ridge of ridges){
    const path=ridge.cells,total=Math.max(1,path.length-1),width=ridge.width;
    for(let s=0;s<path.length;s++){
      const i=path[s],x=i%g.w,y=Math.floor(i/g.w),along=s/total;
      const c=Math.round(latitude(g,y)*16)/16,key=width+':'+c;
      let kernel=kernels.get(key);
      if(!kernel){
        kernel=[];const radius=width*2.8,rx=Math.ceil(radius/c),ry=Math.ceil(radius);
        for(let dy=-ry;dy<=ry;dy++)for(let dx=-rx;dx<=rx;dx++){
          const d=Math.hypot(dx*c,dy);if(d>radius)continue;
          const q=d/width,value=.78*Math.exp(-q*q*2)+.22*Math.exp(-q*q*.35);
          kernel.push(dx,dy,value);
        }
        kernels.set(key,kernel);
      }
      const taper=Math.min(ridge.startJoined?1:smooth(along/.12),ridge.endJoined?1:smooth((1-along)/.18));
      const envelope=ridge.kind==='branch'?1-.65*smooth(along):.88+.12*Math.sin(Math.PI*along);
      let pass=1;
      if(ridge.pass!==null){const u=Math.abs(s-ridge.pass)/Math.max(2,width*1.8/c);if(u<1)pass=.46+.54*smooth(u);}
      const amplitude=ridge.amplitude*taper*envelope*pass;
      for(let k=0;k<kernel.length;k+=3){
        const yy=y+kernel[k+1];if(yy<0||yy>=g.h)continue;
        let xx=x+kernel[k];if(xx<0)xx+=g.w;else if(xx>=g.w)xx-=g.w;
        const j=yy*g.w+xx;if(heights[j]<=0)continue;
        const value=amplitude*kernel[k+2];if(value>field[j])field[j]=value;samples++;
      }
    }
  }
  return {kernels:kernels.size,samples};
}

// 山间抬升保留低口，不预灌湖水
function rasterLandform(g,form,field,heights){
  const {x,y,tx,ty,nx,ny,length,width,amplitude,kind}=form,c=latitude(g,y);
  const radius=Math.hypot(length,width),rx=Math.ceil(radius/c),ry=Math.ceil(radius);
  for(let dy=-ry;dy<=ry;dy++)for(let dx=-rx;dx<=rx;dx++){
    const yy=Math.round(y)+dy;if(yy<0||yy>=g.h)continue;
    const xx=Math.round(x)+dx,px=delta(xx-x,g.w)*c,py=yy-y;
    const u=(px*tx+py*ty)/length,v=(px*nx+py*ny)/width,r=Math.hypot(u,v);
    if(r>=1)continue;
    const i=cell(g,xx,yy);if(heights[i]<=0)continue;
    const edge=1-smooth((r-.48)/.52);
    const profile=kind==='plateau'?edge:(.10+.85*smooth(r/.72))*edge;
    const outlet=kind==='basin'?1-.85*(1-smooth(Math.abs(u)/.3))*smooth((v+.1)/.8):1;
    field[i]=Math.max(field[i],amplitude*profile*outlet);
  }
}

export function buildMountainRidges(g,o,heights,uplift,boundary,pushX,pushY){
  const scale=clamp(g.w/1024,.3,1),gap=Math.max(2,6*scale);
  const nodes=beltNodes(g,heights,uplift,boundary,gap),chains=beltChains(g,nodes,heights,uplift,gap);
  const ridges=[],landforms=[],ridgeField=new Float32Array(g.n),regionalField=new Float32Array(g.n);
  let budget=Math.round(g.w*10),added=0;
  for(const chain of chains){
    const path=densify(g,chain.cells);if(path.length<4)continue;
    const amplitude=Math.round(600+1050*clamp(chain.level,0,1)),width=Math.round(Math.max(1.2,(2.2+chain.level*.7)*scale)*4)/4;
    const id=ridges.length,pass=path.length>24*scale?Math.floor(path.length*(.36+.28*random(path[0],o.seed))):null;
    ridges.push({id,kind:'main',parent:null,cells:path,amplitude,width,pass,startJoined:chain.startJoined,endJoined:chain.endJoined});
    added+=path.length;
    const mid=path[Math.floor(path.length/2)],first=path[Math.floor(path.length*.2)],last=path[Math.floor(path.length*.8)];
    const y=Math.floor(mid/g.w),c=latitude(g,y),dx=delta(last%g.w-first%g.w,g.w)*c,dy=Math.floor(last/g.w)-Math.floor(first/g.w),span=Math.hypot(dx,dy);
    if(span<12*scale||budget<=0)continue;
    const tx=dx/span,ty=dy/span,[nx,ny]=inland(g,heights,mid,tx,ty,pushX,pushY,scale);
    const armLength=Math.min(42*scale,Math.max(15*scale,span*.65)),arms=[];
    const count=span>40*scale?3:2;
    for(let k=0;k<count&&budget>0;k++){
      const at=Math.round((.2+.6*k/(count-1))*(path.length-1)),root=path[at];
      const angle=Math.atan2(ny,nx)+(k/(count-1)-.5)*.45;
      const cells=branch(g,heights,root,angle,Math.min(armLength,budget),o.seed,(.5-k/(count-1))*.35);
      if(cells.length<4)continue;
      budget-=cells.length;added+=cells.length;arms.push(cells);
      ridges.push({id:ridges.length,kind:'branch',parent:id,rootIndex:at,cells,amplitude:Math.round(amplitude*.8),width:Math.max(1,Math.round(width*.72*4)/4),pass:null,startJoined:true,endJoined:false});
    }
    if(arms.length<2)continue;
    const reach=Math.min(...arms.map(a=>distance(g,a[0],a.at(-1))));if(reach<8*scale)continue;
    const form={parent:id,kind:random(mid,o.seed^0x706c6174)>.48?'plateau':'basin',
      x:wrap(mid%g.w+nx*reach*.42/c,g.w),y:y+ny*reach*.42,tx,ty,nx,ny,
      length:span*.64,width:reach*.68,amplitude:Math.round(amplitude*.52)};
    landforms.push(form);rasterLandform(g,form,regionalField,heights);
  }
  const work=rasterRidges(g,ridges,ridgeField,heights);let peak=0;
  for(let i=0;i<g.n;i++){
    const add=ridgeField[i]+regionalField[i];peak=Math.max(peak,add);heights[i]+=add;
  }
  softCap(heights);smoothHeights(g,heights,.16);
  return {ridges,landforms,added,peak:Math.round(peak),nodes:nodes.length,...work};
}

