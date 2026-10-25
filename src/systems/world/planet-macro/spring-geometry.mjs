const clamp=t=>Math.max(0,Math.min(1,t));
const smooth=t=>{t=clamp(t);return t*t*(3-2*t);};
const FORMS={
  bowl:{rx:1,rz:.9,shift:0,skew:0,depth:.52,rise:.3,rocks:[[-1,-.65,1.4,'rock.boulder'],[-.55,1,1.2,'rock.slab'],[.25,-1,1,'rock.rubble'],[-1.15,.25,1.3,'rock.boulder']]},
  oval:{rx:1.5,rz:.65,shift:-.2,skew:.15,depth:.48,rise:.45,rocks:[[-1.5,-.4,1.3,'rock.slab'],[-1.2,.65,1.1,'rock.boulder'],[.15,-.75,.8,'rock.rubble'],[-.4,.9,1,'rock.slab']]},
  twin:{rx:1.05,rz:.65,shift:-.15,lobe:.3,skew:0,depth:.48,rise:.35,rocks:[[-1,-.9,1.2,'rock.boulder'],[-.7,.95,1.3,'rock.slab'],[-1.2,0,1,'rock.boulder'],[.15,-1,.8,'rock.rubble']]},
  alcove:{rx:.95,rz:1.1,shift:-.18,skew:.1,depth:.5,rise:2.2,rocks:[[-1.1,-.4,2.1,'rock.ledge'],[-.95,.75,1.6,'rock.block'],[-.65,0,1.2,'rock.boulder'],[.05,-1,.7,'rock.rubble']]},
  fissure:{rx:1.45,rz:.4,shift:-.25,skew:.22,depth:.42,rise:1.3,rocks:[[-.8,-.65,1.8,'rock.slab'],[-.9,.65,1.9,'rock.block'],[-1.5,0,1.5,'rock.ledge'],[.3,.65,.7,'rock.rubble']]},
  apron:{rx:.8,rz:1.35,shift:-.12,skew:-.12,depth:.4,rise:.9,rocks:[[-1.1,-.8,1.6,'rock.ledge'],[-.9,.75,1.3,'rock.boulder'],[-.1,-1.2,.8,'rock.rubble'],[.15,1.15,.9,'rock.slab']]},
  snowbed:{rx:1.5,rz:.55,shift:-.2,skew:.1,depth:.25,rise:.1,rocks:[]},
};
const FLAT=['bowl','oval','twin'],SLOPE=['alcove','fissure','apron'];
function springHash(source,seed){let h=Math.imul(Math.round(source.x*8),374761393)^Math.imul(Math.round(source.z*8),668265263)^seed;h=Math.imul(h^h>>>13,1274126177);return (h^h>>>16)>>>0;}
export function configureSpring(source,points,sample,seed=0){
  const a=points[0],b=points[1],length=Math.hypot(b.x-a.x,b.z-a.z)||1;
  source.directionX=(b.x-a.x)/length;source.directionZ=(b.z-a.z)/length;
  source.surfaceLevel=points[0].level??sample(source.x,source.z).height-.12;
  const behind=sample(source.x-source.directionX*4,source.z-source.directionZ*4),ahead=sample(source.x+source.directionX*4,source.z+source.directionZ*4);
  const slope=behind&&ahead?Math.abs(behind.height-ahead.height)/8:0;
  source.springStyle=source.sourceKind==='snowmelt'?'snowmelt':slope>.07?'hillside':'artesian';
  const hash=springHash(source,seed);
  source.springTemplate=source.springStyle==='snowmelt'?'snowbed':(slope>.07?SLOPE:FLAT)[hash%3];
  source.springMirror=hash&8?-1:1;
  source.springRadius=Math.max(source.springStyle==='snowmelt'?1.5:3,Math.min(12,(a.halfWidth??source.halfWidth)*.5+2))*(.9+(hash%101)/500);
}
export function applySpringSurface(surface,point,network,natural){
  if(surface.kind===1||surface.kind===2||surface.river?.source==='macro')return surface;
  for(const source of network.sources){
    if(!source.connected||!source.springStyle)continue;
    const dx=point.x-source.x,dz=point.z-source.z,along=dx*source.directionX+dz*source.directionZ,across=-dx*source.directionZ+dz*source.directionX;
    const form=FORMS[source.springTemplate]??FORMS.bowl,u=along/source.springRadius-form.shift,v=across/source.springRadius*(source.springMirror??1),x=u-form.skew*v;
    const radius=form.lobe?Math.min(Math.hypot(x/form.rx,(v-form.lobe)/form.rz),Math.hypot(x/form.rx,(v+form.lobe)/form.rz)):Math.hypot(x/form.rx,v/form.rz);
    if(radius>=1.5)continue;
    surface.springDamp=Math.max(surface.springDamp??0,1-smooth(radius/1.5));
    const rise=form.rise*smooth((radius-.65)/.4)*(1-smooth((radius-1)/.5))*smooth((.2-u)/.7);
    if(radius>=1){if(!(surface.water>surface.height))surface.height+=rise*smooth((surface.height-source.surfaceLevel)/.25);continue;}
    const up=source.springStyle==='artesian'?.045*Math.max(0,1-radius*4):0;
    const level=Math.min(source.surfaceLevel+up,natural-.08,surface.river&&surface.river.id===source.id?surface.water:Infinity),blend=1-smooth(radius)**2;
    surface.height+=(level-form.depth-surface.height)*blend;
    if(surface.height<level){surface.water=Math.max(surface.water,level);surface.kind=surface.river?3:4;surface.salinity='fresh';surface.saltLakeId=null;}
    else if(!(surface.water>surface.height))surface.height+=rise*smooth((surface.height-source.surfaceLevel)/.25);
  }
  return surface;
}
export function springRockPattern(source){
  if(!source.connected||!source.springStyle||source.springStyle==='snowmelt')return [];
  const r=source.springRadius,angle=Math.atan2(source.directionZ,source.directionX),sites=[];
  const hillside=source.springStyle==='hillside',pattern=(FORMS[source.springTemplate]??FORMS.bowl).rocks;
  for(let k=0;k<pattern.length;k++){
    const [along,side,scale,id]=pattern[k],across=side*(source.springMirror??1);
    sites.push({x:source.x+(along*source.directionX-across*source.directionZ)*r,z:source.z+(along*source.directionZ+across*source.directionX)*r,
      id,scale:scale*Math.max(.8,Math.min(1.8,r/4)),yaw:angle+k*1.73,form:hillside?'outcrop':'buried'});
  }
  return sites;
}
