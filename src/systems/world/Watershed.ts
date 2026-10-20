import type { MacroWorld } from './planet-macro/backend.mjs';

/** 宏观水文快照承载全球骨架，详见同名文档 */
export interface WatershedState {
  planetMacro?: MacroWorld;
}

/** 构造承载全球骨架的快照 */
export function macroHydrologyState(world: MacroWorld): WatershedState {
  return { planetMacro: world };
}
