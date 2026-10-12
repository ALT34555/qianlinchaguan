import {DEFAULT_WATER_FREEZE_RULES, temperatureWaterFreezing, waterIceAmount, waterFreezeFlowScale, waterIceThickness, type WaterFreezeRules, type WaterFreezeState} from './WaterFreezing';

export const WATER_FORMS = ['normal', 'low', 'stopped', 'flood'] as const;
export type WaterForm = 0 | 1 | 2 | 3;
export type WaterRGB = readonly [number, number, number];
export interface WaterPalette { shallow: WaterRGB; deep: WaterRGB; ice: WaterRGB; foam: WaterRGB }
export interface WaterAppearanceOverride { form?: WaterForm; colors?: Partial<WaterPalette>; freezing?: WaterFreezeState; temperature?: number }
export type WaterAppearanceUpdate = { [K in keyof WaterAppearanceOverride]?: WaterAppearanceOverride[K] | null };
export interface ChunkWaterState {
  form: WaterForm;
  temperature: number;
  frozen: boolean;
  freezing: WaterFreezeState;
  iceAmount: number;
  iceThickness: number;
  flowScale: number;
  colors: WaterPalette;
}
export type ChunkWaterAppearance = ChunkWaterState;
export interface WaterAppearanceEntry { cx: number; cz: number; appearance: WaterAppearanceOverride }
export const DEFAULT_WATER_PALETTE: WaterPalette = {
  shallow: [96, 166, 158], deep: [28, 70, 104], ice: [188, 220, 232], foam: [247, 255, 255],
};
export function validateWaterAppearance(value: WaterAppearanceOverride): WaterAppearanceOverride {
  if (value.form !== undefined && ![0, 1, 2, 3].includes(value.form)) throw new Error('水体形态须为 0~3');
  if (value.freezing !== undefined && ![0, 1, 2].includes(value.freezing)) throw new Error('冻结状态须为 0~2');
  if (value.temperature !== undefined && !Number.isFinite(value.temperature)) throw new Error('水域温度须为有限数值');
  const colors: Partial<WaterPalette> = {};
  for (const key of ['shallow', 'deep', 'ice', 'foam'] as const) {
    const rgb = value.colors?.[key];
    if (!rgb) continue;
    if (rgb.length !== 3 || rgb.some(v => !Number.isFinite(v) || v < 0 || v > 255)) throw new Error('水体颜色须为 0~255 RGB');
    colors[key] = [...rgb];
  }
  return { ...(value.form === undefined ? {} : { form: value.form }),
    ...(value.freezing === undefined ? {} : {freezing: value.freezing}),
    ...(value.temperature === undefined ? {} : {temperature: value.temperature}), colors };
}
export function resolveWaterAppearance(temperature: number, override?: WaterAppearanceOverride, rules: Readonly<WaterFreezeRules> = DEFAULT_WATER_FREEZE_RULES): ChunkWaterState {
  const form = override?.form ?? 0;
  temperature = override?.temperature ?? temperature;
  const freezing = override?.freezing ?? temperatureWaterFreezing(temperature, rules);
  return { form, temperature, freezing, frozen: freezing === 2,
    iceAmount: waterIceAmount(freezing, rules), iceThickness: freezing ? waterIceThickness(temperature, rules) : 0, flowScale: [1, .4, 0, 1.6][form] * waterFreezeFlowScale(freezing, rules),
    colors: { ...DEFAULT_WATER_PALETTE, ...override?.colors } };
}
