import type { PlayMode } from '../core/PlayMode';
import { DEFAULT_GENERATION, type ClimateWeights, type WorldGeneration } from '../systems/world/WorldSettings';
import { DEFAULT_PLAYER_PROFILE, type PlayerProfile } from '../entities/PlayerProfile';
import { CreatorSession } from './world-creator/session';
import type { StartOptions } from './world-creator/options';

export { parseSeed } from './world-creator/options';
export type { StartOptions } from './world-creator/options';

/** 创建页入口，流程在world-creator */
export class WorldCreator {
  private readonly session: CreatorSession;

  constructor(
    root: HTMLElement,
    seed: string,
    weights: ClimateWeights,
    enter: (options: StartOptions) => void,
    generation: WorldGeneration = DEFAULT_GENERATION,
    back: () => void = () => {},
    defaultRenName = '赤',
    playMode: PlayMode = 'leisure',
    defaultProfile: PlayerProfile = { ...DEFAULT_PLAYER_PROFILE },
  ) {
    void weights;
    this.session = new CreatorSession({ root, seed, enter, generation, back, defaultRenName, playMode, defaultProfile });
  }

  destroy(): void {
    this.session.destroy();
  }
}
