import {sampleSurface} from './backend.mjs';
import {cellAt} from './sphere-frame.mjs';
import {getTributaries,applyTributaries} from './source-rivers.mjs';
import {finishCover} from './surface-cover.mjs';

export function samplePlanetSurface(world,wx,wz,details=true,channels=false){
  const surface=sampleSurface(world,wx,wz,details,false,true,channels);
  return details?finishCover(applyTributaries(world,wx,wz,surface,getTributaries(world,cellAt(world,wx,wz)))):surface;
}
