import {TAU,TILE,clamp,tileKey,levelFor,visibleTiles,unproject} from './tiles.mjs';

export function ancestor(tile,z){
  const d=2**(tile.z-z),x=Math.floor(tile.x/d),y=Math.floor(tile.y/d);
  return {z,x,y,key:tileKey(z,x,y),distance:tile.distance};
}
export function tileAt(view,z,point){
  const p=unproject(view,point.x,point.y);if(!p)return null;
  const n=2**z,x=((Math.floor((p.lon/TAU+.5)*n)%n)+n)%n,y=clamp(Math.floor((.25-p.lat/TAU)*n),0,n/2-1);
  return {z,x,y,key:tileKey(z,x,y),distance:0};
}
export function planTiles(view,has,failed=new Set(),focus={x:view.width/2,y:view.height/2}){
  let z=levelFor(view),tiles=visibleTiles(view,z);
  while(tiles.length>512&&z>2){z--;tiles=visibleTiles(view,z);}
  const anchor=tileAt(view,z,focus),queue=new Map(),protectedKeys=new Set();
  const ordered=[...(anchor?[anchor]:[]),...tiles];
  const add=t=>{if(!has(t.key)&&!failed.has(t.key)&&!queue.has(t.key))queue.set(t.key,t);};
  for(const tile of ordered){
    let found=1;
    for(let at=tile.z;at>=2;at--){const p=ancestor(tile,at);protectedKeys.add(p.key);if(has(p.key)){found=at;break;}}
    if(found<tile.z){
      let next=Math.max(2,found+1);
      while(next<=tile.z&&failed.has(ancestor(tile,next).key))next++;
      if(next<=tile.z)add(ancestor(tile,next));
    }
  }
  if(!queue.size&&tiles.every(t=>has(t.key))&&anchor&&z<22){
    const n=2**(z+1),span=TAU/n,radius=TILE/(span*view.scale),point=unproject(view,focus.x,focus.y);
    if(point){
      const cx=Math.floor((point.lon/TAU+.5)*n),cy=Math.floor((.25-point.lat/TAU)*n);
      for(let dy=-1;dy<=1;dy++)for(let dx=-1;dx<=1;dx++){
        const x=((cx+dx)%n+n)%n,y=cy+dy;if(y<0||y>=n/2)continue;
        add({z:z+1,x,y,key:tileKey(z+1,x,y),distance:Math.hypot(dx,dy)*radius,prefetch:true});
      }
    }
  }
  return {tiles,queue:[...queue.values()].sort((a,b)=>a.distance-b.distance),protectedKeys,z};
}

export class TileStore{
  constructor(budget=48*2**20){this.budget=budget;this.bytes=0;this.items=new Map();this.protectedKeys=new Set();}
  id(style,key){return `${style}:${key}`;}
  has(style,key){return this.items.has(this.id(style,key));}
  get(style,key){const id=this.id(style,key),item=this.items.get(id);if(item){this.items.delete(id);this.items.set(id,item);}return item?.tile;}
  protect(style,keys){this.protectedKeys=new Set([...keys].map(key=>this.id(style,key)));this.trim();}
  set(style,tile){
    const id=this.id(style,tile.key),bytes=tile.canvas.width*tile.canvas.height*4;
    this.bytes-=this.items.get(id)?.bytes??0;this.items.delete(id);this.items.set(id,{tile,bytes});this.bytes+=bytes;this.trim();
  }
  trim(){
    for(const [id,item] of this.items){if(this.bytes<=this.budget)break;if(this.protectedKeys.has(id))continue;this.items.delete(id);this.bytes-=item.bytes;}
    // 视口超预算时仍遵守硬上限
    while(this.bytes>this.budget&&this.items.size){const [id,item]=this.items.entries().next().value;this.items.delete(id);this.bytes-=item.bytes;}
  }
}
