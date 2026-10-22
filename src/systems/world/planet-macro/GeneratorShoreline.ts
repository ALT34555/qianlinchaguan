import type {WorldGeneratorBase} from '../WorldContract';
import {PlanetMacroGenerator} from './PlanetMacroGenerator';
import {createPlanetShorelineSource,type ShorelineSource} from './shoreline-query.mjs';

export function generatorShorelineSource(generator:WorldGeneratorBase,worldRevision:string,hydrologyRevision:string):ShorelineSource {
  if(!(generator instanceof PlanetMacroGenerator))throw Error('Shoreline requires planet-macro');
  const source=createPlanetShorelineSource(generator.macroWorld,{worldRevision,hydrologyRevision});
  source.column=(x,z)=>{
    const value=generator.getSurfaceColumn(x,z,false),state=value.state;
    return {bed:Math.fround(state.bedLevel),water:Math.fround(state.level),
      iceLevel:Math.fround(state.iceLevel),iceBottom:Math.fround(state.iceBottomLevel),iceCoverage:state.iceCoverage,
      temperature:state.temperature,color:value.color,chunkCode:value.chunkCode,
      waterBodyId:value.waterBodyId,waterBodyKind:value.waterBodyKind as 'sea'|'lake'|'river'|'unknown'};
  };
  return source;
}
