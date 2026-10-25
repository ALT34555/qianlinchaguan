import { atlasPath,type PathPoint } from './AtlasPath';
import { WALK_SPEED,SPRINT_SPEED,SWIM_SPEED } from '../../entities/Player';
import { movementScale } from '../../entities/PlayerProfile';
export interface AtlasPosition extends PathPoint {y:number;yaw:number}
export interface AtlasGround {height(x:number,z:number):number;water(x:number,z:number):number;circumference?:number}
export class AtlasWalker {
  private goal?:PathPoint;
  private path:PathPoint[]=[];
  private vx=0;
  private vz=0;
  private planned=false;
  private speedScale=1;
  private heightCm=170;
  constructor(readonly position:AtlasPosition,private readonly ground:AtlasGround){}
  setHeight(heightCm:number):void {if(heightCm===this.heightCm)return;this.heightCm=heightCm;this.speedScale=movementScale(heightCm);this.path=[];this.planned=false;}
  target(point:PathPoint):void {
    this.goal={...point};this.path=[];this.planned=false;
    const n=this.ground.circumference;if(n)this.goal.x+=Math.round((this.position.x-this.goal.x)/n)*n;
  }
  private passable=(a:PathPoint,b:PathPoint):boolean=>{
    const n=this.ground.circumference;if(n&&Math.abs(b.z)>=n/4)return false;
    const rise=this.ground.height(b.x,b.z)-this.ground.height(a.x,a.z);return rise<=this.speedScale&&rise>=-3*this.speedScale;
  };
  tick(dt:number,dx:number,dz:number,sprint=false):{position:AtlasPosition;blocked:boolean;walking:boolean}{
    dt=Math.min(.1,dt);const p=this.position;let blocked=false;
    if(dx||dz){this.goal=undefined;this.path=[];}
    if(this.goal&&!this.planned){
      this.planned=true;
      const distance=Math.hypot(this.goal.x-p.x,this.goal.z-p.z);
      if(distance<1)this.goal=undefined;
      else{
        const cx=Math.floor(p.x/64),cz=Math.floor(p.z/64),left=(cx-2)*64,right=(cx+3)*64-1,top=(cz-2)*64,bottom=(cz+3)*64-1;
        const step=Math.min(1,96/distance),stage={x:p.x+(this.goal.x-p.x)*step,z:p.z+(this.goal.z-p.z)*step};
        this.path=atlasPath(p,stage,(a,b)=>{
          if(b.x<left||b.x>right||b.z<top||b.z>bottom)return false;
          let prev=a;
          for(let i=1;i<=4;i++){const next={x:a.x+(b.x-a.x)*i/4,z:a.z+(b.z-a.z)*i/4};if(!this.passable(prev,next))return false;prev=next;}
          return true;
        },512,4)??[];
      }
    }
    if(this.path.length){
      const next=this.path[0],distance=Math.hypot(next.x-p.x,next.z-p.z);
      if(distance<.3)this.path.shift();else{dx=(next.x-p.x)/distance;dz=(next.z-p.z)/distance;}
    }else if(this.goal){
      const distance=Math.hypot(this.goal.x-p.x,this.goal.z-p.z);
      if(distance<.5)this.goal=undefined;else{dx=(this.goal.x-p.x)/distance;dz=(this.goal.z-p.z)/distance;}
    }
    const length=Math.hypot(dx,dz);if(length>1){dx/=length;dz/=length;}
    const water=this.ground.water(p.x,p.z)>this.ground.height(p.x,p.z)+.4;
    const speed=(water?SWIM_SPEED:sprint?SPRINT_SPEED:WALK_SPEED)*this.speedScale,k=Math.min(1,14*dt);
    this.vx+=(dx*speed-this.vx)*k;this.vz+=(dz*speed-this.vz)*k;
    if(this.goal&&!this.passable(p,{x:p.x+this.vx*dt,z:p.z+this.vz*dt})){
      this.goal=undefined;this.path=[];this.vx=this.vz=0;blocked=true;
    }else{
      if(this.passable(p,{x:p.x+this.vx*dt,z:p.z}))p.x+=this.vx*dt;else this.vx=0;
      if(this.passable(p,{x:p.x,z:p.z+this.vz*dt}))p.z+=this.vz*dt;else this.vz=0;
    }
    if(length)p.yaw=Math.atan2(-dx,-dz);
    p.y=Math.max(this.ground.height(p.x,p.z),this.ground.water(p.x,p.z)-.4)+.01;
    return {position:{...p},blocked,walking:!!this.goal};
  }
}
