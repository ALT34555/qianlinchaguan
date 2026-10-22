import {installTileWorker} from './tile-worker-runtime.mjs';
import {createWorldGenerator} from '../../systems/world/GeneratorFactory';
import {generatorShorelineSource} from '../../systems/world/planet-macro/GeneratorShoreline';
import {createPlanetShorelineSource} from '../../systems/world/planet-macro/shoreline-query.mjs';

const receive=installTileWorker(self,(message:any)=>{
  if(!message.generatorOptions)return {source:createPlanetShorelineSource(message.world),update() {}};
  const options=message.generatorOptions;
  const generator=createWorldGenerator(options.seed,options.climateWeights,options.generation,{state:{planetMacro:message.world}});
  const state={source:generatorShorelineSource(generator,message.world.fingerprint,message.waterKey??''),update(water:any){
    generator.replaceWaterAppearanceOverrides(water.overrides??[]);generator.setWaterFreezeRules(water.freezeRules??{});generator.setWaterSeason(water.season??null);
    state.source=generatorShorelineSource(generator,message.world.fingerprint,JSON.stringify(water));
  }};
  state.update(message.water??{});return state;
});
self.onmessage=({data})=>receive(data);
