export const WATER_FREEZE_STATES = ['liquid', 'partial', 'frozen'] as const;
export type WaterFreezeState = 0 | 1 | 2;
export interface WaterFreezeRules {
  partialTemperature: number;
  fullTemperature: number;
  partialIceAmount: number;
  partialFlowScale: number;
  altitudeLapseRate: number;
  minimumIceThickness: number;
  iceThicknessPerDegree: number;
}
export const DEFAULT_WATER_FREEZE_RULES: Readonly<WaterFreezeRules> = Object.freeze({
  partialTemperature: 0, fullTemperature: -6, partialIceAmount: .55,
  partialFlowScale: .4, altitudeLapseRate: .0065, minimumIceThickness: .15, iceThicknessPerDegree: .12,
});
export function validateWaterFreezeRules(rules: WaterFreezeRules): WaterFreezeRules {
  if ([rules.partialTemperature, rules.fullTemperature, rules.partialIceAmount, rules.partialFlowScale, rules.altitudeLapseRate, rules.minimumIceThickness, rules.iceThicknessPerDegree].some(v => !Number.isFinite(v)) ||
      rules.fullTemperature >= rules.partialTemperature || rules.partialIceAmount <= 0 || rules.partialIceAmount >= 1 ||
      rules.partialFlowScale < 0 || rules.partialFlowScale > 1 || rules.altitudeLapseRate < 0 || rules.minimumIceThickness <= 0 || rules.iceThicknessPerDegree < 0) throw new Error('水体冻结规则无效');
  return {...rules};
}
export function temperatureWaterFreezing(temperature: number, rules: Readonly<WaterFreezeRules> = DEFAULT_WATER_FREEZE_RULES): WaterFreezeState {
  return temperature <= rules.fullTemperature ? 2 : temperature <= rules.partialTemperature ? 1 : 0;
}
export function waterIceAmount(state: WaterFreezeState, rules: Readonly<WaterFreezeRules> = DEFAULT_WATER_FREEZE_RULES): number {
  return state === 2 ? 1 : state === 1 ? rules.partialIceAmount : 0;
}
export function waterFreezeFlowScale(state: WaterFreezeState, rules: Readonly<WaterFreezeRules> = DEFAULT_WATER_FREEZE_RULES): number {
  return state === 2 ? 0 : state === 1 ? rules.partialFlowScale : 1;
}

export function waterIceThickness(temperature: number, rules: Readonly<WaterFreezeRules> = DEFAULT_WATER_FREEZE_RULES): number {
  return rules.minimumIceThickness + Math.max(0, rules.partialTemperature - temperature) * rules.iceThicknessPerDegree;
}
