export const MACRO_TERRAIN_PROFILES = ['balanced','archipelago','mountains','rift'] as const;
export type MacroTerrainProfile = typeof MACRO_TERRAIN_PROFILES[number];
export const MACRO_CLIMATE_PROFILES = ['wind-field'] as const;
export type MacroClimateProfile = typeof MACRO_CLIMATE_PROFILES[number];
export const MACRO_GRID_SIZES = [256,512,1024] as const;
export const MACRO_CELL_CHUNKS = [64,100,160,250,360,480,625] as const;
export function macroSeasonStrength(_profile?:MacroClimateProfile):number {return 1;}
