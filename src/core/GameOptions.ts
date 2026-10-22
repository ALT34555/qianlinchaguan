import type { PlayMode } from './PlayMode';
import type { GameSettings } from './GameSettings';
import type { PlayerProfile } from '../entities/PlayerProfile';
import type { PlayerFactionOptions } from '../systems/world/artificial/PlayerFaction';
import type { ClimateWeights, WorldGeneration } from '../systems/world/WorldSettings';
import type { PlayerPosition, WorldSave } from '../systems/world/WorldSave';
import type { SaveStore } from '../systems/world/LocalSaveStore';
import type { PreparedWorld } from '../systems/world/PrepareWorld';

export interface GameOptions {
  worldKey?:string;
  prepared?:PreparedWorld;
  playMode?: PlayMode;
  creationTimeSpeed?: number;
  ren_ming?: string;
  profile?: PlayerProfile;
  playerFaction?: PlayerFactionOptions;
  seed: number;
  climateWeights: ClimateWeights;
  generation: WorldGeneration;
  calendarType: 'real' | 'yuan';
  unixMs?: number;
  utcOffsetMinutes?: number;
  worldName?: string;
  saveId?: string;
  save?: WorldSave;
  initialPlayer?: PlayerPosition;
  showOverlay: boolean;
  settings: GameSettings;
  saveStore: SaveStore;
  persistSettings: (settings: GameSettings) => void;
}
