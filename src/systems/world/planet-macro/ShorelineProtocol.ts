import type {ClimateWeights,WorldGeneration} from '../WorldSettings';
import type {WatershedState} from '../Watershed';
import type {WaterAppearanceEntry} from '../WaterAppearance';
import type {WaterFreezeRules} from '../WaterFreezing';
import type {ShorelineRequest,ShorelineResult} from './shoreline-query.mjs';

export type ShorelineWorkerRequest=
  | {kind:'init';worldRevision:string;seed:number;climateWeights:ClimateWeights;generation:WorldGeneration;hydrology:WatershedState}
  | {kind:'cancel';id:number}
  | {kind:'query';id:number;hydrologySequence:number;request:Omit<ShorelineRequest,'cancelled'>;waterOverrides:WaterAppearanceEntry[];waterFreezeRules:WaterFreezeRules;waterSeason:number|null};
export type ShorelineWorkerResponse=
  | {kind:'shoreline';id:number;result:ShorelineResult}
  | {kind:'error';id:number;name:string;message:string};
