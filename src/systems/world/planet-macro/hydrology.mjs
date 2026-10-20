// 一次填洼，共享湖盆与水量账本
import {Heap} from './heap.mjs';
import {convergeDrainage} from './drainage-routing.mjs';
import {macroBandWidth,macroRiverThreshold} from './river-dimensions.mjs';
import {runoffM3s,evaporationM3s} from './water-balance.mjs';

const DEPTH_EPS=.01,HYPSE=.7;
// 显著深度、闭合湖面积、水膜、出口河占比
export const MIN_DEPRESSION_DEPTH=8,MIN_CLOSED_LAKE_KM2=400,MIN_WATER_DEPTH=.5,LAKE_OUTLET_SHARE=.05;
// 湖泊可见门槛
export const MIN_LAKE_CELLS=3,MIN_DEEP_LAKE=30;
// 主河道最小集水面积 km²
export const MIN_RIVER_CATCHMENT_KM2=25000;
export const MAX_PLAIN_LAKES=4,MAX_PLAIN_LAKE_DEPTH=30;
// 洼地状态
const OPEN=1,SALT=2,DRY=3,PLAIN=4;

// 闭合洼地：按平衡面积回推水面
export function closedLakeBalance({supply,evapUnit,area,maxDepth}){
  const unit=evaporationM3s(evapUnit,1);
  const equilibrium=unit>0?Math.max(0,supply)/unit:0;
  const wet=area>0?Math.min(1,equilibrium/area):0;
  const depth=maxDepth*Math.pow(Math.max(0,wet),HYPSE);
  return {equilibrium,depth,exists:equilibrium>=MIN_CLOSED_LAKE_KM2&&depth>=MIN_WATER_DEPTH};
}

// 湖盆内部统一接向最低序出口
function routeBasins(g,basins,cellSlot,receiver,rank,order,visited){
  visited.fill(0);
  for(const basin of basins){
    const root=basin.outlet;if(root<0)continue;
    let head=0,tail=1;order[0]=root;visited[root]=1;
    while(head<tail){
      const i=order[head++];
      for(let d=0;d<8;d++){
        const j=g.neighbors[i*8+d];
        if(visited[j]||cellSlot[j]!==basin.slot)continue;
        visited[j]=1;receiver[j]=i;order[tail++]=j;
      }
    }
  }
  // 复用队列，重建无环汇流顺序
  visited.fill(0);
  for(let i=0;i<g.n;i++)if(receiver[i]>=0)visited[receiver[i]]++;
  let head=0,tail=0;
  for(let i=0;i<g.n;i++)if(!visited[i])order[tail++]=i;
  while(head<tail){const i=order[head++],r=receiver[i];if(r>=0&&!--visited[r])order[tail++]=r;}
  if(tail!==g.n)throw Error('湖盆排水形成环');
  order.reverse();for(let at=0;at<g.n;at++)rank[order[at]]=at;
}

export function buildHydrology(g,o,height,climate,resistance){
  const n=g.n,receiver=new Int32Array(n).fill(-1),rank=new Uint32Array(n),order=new Uint32Array(n),spill=new Float32Array(n);
  const visited=new Uint8Array(n),heap=new Heap(n),water=new Uint8Array(n);
  const flow=new Float64Array(n),catchment=new Float64Array(n),riverWidth=new Float32Array(n);
  const lakeId=new Int32Array(n).fill(-1),cellSlot=new Int32Array(n).fill(-1),outletAt=new Int32Array(n).fill(-1);
  let sea=0,lowest=0;
  for(let i=0;i<n;i++){if(height[i]<height[lowest])lowest=i;if(height[i]<=0){visited[i]=1;spill[i]=0;water[i]=1;heap.push(i,0);sea++;}}
  if(!sea){visited[lowest]=1;spill[lowest]=height[lowest];heap.push(lowest,height[lowest]);}
  let count=0;
  while(heap.size){const i=heap.pop();rank[i]=count;order[count++]=i;
    for(let d=0;d<8;d++){const j=g.neighbors[i*8+d];if(visited[j])continue;visited[j]=1;spill[j]=Math.max(height[j],spill[i]);receiver[j]=i;heap.push(j,spill[j]);}
  }
  for(let at=0;at<count;at++){
    const i=order[at];if(height[i]<=0||receiver[i]<0)continue;
    let best=receiver[i],grade=-Infinity;
    for(let d=0;d<8;d++){const j=g.neighbors[i*8+d];if(rank[j]>=rank[i]||spill[j]>spill[i]+.001)continue;
      const candidate=(spill[i]-spill[j])/Math.max(1,g.lengths[i*8+d]);if(candidate>grade){grade=candidate;best=j;}}
    receiver[i]=best;
  }
  convergeDrainage(g,height,spill,rank,order,receiver,catchment,flow,resistance);

  // 产流只在这里算一次
  let input=0,landArea=0;
  for(let i=0;i<n;i++)if(height[i]>0){
    flow[i]=runoffM3s(climate.rainfall[i],climate.evaporation[i],g.areas[i]);
    catchment[i]=g.areas[i];
    input+=flow[i];landArea+=g.areas[i];
  }

  // 洼地分组：溢流高程相同且相邻
  const parent=new Int32Array(n);
  for(let i=0;i<n;i++)parent[i]=i;
  const find=i=>{while(parent[i]!==i){parent[i]=parent[parent[i]];i=parent[i];}return i;};
  for(let i=0;i<n;i++){
    if(height[i]<=0||spill[i]-height[i]<=DEPTH_EPS)continue;
    for(const d of [1,3,6,7]){
      const j=g.neighbors[i*8+d];
      if(height[j]<=0||spill[j]-height[j]<=DEPTH_EPS||Math.abs(spill[j]-spill[i])>DEPTH_EPS*.5)continue;
      const a=find(i),b=find(j);
      if(a!==b){if(a<b)parent[b]=a;else parent[a]=b;}
    }
  }
  // 每个洼地只统计一次
  const slotOfRoot=new Map(),basins=[];
  for(let i=0;i<n;i++){
    if(height[i]<=0||spill[i]-height[i]<=DEPTH_EPS)continue;
    const root=find(i);parent[i]=root;
    let slot=slotOfRoot.get(root);
    if(slot===undefined){
      slot=basins.length;slotOfRoot.set(root,slot);
      basins.push({root,slot,area:0,cells:0,minHeight:Infinity,minCell:i,evapArea:0,rainVolume:0,landRunoff:0,depthArea:0,minX:0,maxX:0,minY:Infinity,maxY:0,outlet:-1,
        capacity:0,evapUnit:0,maxDepth:0,supply:0,overflow:0,significant:false,visible:false});
    }
    const basin=basins[slot],area=g.areas[i];
    basin.area+=area;basin.cells++;
    if(height[i]<basin.minHeight){basin.minHeight=height[i];basin.minCell=i;}
    basin.evapArea+=climate.evaporation[i]*area;
    basin.rainVolume+=climate.rainfall[i]*area*1000/31557600;
    basin.landRunoff+=flow[i];
    basin.depthArea+=(spill[i]-height[i])*area;
    const dx=((i%g.w-root%g.w+g.w*1.5)%g.w)-g.w*.5,y=Math.floor(i/g.w);
    basin.minX=Math.min(basin.minX,dx);basin.maxX=Math.max(basin.maxX,dx);
    basin.minY=Math.min(basin.minY,y);basin.maxY=Math.max(basin.maxY,y);
    if(basin.outlet<0||rank[i]<rank[basin.outlet])basin.outlet=i;
    cellSlot[i]=slot;
  }
  const shallow=[];
  for(const basin of basins){
    if(basin.outlet<0)continue;
    basin.evapUnit=basin.area>0?basin.evapArea/basin.area:0;
    basin.capacity=evaporationM3s(basin.evapUnit,basin.area);
    basin.maxDepth=spill[basin.outlet]-basin.minHeight;
    basin.significant=basin.maxDepth>=MIN_DEPRESSION_DEPTH;
    basin.visible=basin.significant&&(basin.cells>=MIN_LAKE_CELLS||basin.maxDepth>=MIN_DEEP_LAKE);
    basin.plain=basin.maxDepth<=MAX_PLAIN_LAKE_DEPTH&&basin.maxDepth>=1&&basin.depthArea/basin.area<=13&&spill[basin.outlet]<=300;
    if(basin.plain){
      basin.visible=false;
      if(basin.cells>=3&&basin.area>=2000&&basin.area<=20000&&basin.maxX>basin.minX&&basin.maxY>basin.minY&&basin.rainVolume>=basin.capacity*.65)shallow.push(basin);
    }
  }
  shallow.sort((a,b)=>b.area*b.rainVolume/Math.max(1,b.capacity)-a.area*a.rainVolume/Math.max(1,a.capacity)||a.root-b.root);
  for(const basin of shallow.slice(0,MAX_PLAIN_LAKES))basin.visible=true;
  routeBasins(g,basins,cellSlot,receiver,rank,order,visited);

  const status=new Uint8Array(basins.length),level=new Float32Array(basins.length),lakeIndex=new Int32Array(basins.length).fill(-1);
  for(const basin of basins)if(!basin.visible)status[basin.slot]=PLAIN;
  // 汇流：下游累加，湖口一次判定
  let lakeLoss=0,lakeRain=0,lakeRunoff=0,dryLoss=0,riverLoss=0;
  const oases=[];
  const threshold=macroRiverThreshold(o.equatorChunks/o.columns,o.precipitation);
  for(let at=count-1;at>=0;at--){
    const i=order[at],slot=cellSlot[i];
    // 干旱河段按实际来水扣损失
    if(slot<0&&height[i]>0&&receiver[i]>=0&&climate.rainfall[i]<250&&flow[i]>threshold*.25){
      const area=macroBandWidth(flow[i])*Math.sqrt(g.areas[i])*.004;
      const loss=Math.min(flow[i]*.08,evaporationM3s(Math.max(0,climate.evaporation[i]-climate.rainfall[i]),area));
      flow[i]-=loss;riverLoss+=loss;
    }
    if(slot>=0&&basins[slot].outlet===i&&basins[slot].visible){
      const basin=basins[slot],q=flow[i];
      basin.supply=Math.max(0,q-basin.landRunoff+basin.rainVolume);
      const open=basin.supply>0&&basin.supply>=basin.capacity;
      basin.overflow=open?basin.supply-basin.capacity:0;
      if(open){
        status[slot]=OPEN;level[slot]=spill[i];
        basin.wetShare=1;basin.loss=basin.capacity;
        if(receiver[i]>=0&&basin.overflow>threshold*LAKE_OUTLET_SHARE)outletAt[receiver[i]]=slot;
      }else{
        const cost=basin.capacity-basin.rainVolume+basin.landRunoff;
        basin.wetShare=cost>0?Math.min(1,q/cost):0;
        const balance=closedLakeBalance({...basin,supply:basin.capacity*basin.wetShare});
        status[slot]=balance.exists?SALT:DRY;
        level[slot]=basin.minHeight+balance.depth;
        if(status[slot]===SALT){
          basin.supply=q+basin.wetShare*(basin.rainVolume-basin.landRunoff);
          basin.loss=basin.supply;
        }else{basin.wetShare=0;basin.loss=0;dryLoss+=q;}
        const oasisArea=basin.evapUnit>0?q/evaporationM3s(basin.evapUnit,1):0;
        if(basin.rainVolume<basin.capacity*.45&&oasisArea>=2&&(climate.temperature?.[basin.minCell]??20)>5){
          oases.push({cell:basin.minCell,areaKm2:Math.min(400,oasisArea*.25),inflowM3s:q,lake:status[slot]===SALT,slot,shore:-1,gap:Infinity});
        }
        receiver[i]=-1;
      }
      lakeRain+=basin.rainVolume*basin.wetShare;
      lakeRunoff+=basin.landRunoff*basin.wetShare;
      lakeLoss+=basin.loss;
      flow[i]=basin.overflow;
    }
    const r=receiver[i];if(r>=0){flow[r]+=flow[i];catchment[r]+=catchment[i];}
  }

  // 水面与湖泊记录
  const lakes=[];
  for(const basin of basins){
    if(status[basin.slot]!==OPEN&&status[basin.slot]!==SALT)continue;
    lakeIndex[basin.slot]=lakes.length;
    lakes.push({id:lakes.length,level:level[basin.slot],areaKm2:0,cells:0,maxDepth:0,
      outlet:status[basin.slot]===OPEN&&receiver[basin.outlet]>=0?receiver[basin.outlet]:-1,exitCell:basin.outlet,
      outflowM3s:basin.overflow,inflowM3s:basin.supply,evaporationM3s:basin.loss,
      rainfallM3s:basin.rainVolume*basin.wetShare,landRunoffReplacedM3s:basin.landRunoff*basin.wetShare,
      plain:basin.plain,estimatedAreaKm2:basin.area*basin.wetShare,waterBalanceVersion:2,
      closed:status[basin.slot]===SALT,salinity:status[basin.slot]===SALT?'salt':'fresh'});
  }
  for(let i=0;i<n;i++){
    const slot=cellSlot[i];if(slot<0)continue;
    const entry=lakeIndex[slot];if(entry<0)continue;
    const depth=level[slot]-height[i];if(depth<=DEPTH_EPS)continue;
    water[i]=2;lakeId[i]=entry;
    const lake=lakes[entry];lake.areaKm2+=g.areas[i];lake.cells++;
    if(depth>lake.maxDepth)lake.maxDepth=depth;
  }
  oases.sort((a,b)=>b.inflowM3s-a.inflowM3s||a.cell-b.cell);oases.length=Math.min(8,oases.length);
  const oasisSlots=new Map(oases.filter(v=>v.lake).map(v=>[v.slot,v]));
  // 河网：一次判定，含湖出口
  const average=g.areas.reduce((a,b)=>a+b,0)/n;
  let riverCells=0;
  visited.fill(0);
  for(const i of order){
    const oasis=oasisSlots.get(cellSlot[i]);
    if(oasis&&water[i]===2)for(let d=0;d<8;d++){
      const j=g.neighbors[i*8+d],gap=Math.abs(height[j]-level[oasis.slot]);
      if(height[j]>0&&water[j]!==2&&gap<oasis.gap){oasis.shore=j;oasis.gap=gap;}
    }
    if(water[i]===2&&lakes[lakeId[i]].plain)visited[i]=1;
    if(height[i]<=0||water[i]===2)continue;
    const basinSlot=cellSlot[i];
    if(basinSlot>=0&&(status[basinSlot]===SALT||status[basinSlot]===DRY))continue;
    const qualifies=flow[i]>threshold&&catchment[i]>Math.max(average*4,MIN_RIVER_CATCHMENT_KM2);
    const slot=outletAt[i];
    const outlet=slot>=0&&status[slot]===OPEN&&flow[i]>threshold*LAKE_OUTLET_SHARE;
    const r=receiver[i];
    const inlet=r>=0&&visited[r]&&flow[i]>threshold*.25&&catchment[i]>Math.max(g.areas[i]*2,1000);
    if(inlet)visited[i]=1;
    if(!qualifies&&!outlet&&!inlet)continue;
    water[i]=3;riverWidth[i]=macroBandWidth(flow[i])/2;riverCells++;
  }
  let output=0;for(let i=0;i<n;i++)if(receiver[i]<0)output+=flow[i];
  let dryBasins=0,saltBasins=0;for(const basin of basins)if(status[basin.slot]===DRY)dryBasins++;else if(status[basin.slot]===SALT)saltBasins++;
  for(const oasis of oases){if(oasis.shore>=0)oasis.cell=oasis.shore;delete oasis.slot;delete oasis.shore;delete oasis.gap;}
  return {receiver,rank,spill,flow,catchment,riverWidth,lakeId,water,lakes,oases,input,output,landArea,lakeLoss,lakeRain,lakeRunoff,dryLoss,riverLoss,dryBasins,saltBasins,
    shallowCandidates:shallow.length,plainLakes:lakes.filter(l=>l.plain).length,
    terminal:sea?'ocean':'closed-basin'};
}
