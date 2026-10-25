const clamp=v=>Math.max(0,Math.min(1,v));
const smooth=v=>{const t=clamp(v);return t*t*(3-2*t);};
const FORMS=[
  [[0,0],[.28,.65],[.68,1],[1,0]],
  [[0,0],[.23,.85],[.71,-.5],[1,0]],
  [[0,0],[.18,.4],[.48,.95],[.81,-.35],[1,0]],
  [[0,0],[.37,.18],[.76,-.28],[1,0]],
];
function random(seed){return()=>{let t=seed+=0x6d2b79f5;t=Math.imul(t^t>>>15,t|1);t^=t+Math.imul(t^t>>>7,t|61);return ((t^t>>>14)>>>0)/4294967296;};}

// 单调前进，不等距弯点
export function templateTributaryPath(source,target,seed,attempt=0){
  const dx=target.x-source.x,dz=target.z-source.z,length=Math.hypot(dx,dz);
  if(!length)return [];
  const rng=random(seed^Math.imul(attempt+1,0x9e3779b1)),form=FORMS[Math.floor(rng()*FORMS.length)],side=rng()<.5?-1:1;
  const amplitude=Math.min(length*.085,420)*(.75+rng()*.5);
  const knots=form.map(([t,v],k)=>k===0||k===form.length-1?{t,v:0}:{t:t+(rng()-.5)*.07,v:v*(.75+rng()*.5)*side});
  const slopes=knots.slice(1).map((b,k)=>(b.v-knots[k].v)/(b.t-knots[k].t));
  const tangents=knots.map((_,k)=>{
    if(!k)return slopes[0];if(k===knots.length-1)return slopes.at(-1);
    const a=slopes[k-1],b=slopes[k];return a*b<=0?0:2*a*b/(a+b);
  });
  const steps=Math.max(24,Math.min(192,Math.ceil(length/32))),points=[];let section=0;
  for(let k=0;k<=steps;k++){
    const t=k/steps;while(section<knots.length-2&&t>knots[section+1].t)section++;
    const a=knots[section],b=knots[section+1],span=b.t-a.t,u=(t-a.t)/span,u2=u*u,u3=u2*u;
    const offset=amplitude*((2*u3-3*u2+1)*a.v+(u3-2*u2+u)*span*tangents[section]+(-2*u3+3*u2)*b.v+(u3-u2)*span*tangents[section+1]);
    points.push({x:source.x+dx*t-dz/length*offset,z:source.z+dz*t+dx/length*offset});
  }
  points[0]={x:source.x,z:source.z};points[steps]={x:target.x,z:target.z};return points;
}

// 纵剖面由端点确定，无地势扫描
export function templateTributaryProfile(points,source){
  if(!points||points.length<2)return null;
  const lengths=[0];for(let k=1;k<points.length;k++)lengths.push(lengths[k-1]+Math.hypot(points[k].x-points[k-1].x,points[k].z-points[k-1].z));
  const total=lengths.at(-1),start=source.level,target=points.at(-1),end=target.level;
  if(total<64||total>32768||![start,end].every(Number.isFinite)||start<=end)return null;
  const receiverLimit=Number.isFinite(target.halfWidth)?target.halfWidth*(target.sourceType==='source'?.7:.4):Infinity;
  const maximum=Math.min(source.halfWidth,total*.01,receiverLimit),minimum=source.minHalfWidth;
  if(maximum<minimum)return null;
  const head=Math.max(minimum,Math.min(maximum,source.halfWidth*.3,2));
  return points.map((p,k)=>{
    const t=lengths[k]/total,level=start+(end-start)*t,halfWidth=head+(maximum-head)*smooth(t);
    const depth=Math.min(source.depth,halfWidth*2*.22,2.5);
    return {...p,level:k===points.length-1?end:level,halfWidth,depth,bedHeight:level-depth};
  });
}

export const tributaryValleyRadius=radius=>Math.max(32,radius*3+20);

// 同一控制线生成谷底、谷坡与河槽
export function templateTributarySection(distance,radius,depth,natural,level){
  const valleyRadius=tributaryValleyRadius(radius),r=distance/valleyRadius;
  if(r>=1)return null;
  const shoulder=level+.18+Math.max(1,radius*.35)*smooth((distance-radius)/Math.max(1,valleyRadius-radius));
  const ground=natural+(shoulder-natural)*(1-smooth((distance-radius)/Math.max(1,valleyRadius-radius)));
  const waterDepth=Math.min(depth,radius*2*.22,2.5),bed=level-waterDepth,core=distance/radius;
  const height=core<1?bed+(level+.18-bed)*smooth(core):ground;
  return {level,bed,decorative:true,templateTerrain:true,valleyRadius,section:{height,wet:core<1&&height<level}};
}
