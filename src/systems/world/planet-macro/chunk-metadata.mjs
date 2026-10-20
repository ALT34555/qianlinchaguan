import definitions from '../../../../content/data/world/macro_chunk_types.json' with {type:'json'};
import {macroAt} from './backend.mjs';
import {landMaterial} from './surface-cover.mjs';
const types=new Map(definitions.map(def=>[def.id,{...def}]));
export function chunkMetadata(id){const type=types.get(id);if(!type)throw Error('未知实验室区块编号：'+id);return type;}

// 宏观近似和区块共用赋值规则
export function landTypeId(temperature,rainfall,evaporation,relief,slope,relative){
  const landClass=temperature< -8?1:temperature<8?2:temperature<18?3:temperature<24?4:5;
  const terrain=slope>=.7?4:relief<4?1:relative< -.5?2:3;
  const material=landMaterial(temperature,rainfall/Math.max(150,evaporation),relief,slope,relative);
  return landClass*100+material*10+terrain;
}

// 复用分类九点，不另采地形
export function landChunkMetadata(world,samples,wx,wz){
  const center=samples[4],temperature=center.temperature;
  let low=Infinity,high=-Infinity,total=0,slope=0;
  for(const sample of samples){low=Math.min(low,sample.height);high=Math.max(high,sample.height);total+=sample.height;}
  for(let z=0;z<3;z++)for(let x=0;x<3;x++){
    const k=z*3+x;if(x<2)slope=Math.max(slope,Math.abs(samples[k+1].height-samples[k].height)/32);
    if(z<2)slope=Math.max(slope,Math.abs(samples[k+3].height-samples[k].height)/32);
  }
  const relief=high-low,relative=center.height-(total-center.height)/8;
  const rainfall=macroAt(world,wx,wz,'rainfall'),evaporation=macroAt(world,wx,wz,'evaporation'),aridity=rainfall/Math.max(150,evaporation);
  const terrain=slope>=.7?4:relief<4?1:relative< -.5?2:3,landClass=Math.floor(landTypeId(temperature,rainfall,evaporation,relief,slope,relative)/100);
  const material=landMaterial(temperature,center.localAridity??aridity,relief,slope,relative,center.exposure??.28);
  return {...chunkMetadata(landClass*100+material*10+terrain),temperature,rainfall,aridity,relief,slope,classificationBasis:'nine-point terrain; continuous ground cover and annual climate'};
}
