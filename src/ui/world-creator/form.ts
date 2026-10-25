import { CHUNK_SIZE } from '../../core/config';
import { parseCreationTimeSpeed, type PlayMode } from '../../core/PlayMode';
import { randomSeed } from '../../core/math/Random';
import { renName } from '../../entities/PlayerIdentity';
import { playerProfile, type PlayerProfile } from '../../entities/PlayerProfile';
import { hexColor, playerFactionOptions, type PlayerFactionOptions } from '../../systems/world/artificial/PlayerFaction';
import { DEFAULT_ARTIFICIAL } from '../../systems/world/artificial/ArtificialSettings';
import { DEFAULT_YUAN_DATE, dateInputValue, startDateUnixMs, todayDate } from '../../systems/calendar/WorldDate';
import { DEFAULT_GENERATION, normalizeClimateWeights, normalizeGeneration, type WorldGeneration } from '../../systems/world/WorldSettings';
import { MACRO_CELL_CHUNKS, MACRO_GRID_SIZES } from '../../systems/world/planet-macro/Settings';
import { worldPreset } from '../WorldPresets';
import { t } from '../../i18n';
import type { FieldAccess } from './fields';
import { parseSeed, type StartOptions } from './options';

export interface CreatorFormConfig {
  playMode: PlayMode;
  defaultRenName: string;
  defaultProfile: PlayerProfile;
}

/** 表单唯一源：从DOM读取归一化 */
export class CreatorForm {
  private spawnClearedByLock = false;

  constructor(
    private readonly fields: FieldAccess,
    private readonly config: CreatorFormConfig,
  ) {}

  initialize(seed: string): void {
    this.fields.input('world-seed').value = seed;
    this.fields.input('start-real-date').value = dateInputValue(todayDate());
    this.fields.input('start-yuan-year').value = String(DEFAULT_YUAN_DATE.year);
    this.fields.input('start-yuan-month').value = String(DEFAULT_YUAN_DATE.month);
    this.fields.input('start-yuan-day').value = String(DEFAULT_YUAN_DATE.day);
    // 星球气候由温度与纬度决定
    this.refresh();
  }

  randomizeSeed(): void {
    this.fields.input('world-seed').value = String(crypto.getRandomValues(new Uint32Array(1))[0]);
  }

  settings(): StartOptions {
    const calSelect = this.fields.el<HTMLSelectElement>('calendar-mode');
    if (!this.fields.input('world-seed').value.trim()) this.fields.input('world-seed').value = String(randomSeed());
    const generation = this.generationSettings();
    const playerFaction = this.playerSettings();
    if (!generation.artificial?.enabled) playerFaction.spawnAsFlagLand = false;
    const name = renName(this.fields.input('ren-ming').value, renName(this.config.defaultRenName));
    return {
      playMode: this.config.playMode,
      creationTimeSpeed: parseCreationTimeSpeed(this.fields.input('creation-time-speed').value.trim() ? Number(this.fields.input('creation-time-speed').value) : NaN),
      ren_ming: name,
      profile: this.profileSettings(name, playerFaction),
      playerFaction,
      seed: parseSeed(this.fields.input('world-seed').value),
      climateWeights: normalizeClimateWeights([20, 20, 20, 20, 20]),
      generation,
      calendarType: (calSelect ? calSelect.value : 'real') as 'real' | 'yuan',
      unixMs: this.startUnixMs(),
      utcOffsetMinutes: -new Date().getTimezoneOffset(),
      worldName: this.fields.input('world-name').value.trim() || t('creator.unnamedWorld'),
    };
  }

  generationSettings(): WorldGeneration {
    const number = (id: string) => this.fields.input(id).value.trim() ? Number(this.fields.input(id).value) : NaN;
    const artificial = { enabled: this.fields.input('artificial-enabled').checked, initialCivilizationLimit: number('initial-civilization-limit'), expansion: number('natural-expansion'),
      evolution: number('natural-evolution'), independence: this.fields.input('flag-independence').checked };
    return normalizeGeneration({ ...DEFAULT_GENERATION, artificial,
      landRatio: number('land-ratio'), precipitation: number('precipitation'),
      terrainRule: 'planet-macro', macroTerrain: this.fields.el<HTMLSelectElement>('macro-terrain').value,
      macroColumns: MACRO_GRID_SIZES[number('macro-size')], macroClimate: this.fields.el<HTMLSelectElement>('macro-climate').value,
      planet: { map: 'procedural', poleTemperature: number('pole-temperature'),
        equatorTemperature: number('equator-temperature'), monsoonDirection: number('monsoon-direction'), equatorChunks: number('equator-chunks'),
        tectonicActivity: number('tectonic-activity') } });
  }

  setGenerationForm(generation: WorldGeneration): void {
    this.fields.el<HTMLSelectElement>('macro-terrain').value = generation.macroTerrain;
    this.fields.el<HTMLSelectElement>('macro-climate').value = generation.macroClimate;
    const columns = generation.macroColumns;
    const nearest = (values: readonly number[], value: number) => values.reduce((best, v, i) => Math.abs(Math.log(v / value)) < Math.abs(Math.log(values[best] / value)) ? i : best, 0);
    this.fields.input('macro-size').value = String(nearest(MACRO_GRID_SIZES, columns));
    this.fields.input('macro-scale').value = String(nearest(MACRO_CELL_CHUNKS, generation.planet.equatorChunks / columns));
    this.fields.input('pole-temperature').value = String(generation.planet.poleTemperature);
    this.fields.input('equator-temperature').value = String(generation.planet.equatorTemperature);
    this.fields.input('monsoon-direction').value = String(generation.planet.monsoonDirection);
    this.fields.input('equator-chunks').value = String(generation.planet.equatorChunks);
    this.fields.input('tectonic-activity').value = String(generation.planet.tectonicActivity ?? 5);
    this.fields.input('land-ratio').value = String(generation.landRatio);
    this.fields.input('precipitation').value = String(generation.precipitation);
    const artificial = generation.artificial ?? DEFAULT_ARTIFICIAL;
    this.fields.input('artificial-enabled').checked = artificial.enabled;
    this.fields.input('initial-civilization-limit').value = String(artificial.initialCivilizationLimit ?? DEFAULT_ARTIFICIAL.initialCivilizationLimit);
    this.fields.input('natural-expansion').value = String(artificial.expansion);
    this.fields.input('natural-evolution').max = String(artificial.expansion);
    this.fields.input('natural-evolution').value = String(artificial.evolution);
    this.fields.input('flag-independence').checked = artificial.independence;
    this.syncArtificialFields();
    this.syncGenerationFields();
  }

  syncGenerationFields(): void {
    // 宏观星球为唯一生成规则，字段可见性固定
    this.fields.el('planet-view-tools').classList.remove('hidden');
    this.fields.el('lightweight-note').classList.add('hidden');
    this.fields.el('earth-precision-section')?.classList.add('hidden');

    this.fields.input('monsoon-direction').closest('label')!.classList.add('hidden');
    this.fields.input('equator-chunks').closest('label')!.classList.add('hidden');
    this.fields.input('precipitation').closest('label')!.querySelector('small')!.textContent = t('creator.macroRain.hint');
    this.fields.input('precipitation').max = '2'; this.fields.input('precipitation').step = '.25';
    this.fields.el<HTMLSelectElement>('macro-climate').disabled = true;
    this.syncMacroDimensions();
    this.fields.input('equator-chunks').max = '640000';
    this.fields.input('monsoon-direction').disabled = true;
    this.fields.input('tectonic-activity').disabled = false;
    this.fields.input('land-ratio').disabled = false;
    this.fields.input('pole-temperature').disabled = false;
    this.fields.input('equator-chunks').step = '4';
  }

  syncMacroDimensions(): void {
    const columns = MACRO_GRID_SIZES[this.fields.input('macro-size').valueAsNumber];
    const chunks = MACRO_CELL_CHUNKS[this.fields.input('macro-scale').valueAsNumber];
    const meters = chunks * CHUNK_SIZE, circumference = columns * meters;
    const distance = (m: number) => m >= 1000 ? `${Number((m / 1000).toFixed(3))} km` : `${m} m`;
    this.fields.input('equator-chunks').value = String(columns * chunks);
    this.fields.el('macro-size-value').textContent = `${t(`creator.macroSize.${columns}`)} · ${columns} × ${columns / 2}`;
    this.fields.el('macro-scale-value').textContent = t('creator.macroScale.value', { distance: distance(meters), chunks });
    this.fields.el('macro-dimensions').textContent = t('creator.macroDimensions', { distance: distance(circumference) });
    this.fields.input('macro-size').setAttribute('aria-valuetext', this.fields.el('macro-size-value').textContent!);
    this.fields.input('macro-scale').setAttribute('aria-valuetext', this.fields.el('macro-scale-value').textContent!);
  }

  syncArtificialFields(): void {
    const enabled = this.fields.input('artificial-enabled').checked;
    const expansion = this.fields.input('natural-expansion').valueAsNumber;
    const evolution = this.fields.input('natural-evolution');
    const speed = Math.min(evolution.valueAsNumber, expansion);
    evolution.max = String(expansion); evolution.value = String(speed);
    this.fields.el('artificial-speeds').style.setProperty('--distance-limit', String(expansion / 4));
    this.fields.el('artificial-options').classList.toggle('hidden', !enabled);
    this.fields.input('initial-civilization-limit').disabled = !enabled;
    this.fields.input('natural-expansion').disabled = !enabled;
    evolution.disabled = !enabled || expansion === 0;
    this.fields.el('natural-expansion-value').textContent = expansion === 0 ? t('creator.artificial.off') : `×${expansion}`;
    this.fields.el('natural-evolution-value').textContent = speed === 0 ? t('creator.artificial.off') : `×${speed}`;
    const independent = enabled && speed > 0;
    this.fields.el('independence-option').classList.toggle('hidden', !independent);
    this.fields.el('independence-hint').classList.toggle('hidden', !independent);
    this.fields.input('flag-independence').disabled = !independent;
    if (!independent) this.fields.input('flag-independence').checked = false;
    const spawn = this.fields.input('spawn-as-flag');
    spawn.disabled = !enabled;
    if (!enabled) {
      if (spawn.checked) this.spawnClearedByLock = true;
      spawn.checked = false;
    } else if (this.spawnClearedByLock) {
      spawn.checked = true;
      this.spawnClearedByLock = false;
    }
    const human = this.fields.el<HTMLDetailsElement>('fold-human');
    human.classList.toggle('locked', !enabled);
    if (!enabled) human.open = false;
  }

  syncSchemeChip(): void {
    try { this.fields.el('color-scheme-core').style.background = hexColor(this.fields.input('faction-main-color').value); } catch { }
    try { this.fields.el('color-scheme-chip').style.background = hexColor(this.fields.input('faction-trim-color').value); } catch { }
  }

  /** 刷新派生显示并校验；返回归一化错误，不写状态文案 */
  refresh(): unknown | null {
    this.syncArtificialFields();
    this.fields.el('tectonic-value').textContent = this.fields.input('tectonic-activity').value;
    const ratio = Number(this.fields.input('land-ratio').value);
    const precipitation = Number(this.fields.input('precipitation').value);
    const precipitationLabel = precipitation === 0 ? t('creator.precipitation.none') : precipitation === 1 ? t('creator.macroRain.baseline') : t('creator.macroRain.multiplier');
    this.fields.el('precipitation-value').textContent = `${Number(precipitation.toFixed(2))}${precipitationLabel ? ` · ${precipitationLabel}` : ''}`;
    this.fields.el('land-ratio-value').textContent = ratio === 0 ? t('creator.landRatio.water') : ratio === 1 ? t('creator.landRatio.land')
      : ratio === .5 ? t('creator.landRatio.balanced') : t('creator.landRatio.percent', { value: Math.round(ratio * 100) });
    try {
      this.generationSettings();
      parseCreationTimeSpeed(this.fields.input('creation-time-speed').value.trim() ? Number(this.fields.input('creation-time-speed').value) : NaN);
      const name = renName(this.fields.input('ren-ming').value, renName(this.config.defaultRenName));
      this.profileSettings(name, this.playerSettings());
      return null;
    } catch (error) {
      return error;
    }
  }

  applyPreset(preset?: string): string | undefined {
    if (!preset) return;
    const target = worldPreset(preset, this.generationSettings());
    if (!target) return;
    this.setGenerationForm(target.generation);
    // 平面气候权重已移除；星球预设只改生成参数
    return preset;
  }

  startUnixMs(): number {
    const mode = this.fields.el<HTMLSelectElement>('calendar-mode').value as 'real' | 'yuan';
    const parts = mode === 'real' ? this.fields.input('start-real-date').value.split('-').map(Number)
      : ['start-yuan-year', 'start-yuan-month', 'start-yuan-day'].map(id => this.fields.input(id).value.trim() ? Number(this.fields.input(id).value) : NaN);
    return startDateUnixMs(mode, { year: parts[0], month: parts[1], day: parts[2] }, -new Date().getTimezoneOffset());
  }

  private profileSettings(name: string, playerFaction: PlayerFactionOptions): PlayerProfile {
    return playerProfile({ ...this.config.defaultProfile, name, heightCm: this.fields.input('profile-height').valueAsNumber, mainColor: playerFaction.mainColor, trimColor: playerFaction.trimColor }, this.config.playMode);
  }

  private playerSettings(): PlayerFactionOptions {
    return playerFactionOptions({ spawnAsFlagLand: this.fields.input('spawn-as-flag').checked,
      name: this.fields.input('faction-name').value, mainColor: this.fields.input('faction-main-color').value,
      trimColor: this.fields.input('faction-trim-color').value });
  }
}
