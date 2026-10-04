/**
 * 世界常量（主线程与 Worker 共用，不得依赖 DOM）。
 */

/** 区块边长（方块），区块为 CHUNK_SIZE x CHUNK_SIZE 的柱状区域 */
export const CHUNK_SIZE = 64;

/** 海平面：水面位于 y = SEA_LEVEL 处 */
export const SEA_LEVEL = 0;

/** 世界高度下限 / 上限 */
export const WORLD_MIN_Y = -4096;
export const WORLD_MAX_Y = 4096;

/** 默认世界种子 */
export const DEFAULT_SEED = 20261004;

/** 默认渲染半径（区块） */
export const DEFAULT_RENDER_DISTANCE = 6;
