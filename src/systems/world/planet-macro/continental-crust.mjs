const TAU=Math.PI*2;
const dot=(a,b)=>a[0]*b[0]+a[1]*b[1]+a[2]*b[2];
const clamp=(v,a=0,b=1)=>Math.max(a,Math.min(b,v));
const smooth=v=>{v=clamp(v);return v*v*(3-2*v);};
const sphere=(lon,y)=>{const r=Math.sqrt(Math.max(0,1-y*y));return [r*Math.cos(lon),y,r*Math.sin(lon)];};
const rotate=(p,a,t)=>{const c=Math.cos(t),s=Math.sin(t),d=dot(a,p);return [p[0]*c+(a[1]*p[2]-a[2]*p[1])*s+a[0]*d*(1-c),p[1]*c+(a[2]*p[0]-a[0]*p[2])*s+a[1]*d*(1-c),p[2]*c+(a[0]*p[1]-a[1]*p[0])*s+a[2]*d*(1-c)];};
const PROFILES={balanced:{count:5,basin:1},archipelago:{count:10,basin:1.2},mountains:{count:4,basin:.8},rift:{count:6,basin:1.15}};
export function createContinentalCrust(g,o,plates,noise,rng,angle=o.activity/9*.38){
  const profile=PROFILES[o.recipe],cores=[],count=Math.min(profile.count,o.plateCount);
  const sea=1-o.landRatio,halfWidth=(.04*sea+.05*sea*sea)*profile.basin;
  for(let k=0;k<count;k++){
    let center,best=-Infinity;
    for(let n=0;n<32;n++){
      const p=sphere(rng()*TAU,rng()*2-1),distance=cores.length?1-Math.max(...cores.map(c=>dot(p,c.start))):1;
      if(distance>best){best=distance;center=p;}
    }
    let owner=0,nearest=-Infinity;
    for(const plate of plates){const d=dot(center,plate.start);if(d>nearest){nearest=d;owner=plate.id;}}
    cores.push({id:k,start:center,owner,bias:(rng()-.5)*.035});
  }
  const coreX=new Float64Array(count),coreY=new Float64Array(count),coreZ=new Float64Array(count),coreBias=new Float64Array(count),chords=new Float64Array(count*count);
  for(let k=0;k<count;k++){
    const c=cores[k],s=c.start;coreX[k]=s[0];coreY[k]=s[1];coreZ[k]=s[2];coreBias[k]=c.bias;
    for(let j=0;j<k;j++){const d=Math.hypot(s[0]-coreX[j],s[1]-coreY[j],s[2]-coreZ[j]);chords[k*count+j]=d;chords[j*count+k]=d;}
  }
  const maxSin=halfWidth>0?Math.sin(Math.min(Math.PI/2,halfWidth*1.2)):0,sBasin=o.seed^0x62617369,sCrus=o.seed^0x63727573,sCoas=o.seed^0x636f6173;
  const fieldXYZ=(px,py,pz)=>{
    let best=-Infinity,second=-Infinity,a=0,b=0;
    for(let k=0;k<count;k++){
      const value=px*coreX[k]+py*coreY[k]+pz*coreZ[k]+coreBias[k];
      if(value>best){second=best;b=a;best=value;a=k;}else if(value>second){second=value;b=k;}
    }
    const diff=best-second,chord=chords[a*count+b];let basin=0;
    if(halfWidth>0&&diff<chord*maxSin){
      const distance=Math.asin(clamp(diff/chord)),width=halfWidth*(1+.2*noise(px*3,py*3,pz*3,sBasin));
      basin=width>0?1-smooth(distance/width):0;
    }
    const coast=noise(px*4.1,py*4.1,pz*4.1,sCrus)*.025+noise(px*11,py*11,pz*11,sCoas)*.008;
    return {score:best+coast-basin*.16,basin,core:a};
  };
  const field=p=>fieldXYZ(p[0],p[1],p[2]);
  const samples=new Float64Array(2048),golden=Math.PI*(3-Math.sqrt(5)),phase=rng()*TAU;
  for(let k=0;k<samples.length;k++)samples[k]=field(sphere(k*golden+phase,1-2*(k+.5)/samples.length)).score;
  samples.sort();
  const reference=o.landRatio===0?2:o.landRatio===1?-2:samples[Math.min(samples.length-1,Math.floor(samples.length*(1-o.landRatio)))];
  const sample=(p,plate,fraction=1)=>field(rotate(p,plate.axis,-angle*plate.speed*fraction));
  const continentalXYZ=(px,py,pz,plate,fraction=1)=>{
    const t=-angle*plate.speed*fraction,c=Math.cos(t),s=Math.sin(t),ax=plate.axis[0],ay=plate.axis[1],az=plate.axis[2],d=ax*px+ay*py+az*pz,k=1-c;
    const rx=px*c+(ay*pz-az*py)*s+ax*d*k,ry=py*c+(az*px-ax*pz)*s+ay*d*k,rz=pz*c+(ax*py-ay*px)*s+az*d*k;
    return smooth((fieldXYZ(rx,ry,rz).score-reference)/.045+.5);
  };
  const continental=(p,plate,fraction)=>smooth((sample(p,plate,fraction).score-reference)/.045+.5);
  const continents=cores.map(c=>({...c,center:rotate(c.start,plates[c.owner].axis,angle*plates[c.owner].speed)}));
  return {sample,fieldXYZ,continental,continentalXYZ,reference,continents,halfWidthKm:halfWidth*g.radius/1000};
}
