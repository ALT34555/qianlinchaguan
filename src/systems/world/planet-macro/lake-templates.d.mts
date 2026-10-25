import type {MacroWorld} from './backend.mjs';
export interface LakeTemplateSample {height:number;water:number;kind:number;field:number;patch:number;id:number;salinity:'fresh'|'salt';saltLakeId:number|null;lake:{id:number;level:number;depth:number;salinity:'fresh'|'salt';cells:number[]}}
export function sampleTemplateLake(world:MacroWorld,x:number,z:number,natural?:number):LakeTemplateSample|null;
export function lakeTemplateCatalog(world:MacroWorld):{lakes:Map<number,LakeTemplateSample['lake']>;tiles:Map<number,unknown>;queries:number};
export function templateCell(world:MacroWorld,x:number,y:number):number;
export interface LakeTemplateForm {bits:number[];center:boolean;variant:number;cornerVariant?:number;centerVariant?:number;shoreFeatures?:readonly (readonly number[])[];phase:number}
export const LAKE_SHORE_TEMPLATES:readonly Readonly<{name:string;bend:number;shoal:number;curve?:readonly number[];features:readonly (readonly number[])[]}>[];
export const LAKE_CORNER_TEMPLATES:typeof LAKE_SHORE_TEMPLATES;
export const LAKE_CENTER_TEMPLATES:readonly Readonly<{name:string;depth:number;ribbon?:number;islands:readonly (readonly number[])[];cuts:readonly (readonly number[])[];basins:readonly (readonly number[])[]}>[];
export function lakeIslandChance(activity?:number):number;
export function lakeIslandEnabled(seed:number,activity?:number):boolean;
export function lakeCenterVariant(seed:number,islands:boolean):number;
export function lakeShoreFeatures(variant:number,seed:number):readonly (readonly number[])[];
export function lakeTemplateTile(world:MacroWorld,cell:number):(LakeTemplateForm&{id:number})[];
export function templateField(form:LakeTemplateForm,x:number,z:number):number;
export function templateLakeBed(form:LakeTemplateForm,x:number,z:number,depth:number,field:number,patch:number):number;
