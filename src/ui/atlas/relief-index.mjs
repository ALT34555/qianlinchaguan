// 全球等高线索引，详见同名文档
import {landHeight, lakeMask, macroAt} from '../../systems/world/planet-macro/backend.mjs';
import {elevationIndex, wetIndex, depthIndex, DEPTH_SEA, DEPTH_LAKE} from './relief.mjs';

/** 索引分块边长，进度止于此 */
export const CHUNK=128;
/** 细格相对宏观格 */
const SUB=2;
const cache=new WeakMap();

export function indexFor(world){let value=cache.get(world);if(!value){value=new ReliefIndex(world);cache.set(world,value);}return value;}

export class ReliefIndex{
  constructor(world){
    const w=this.world=world,n=world.columns*world.rows;
    this.columns=world.columns;this.rows=world.rows;this.chunks=Math.max(1,world.columns/CHUNK);
    this.span=w.circumference/w.columns;this.step=this.span/SUB;
    this.land=new Float32Array(n);this.snow=new Uint8Array(n);this.wet=new Uint8Array(n);
    this.depth=new Uint8Array(n);this.owner=new Uint8Array(n);
    this.level=new Uint8Array(n);this.cos=new Float32Array(n);
    this.lakeCells=null;this.done=0;this.version=0;this.maxRain=0;this.samples=0;
    for(let i=0;i<n;i++)if(w.rainfall[i]>this.maxRain)this.maxRain=w.rainfall[i];
  }
  advance(cancel){
    if(cancel())return true;
    if(this.done>=this.chunks)return false;
    this.chunk(this.done++);this.version++;
    return this.done<this.chunks;
  }
  get ready(){return this.done>=this.chunks;}
  finish(){while(this.done<this.chunks)this.chunk(this.done++);this.version++;}

  chunk(done){
    const cy=Math.floor(done/this.chunks)*CHUNK,cx=(done%this.chunks)*CHUNK;
    for(let y=cy;y<cy+CHUNK;y++)for(let x=cx;x<cx+CHUNK;x++)this.finalize(x,y);
    for(let pass=0;pass<CHUNK+2;pass++){
      let changed=false;
      for(let y=cy-pass;y<cy+CHUNK+pass;y++)for(let x=cx-pass;x<cx+CHUNK+pass;x++){
        if(x<0||x>=this.columns||y<0||y>=this.rows)continue;
        if(this.relax(x,y))changed=true;
      }
      if(!changed)break;
    }
  }
  index(x,y){
    if(y<0){y=-y-1;x+=this.columns/2;}else if(y>=this.rows){y=this.rows*2-y-1;x+=this.columns/2;}
    return y*this.columns+((x%this.columns)+this.columns)%this.columns;
  }
  relax(x,y){
    const i=y*this.columns+x,current=this.level[i];
    if(this.owner[i]){const value=this.owner[i]===1?1:2;if(current===value)return false;this.level[i]=value;return true;}
    let best=255;
    for(let dy=-1;dy<=1;dy++)for(let dx=-1;dx<=1;dx++){
      if(!dx&&!dy)continue;const v=this.level[this.index(x+dx,y+dy)];
      if(v>1&&v+1<best)best=v+1;
    }
    const value=Math.min(255,best===255?2:best);
    if(value===current)return false;
    this.level[i]=value;return true;
  }
  finalize(x,y){
    const w=this.world,i=y*w.columns+x,height=w.height[i],water=w.water[i],temp=w.temperature[i];
    this.land[i]=height>0?height:0;
    this.snow[i]=this.snowIndex(temp);
    this.wet[i]=this.maxRain>0?wetIndex(temp,w.rainfall[i],this.maxRain):0;
    this.owner[i]=height<=0||water===1?1:water===2?2:0;
    this.depth[i]=water===1?depthIndex(0,height,DEPTH_SEA):water===2?depthIndex(w.spill[i]??0,height,DEPTH_LAKE):0;
    this.cos[i]=Math.max(.01,Math.cos(Math.PI/2-(y+.5)*Math.PI/w.rows));
    this.level[i]=0;
  }
  snowIndex(temp){return Math.max(0,Math.min(255,Math.round(255-temp*5.5)));}
  cellAt(x,y){
    const cx=Math.min(this.columns-1,Math.max(0,Math.floor((x+.5)/SUB))),cy=Math.min(this.rows-1,Math.max(0,Math.floor((y+.5)/SUB)));
    return cy*this.columns+cx;
  }
  fine(x,y){
    const w=this.world,cell=this.cellAt(x,y);
    const wx=(x+.5)*this.step-w.circumference/2,wz=(y+.5)*this.step-w.circumference/4;
    const height=landHeight(w,wx,wz);
    let kind=height<0&&this.owner[cell]===1?1:0,water=-Infinity;
    if(!kind&&this.nearLake(cell)){const lake=lakeMask(w,wx,wz,height);if(lake.kind===2){kind=2;water=lake.water;}}
    this.samples++;
    return {height,kind,water,temperature:macroAt(w,wx,wz,'temperature'),rainfall:w.rainfall[cell]};
  }
  nearLake(cell){
    const flag=this.lakeCells??this.buildLakeCells();
    return flag[cell]===1;
  }
  buildLakeCells(){
    const w=this.world,n=w.columns*w.rows,flag=new Uint8Array(n);
    for(let i=0;i<n;i++)if(w.water[i]===2)for(let dy=-1;dy<=2;dy++)for(let dx=-1;dx<=2;dx++){
      const x=(i%w.columns)+dx,y=Math.floor(i/w.columns)+dy;if(x<0||x>=w.columns||y<0||y>=w.rows)continue;
      flag[y*w.columns+x]=1;
    }
    this.lakeCells=flag;return flag;
  }
  fineIndex(sample){
    if(sample.kind===1||sample.kind===2)return Math.max(1,Math.min(120,Math.round(128+Math.log2(Math.max(1,sample.height)/128)*8)));
    return elevationIndex(sample.height);
  }
}
