export const PLATE_LAYOUT='major-minor-v1';
const TAU=Math.PI*2;
const dot=(a,b)=>a[0]*b[0]+a[1]*b[1]+a[2]*b[2];
const cross=(a,b)=>[a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0]];
const unit=p=>{const n=Math.hypot(...p)||1;return p.map(v=>v/n);};
const sphere=(lon,y)=>{const r=Math.sqrt(Math.max(0,1-y*y));return [r*Math.cos(lon),y,r*Math.sin(lon)];};

export function planetSize(columns){
  if(!Number.isInteger(columns)||columns<64||columns>1024)throw Error('星球规模须为64～1024列');
  const major=Math.max(4,Math.round(columns/64)),minor=Math.max(1,Math.round(major/4));
  return {major,minor,total:major+minor,name:columns<=256?'小':columns<=512?'中':'大'};
}

export function createPlates(options,rng){
  const {major,minor}=planetSize(options.columns),plates=[];
  for(let id=0;id<major;id++){
    let center,best=-Infinity;
    for(let attempt=0;attempt<12;attempt++){
      const p=sphere(rng()*TAU,rng()*2-1),separation=plates.length?1-Math.max(...plates.map(b=>dot(p,b.start))):1;
      if(separation>best){best=separation;center=p;}
    }
    const axis=unit(cross(center,sphere(rng()*TAU,rng()*2-1)));
    plates.push({id,kind:'major',start:center,center:[...center],axis,speed:.65+rng()*.7,oceanic:rng()>.5,bias:(rng()-.5)*.025});
  }
  const candidates=[];
  for(let a=0;a<major;a++)for(let b=a+1;b<major;b++){
    const pa=plates[a],pb=plates[b],delta=pa.start.map((v,k)=>v-pb.start[k]),length=Math.hypot(...delta);
    const offset=(pb.bias-pa.bias)/length,mid=unit(pa.start.map((v,k)=>v+pb.start[k]));
    const center=unit(mid.map((v,k)=>v*Math.sqrt(1-offset*offset)+delta[k]/length*offset));
    const score=dot(center,pa.start)+pa.bias;
    if(plates.some(p=>dot(center,p.start)+p.bias>score+1e-9))continue;
    candidates.push({center,neighbors:[a,b],priority:rng()});
  }
  const used=new Set();
  for(let k=0;k<minor;k++){
    let chosen,best=-Infinity;
    for(const candidate of candidates){
      if(used.has(candidate))continue;
      const clearance=k?Math.min(...plates.slice(major).map(p=>1-dot(candidate.center,p.start))):1;
      const score=clearance*(.8+candidate.priority*.4);
      if(score>best){best=score;chosen=candidate;}
    }
    if(!chosen)throw Error('小板块交界候选不足');
    used.add(chosen);
    const [a,b]=chosen.neighbors.map(id=>plates[id]),center=chosen.center;
    const radius=Math.acos(1-2/major)*(.28+rng()*.1),axis=unit(a.axis.map((v,i)=>v+b.axis[i]));
    plates.push({id:major+k,kind:'minor',neighbors:chosen.neighbors,radius,capCos:Math.cos(radius),start:center,center:[...center],axis,
      speed:(a.speed+b.speed)/2,oceanic:rng()>.5,bias:0});
  }
  return plates;
}

// 小板块只接管有限球冠
export function assignPlateIds(points,plates,ids){
  const major=plates.filter(p=>p.kind==='major'),minor=plates.filter(p=>p.kind==='minor');
  for(let i=0;i<ids.length;i++){
    const at=i*3,x=points[at],y=points[at+1],z=points[at+2];let best=-Infinity,id=0;
    for(const p of major){const score=x*p.center[0]+y*p.center[1]+z*p.center[2]+p.bias;if(score>best){best=score;id=p.id;}}
    let closest=1;
    for(const p of minor){
      const distance=(1-(x*p.center[0]+y*p.center[1]+z*p.center[2]))/(1-p.capCos);
      if(distance<closest){closest=distance;id=p.id;}
    }
    ids[i]=id;
  }
  return ids;
}

export function isCurrentPlateLayout(world){
  if(world?.options?.plateLayout!==PLATE_LAYOUT)return false;
  const {major,minor,total}=planetSize(world.columns),o=world.options,p=world.plates;
  return o.majorPlateCount===major&&o.minorPlateCount===minor&&o.plateCount===total&&Array.isArray(p)&&p.length===total&&
    p.every((v,i)=>v.id===i&&v.kind===(i<major?'major':'minor')&&(i<major||Number.isFinite(v.radius)&&v.radius>0&&v.radius<1&&v.capCos===Math.cos(v.radius)));
}

export function requirePlateLayout(world){
  if(!isCurrentPlateLayout(world))throw Error('板块布局版本不符，请按星球规模重新生成宏观骨架');
}
