export const SEASONS = ['spring', 'summer', 'autumn', 'winter'] as const;
export type Season = typeof SEASONS[number];
