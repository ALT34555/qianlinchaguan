import {sampleSurface} from './backend.mjs';
import {cellAt} from './sphere-frame.mjs';
import {getTributaries,getMainRiverNetwork,applyTributaries} from './source-rivers.mjs';
import {finishCover} from './surface-cover.mjs';

export function samplePlanetSurface(world,wx,wz,details=true,channels=false,cover=details,includeDecorative=true,chunkLabels=cover){
  const surface=sampleSurface(world,wx,wz,details,false,true,channels,cover,includeDecorative&&details);
  if(!details)return surface;
  const out=applyTributaries(world,wx,wz,surface,(includeDecorative?getTributaries:getMainRiverNetwork)(world,cellAt(world,wx,wz)),chunkLabels,includeDecorative);
  return cover?finishCover(out):out;
}
