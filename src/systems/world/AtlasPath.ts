export interface PathPoint {x:number;z:number}
interface Node extends PathPoint {cost:number;rank:number;parent?:Node}
export function atlasPath(start:PathPoint,target:PathPoint,passable:(a:PathPoint,b:PathPoint)=>boolean,limit=512,stride=1):PathPoint[]|null {
  const snap=(v:number)=>Math.round(v/stride)*stride;
  const goal={x:snap(target.x),z:snap(target.z)},origin={x:snap(start.x),z:snap(start.z)};
  const distance=(p:PathPoint)=>Math.hypot(p.x-goal.x,p.z-goal.z);
  const heap:Node[]=[{...origin,cost:0,rank:distance(origin)}],best=new Map<string,number>();
  best.set(`${origin.x},${origin.z}`,0);
  const push=(node:Node)=>{let i=heap.length;heap.push(node);while(i){const p=(i-1)>>1;if(heap[p].rank<=node.rank)break;heap[i]=heap[p];i=p;}heap[i]=node;};
  const pop=()=>{const top=heap[0],last=heap.pop()!;if(heap.length){let i=0;while(i*2+1<heap.length){let c=i*2+1;if(c+1<heap.length&&heap[c+1].rank<heap[c].rank)c++;if(heap[c].rank>=last.rank)break;heap[i]=heap[c];i=c;}heap[i]=last;}return top;};
  for(let explored=0;heap.length&&explored<limit;explored++){
    const node=pop();if(node.cost!==best.get(`${node.x},${node.z}`))continue;
    if(distance(node)<.5){const path:PathPoint[]=[];let p:Node|undefined=node;while(p?.parent){path.push({x:p.x,z:p.z});p=p.parent;}return path.reverse();}
    for(const [dx,dz] of [[1,0],[-1,0],[0,1],[0,-1]]){
      const next={x:node.x+dx*stride,z:node.z+dz*stride},key=`${next.x},${next.z}`,cost=node.cost+stride;
      if(cost>=(best.get(key)??Infinity)||!passable(node,next))continue;
      best.set(key,cost);push({...next,cost,rank:cost+distance(next),parent:node});
    }
  }
  return null;
}
