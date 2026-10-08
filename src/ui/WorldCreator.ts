import { icon, iconUrl } from './Icons';
import { renName } from '../entities/PlayerIdentity';
import { colorControl, bindColorControls } from './ColorControls';
import { ArtificialWorld } from '../systems/world/artificial/ArtificialWorld';
import { DEFAULT_PLAYER_FACTION, hexColor, playerFactionOptions, playerFlagSpawn, type PlayerFactionOptions } from '../systems/world/artificial/PlayerFaction';
import { hashString, randomSeed } from '../core/math/Random';
import { WorldGenerator, FLOW_DIRECTIONS } from '../systems/world/WorldGenerator';
import { getChunkTypeDef, formatChunkId, CHUNK_TYPES } from '../systems/world/ChunkTypes';
import { CLIMATES, DEFAULT_GENERATION, normalizeClimateWeights, normalizeGeneration, planetCoordinates, type ClimateWeights, type WorldGeneration } from '../systems/world/WorldSettings';
import type { WorldSave } from '../systems/world/WorldSave';
import { dateInputValue, todayDate, DEFAULT_YUAN_DATE, startDateUnixMs } from '../systems/calendar/WorldDate';
import { AtlasView, type ViewportStats } from './AtlasView';
import { chunkDescription, chunkName, climateName } from '../i18n/content';
import { t } from '../i18n';
import { DEFAULT_ARTIFICIAL } from '../systems/world/artificial/ArtificialSettings';
import type { TerritoryChunkInfo } from '../systems/world/artificial/ArtificialWorld';

function escapeHtml(value: string): string {
  return value.replace(/[&<>"']/g, c => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' })[c]!);
}

export function parseSeed(raw: string): number {
  const value = raw.trim();
  if (!value) return randomSeed();
  return /^-?\d+$/.test(value) ? Number(BigInt.asUintN(32, BigInt(value))) : hashString(value);
}

export interface StartOptions {
  ren_ming?: string;
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
}

export class WorldCreator {
  private stopColors: () => void = () => {};
  private generator: WorldGenerator | null = null;
  private atlasView: AtlasView | null = null;
  private mapShown = false;
  private isFullscreenMap = false;
  private spawnClearedByLock = false;

  constructor(
    private readonly root: HTMLElement,
    seed: string,
    weights: ClimateWeights,
    private readonly enter: (options: StartOptions) => void,
    generation: WorldGeneration = DEFAULT_GENERATION,
    private readonly back: () => void = () => {},
    private readonly defaultRenName = '赤',
  ) {
    this.renderLayout();
    this.input('ren-ming').value = renName(this.defaultRenName);
    this.stopColors = bindColorControls(this.root);
    this.updateSchemeChip();
    this.initFormValues(seed, weights);
    this.setGenerationForm(generation);
    this.initEventListeners();
  }

  private el<T extends HTMLElement = HTMLElement>(id: string): T {
    return this.root.querySelector(`#${id}`) as T;
  }

  private input(id: string): HTMLInputElement {
    return this.el<HTMLInputElement>(id);
  }

  private status(message: string): void {
    const el = this.el('menu-status');
    if (el) el.textContent = message;
  }

  private error(error: unknown): void {
    this.status(error instanceof Error ? error.message : String(error));
  }

  private weights(): ClimateWeights {
    return normalizeClimateWeights(
      CLIMATES.map((_, i) => {
        const input = this.input(`climate-${i}`);
        return input.value.trim() === '' ? NaN : Number(input.value);
      })
    );
  }

  private settings(): StartOptions {
    const calSelect = this.el<HTMLSelectElement>('calendar-mode');
    if (!this.input('world-seed').value.trim()) this.input('world-seed').value = String(randomSeed());
    const generation = this.generationSettings();
    const playerFaction = this.playerSettings();
    if (!generation.artificial?.enabled) playerFaction.spawnAsFlagLand = false;
    return {
      ren_ming: renName(this.input('ren-ming').value, renName(this.defaultRenName)),
      playerFaction,
      seed: parseSeed(this.input('world-seed').value),
      climateWeights: this.el<HTMLSelectElement>('generation-mode').value !== 'plane' ? normalizeClimateWeights([20, 20, 20, 20, 20]) : this.weights(),
      generation,
      calendarType: (calSelect ? calSelect.value : 'real') as 'real' | 'yuan',
      unixMs: this.startUnixMs(),
      utcOffsetMinutes: -new Date().getTimezoneOffset(),
      worldName: this.input('world-name').value.trim() || t('creator.unnamedWorld'),
    };
  }

  private playerSettings(): PlayerFactionOptions {
    return playerFactionOptions({ spawnAsFlagLand: this.input('spawn-as-flag').checked,
      name: this.input('faction-name').value, mainColor: this.input('faction-main-color').value,
      trimColor: this.input('faction-trim-color').value });
  }

  private generationSettings(): WorldGeneration {
    const number = (id: string) => this.input(id).value.trim() ? Number(this.input(id).value) : NaN;
    const landRatio = number('land-ratio');
    const precipitation = number('precipitation');
    const artificial = { enabled: this.input('artificial-enabled').checked, expansion: number('natural-expansion'),
      evolution: number('natural-evolution'), independence: this.input('flag-independence').checked };
    if (this.el<HTMLSelectElement>('generation-mode').value === 'plane') return normalizeGeneration({...DEFAULT_GENERATION, landRatio, precipitation, artificial});
    return normalizeGeneration({ mode: 'planet', landRatio, precipitation, artificial,
      planet: { map: this.el<HTMLSelectElement>('generation-mode').value === 'earth' ? 'earth' : 'procedural', poleTemperature: number('pole-temperature'),
        equatorTemperature: number('equator-temperature'), monsoonDirection: number('monsoon-direction'), equatorChunks: number('equator-chunks'),
        tectonicActivity: number('tectonic-activity') } });
  }

  private setGenerationForm(generation: WorldGeneration): void {
    this.el<HTMLSelectElement>('generation-mode').value = generation.mode === 'plane' ? 'plane' : generation.planet.map === 'earth' ? 'earth' : 'planet';
    this.input('pole-temperature').value = String(generation.planet.poleTemperature);
    this.input('equator-temperature').value = String(generation.planet.equatorTemperature);
    this.input('monsoon-direction').value = String(generation.planet.monsoonDirection);
    this.input('equator-chunks').value = String(generation.planet.equatorChunks);
    this.input('tectonic-activity').value = String(generation.planet.tectonicActivity ?? 5);
    this.input('land-ratio').value = String(generation.landRatio ?? .5);
    this.input('precipitation').value = String(generation.precipitation ?? .5);
    const artificial = generation.artificial ?? DEFAULT_ARTIFICIAL;
    this.input('artificial-enabled').checked = artificial.enabled;
    this.input('natural-expansion').value = String(artificial.expansion);
    this.input('natural-evolution').max = String(artificial.expansion);
    this.input('natural-evolution').value = String(artificial.evolution);
    this.input('flag-independence').checked = artificial.independence;
    this.updateArtificialFields();
    this.updateGenerationFields();
  }

  private updateGenerationFields(): void {
    const planet = this.el<HTMLSelectElement>('generation-mode').value !== 'plane';
    const earth = this.el<HTMLSelectElement>('generation-mode').value === 'earth';
    this.el('planet-settings').classList.toggle('hidden', !planet);
    this.el('plane-climates').classList.toggle('hidden', planet);
    this.el('planet-view-tools').classList.toggle('hidden', !planet);
    this.el('recommended-presets').classList.toggle('hidden', earth);
    this.input('tectonic-activity').disabled = earth;
    this.input('land-ratio').disabled = earth;
    if (earth) this.input('tectonic-activity').value = '5';
    this.updateWeights();
  }

  private renderLayout(): void {
    this.root.innerHTML = `
      <header class="menu-header">
        <a class="wordmark" href="./">${t('app.name')}</a>
        <div class="header-right">
          <button id="creator-back" class="menu-back">${t('creator.back')}</button>
        </div>
      </header>

      <div class="menu-layout" id="menu-layout-container">
        <section class="world-settings" id="world-settings-panel" aria-labelledby="menu-title">
          <h1 id="menu-title">${t('creator.title')}</h1>
          <p class="menu-intro">${t('creator.intro')}</p>

          <div id="recommended-presets" class="presets-section">
            <span class="field-label-mini">${t('creator.presets')}</span>
            <div class="presets-row">
              <button class="preset-pill" data-preset="balanced">${t('creator.preset.balanced')}</button>
              <button class="preset-pill" data-preset="islands">${t('creator.preset.islands')}</button>
              <button class="preset-pill" data-preset="mountains">${t('creator.preset.mountains')}</button>
              <button class="preset-pill" data-preset="arctic">${t('creator.preset.arctic')}</button>
            </div>
          </div>

          <label class="field-title" for="world-name">${t('creator.worldName')}</label>
          <div class="seed-field"><input id="world-name" maxlength="80" value="${t('creator.worldNamePlaceholder')}" autocomplete="off"></div>

          <label class="field-title" for="ren-ming">${t('player.worldName')}</label>
          <div class="seed-field"><input id="ren-ming" maxlength="40" autocomplete="off"></div>

          <label class="field-title" for="generation-mode">${t('creator.generationMode')}</label>
          <select id="generation-mode" class="filter-dropdown">
            <option value="plane">${t('creator.mode.plane')}</option>
            <option value="planet">${t('creator.mode.planet')}</option>
            <option value="earth">${t('creator.mode.earth')}</option>
          </select>
          <div id="planet-settings" class="planet-settings hidden">
            <div class="planet-fields">
              <label>${t('creator.poleTemp')} <small>${t('creator.poleTemp.hint')}</small><input id="pole-temperature" type="number" min="-100" max="100" step="1"></label>
              <label>${t('creator.equatorTemp')} <small>${t('creator.equatorTemp.hint')}</small><input id="equator-temperature" type="number" min="-100" max="100" step="1"></label>
              <label>${t('creator.monsoon')} <small>${t('creator.monsoon.hint')}</small><input id="monsoon-direction" type="number" min="0" max="359" step="1"></label>
              <label>${t('creator.equatorChunks')} <small>${t('creator.equatorChunks.hint')}</small><input id="equator-chunks" type="number" min="256" max="16384" step="4"></label>
              <label class="tectonic-setting"><span class="tectonic-heading">${t('creator.tectonicActivity')} <output id="tectonic-value" for="tectonic-activity">5</output></span><small>${t('creator.tectonicActivity.hint')}</small><input id="tectonic-activity" aria-label="${t('creator.tectonicActivity')}" type="range" min="0" max="9" step="1"></label>
            </div>
            <p class="setting-note">${t('creator.planetNote')}</p>
          </div>

          <label class="field-title" for="world-seed">
            ${t('creator.seed')} <small>${t('creator.seed.hint')}</small>
          </label>
          <div class="seed-field">
            <input id="world-seed" maxlength="128" autocomplete="off" placeholder="${t('creator.seed.placeholder')}">
            <button id="random-seed" title="${t('creator.seed.randomTitle')}" aria-label="${t('creator.seed.randomAria')}">↻</button>
          </div>

          <section class="artificial-settings">
            <label class="setting-row" for="artificial-enabled"><span>${t('creator.artificial')}</span><input id="artificial-enabled" type="checkbox"></label>
            <label class="setting-row" for="spawn-as-flag"><span>${t('player.flagSpawn')}</span><input id="spawn-as-flag" type="checkbox" checked></label>
            <p class="setting-note">${t('creator.artificial.hint')}</p>
          </section>

          <details class="fold-group" id="fold-geo">
            <summary><span class="fold-name">${t('creator.fold.geo')}</span><span class="fold-mark" aria-hidden="true"></span></summary>
            <div class="fold-body">
              <label class="coverage-setting tectonic-setting">
                <span class="tectonic-heading">${t('creator.landRatio')} <output id="land-ratio-value" for="land-ratio">${t('creator.landRatio.balanced')}</output></span>
                <small>${t('creator.landRatio.hint')}</small>
                <input id="land-ratio" aria-label="${t('creator.landRatio')}" type="range" min="0" max="1" step="0.1" value="0.5">
                <span class="coverage-labels"><span>${t('creator.landRatio.water')}</span><span>${t('creator.landRatio.balanced')}</span><span>${t('creator.landRatio.land')}</span></span>
              </label>
              <label class="coverage-setting tectonic-setting">
                <span class="tectonic-heading">${t('creator.precipitation')} <output id="precipitation-value" for="precipitation">0.5 · ${t('creator.precipitation.balanced')}</output></span>
                <small>${t('creator.precipitation.hint')}</small>
                <input id="precipitation" aria-label="${t('creator.precipitation')}" type="range" min="0" max="1" step="0.1" value="0.5">
                <span class="coverage-labels"><span>${t('creator.precipitation.none')}</span><span>${t('creator.precipitation.balanced')}</span><span>${t('creator.precipitation.rich')}</span></span>
              </label>
              <div id="plane-climates">
                <div class="field-title climate-heading">
                  ${t('creator.climate')} <small>${t('creator.climate.hint')}</small>
                </div>
                <div class="climate-bar" aria-label="${t('creator.climate.aria')}"></div>
                <div class="climate-inputs">
                  ${CLIMATES.map((c, i) => `
                    <label class="climate-row">
                      <span><i style="background:${c.color}"></i>${climateName(i)}</span>
                      <input id="climate-${i}" aria-label="${t('creator.climate.weightAria', { name: climateName(i) })}" type="number" min="0" max="1000" step="1">
                      <output id="percent-${i}"></output>
                    </label>
                  `).join('')}
                </div>
                <p class="setting-note">
                  ${t('creator.climate.note')}
                </p>
              </div>
            </div>
          </details>

          <details class="fold-group" id="fold-sky">
            <summary><span class="fold-name">${t('creator.fold.sky')}</span><span class="fold-mark" aria-hidden="true"></span></summary>
            <div class="fold-body">
              <label class="field-title" for="calendar-mode">
                ${t('creator.calendar')} <small>${t('creator.calendar.hint')}</small>
              </label>
              <div class="seed-field">
                <select id="calendar-mode" class="calendar-select filter-dropdown">
                  <option value="real">${t('creator.calendar.real')}</option>
                  <option value="yuan">${t('creator.calendar.yuan')}</option>
                </select>
              </div>
              <label class="field-title" for="start-real-date">${t('creator.startDate')} <small>${t('creator.startDate.hint')}</small></label>
              <div id="real-date-fields" class="seed-field"><input id="start-real-date" type="date" min="0001-01-01" max="9999-12-31" aria-label="${t('creator.startDate.aria')}"></div>
              <div id="yuan-date-fields" class="date-fields hidden">
                <label>${t('creator.year')}<input id="start-yuan-year" type="number" min="1" max="9999" step="1"></label>
                <label>${t('creator.month')}<input id="start-yuan-month" type="number" min="1" max="12" step="1"></label>
                <label>${t('creator.day')}<input id="start-yuan-day" type="number" min="1" max="30" step="1"></label>
              </div>
            </div>
          </details>

          <details class="fold-group" id="fold-human">
            <summary><span class="fold-name">${t('creator.fold.human')}</span><span class="fold-lock" aria-hidden="true">🔒</span><span class="fold-lock-hint">${t('creator.fold.locked')}</span><span class="fold-mark" aria-hidden="true"></span></summary>
            <div class="fold-body">
              <label class="field-title" for="faction-name">${t('player.factionName')}</label>
              <div class="seed-field"><input id="faction-name" maxlength="40" value="${DEFAULT_PLAYER_FACTION.name}" autocomplete="off"></div>
              <div class="color-scheme">
                <button type="button" id="color-scheme-toggle" class="color-scheme-toggle" aria-expanded="false" aria-controls="color-scheme-body">
                  <span class="color-scheme-chip" id="color-scheme-chip" aria-hidden="true"><span class="color-scheme-core" id="color-scheme-core"></span></span>
                  <span class="color-scheme-name">${t('creator.colorScheme')}</span>
                  <span class="fold-mark" aria-hidden="true"></span>
                </button>
                <div class="color-scheme-body" id="color-scheme-body" hidden>
                  ${colorControl('faction-main-color', t('player.mainColor'), DEFAULT_PLAYER_FACTION.mainColor)}
                  ${colorControl('faction-trim-color', t('player.trimColor'), DEFAULT_PLAYER_FACTION.trimColor)}
                </div>
              </div>
              <div id="artificial-options" class="hidden">
                <div id="artificial-speeds" class="distance-pair">
                  <label class="distance-heading distance-heading-world" for="natural-expansion"><span>${t('creator.expansion')}</span><output id="natural-expansion-value"></output></label>
                  <input id="natural-expansion" type="range" min="0" max="4" step="0.25" value="1">
                  <span class="distance-connector" aria-hidden="true"></span>
                  <label class="distance-heading distance-heading-vegetation" for="natural-evolution"><span>${t('creator.evolution')}</span><output id="natural-evolution-value"></output></label>
                  <div class="distance-vegetation-track"><input id="natural-evolution" type="range" min="0" max="4" step="0.25" value="1"><span class="distance-stop" aria-hidden="true"></span></div>
                </div>
                <p class="setting-note">${t('creator.artificial.speedsHint')}</p>
                <label id="independence-option" class="setting-row" for="flag-independence"><span>${t('creator.independence')}</span><input id="flag-independence" type="checkbox"></label>
                <p id="independence-hint" class="setting-note">${t('creator.independence.hint')}</p>
              </div>
            </div>
          </details>

          <p id="menu-status" class="menu-status" role="status" aria-live="polite"></p>

          <div class="menu-actions">
            <button id="browse-map" class="secondary">
              <span>${t('creator.browseMap')}</span> <span class="btn-arrow">↗</span>
            </button>
          </div>

        </section>

        <section class="atlas-panel" id="atlas-main-panel" aria-label="${t('atlas.panelAria')}">
          <div id="atlas-placeholder" class="atlas-placeholder">
            <div class="contour-art"></div>
            <h3 class="placeholder-caption">${t('atlas.previewTitle')}</h3>
          </div>

          <div id="atlas-content" class="atlas-content hidden">
            <div class="atlas-header-bar">
              <div class="atlas-title-group">
                <h2>${t('atlas.title')}</h2>
                <span id="map-coord-pill" class="coord-pill">${t('atlas.centerPill')}</span>
              </div>
              <div class="atlas-view-tools">
                <span id="planet-view-tools" class="planet-view-tools hidden">
                  <select id="planet-view" aria-label="${t('atlas.globeViewAria')}" class="tool-btn">
                    <option value="globe">${t('atlas.view.globe')}</option><option value="projection">${t('atlas.view.projection')}</option>
                  </select>
                  <button id="map-btn-overview" class="tool-btn" title="${t('atlas.overviewTitle')}">${t('atlas.overview')}</button>
                </span>
                <button id="map-btn-spawn" class="tool-btn" title="${t('atlas.spawnTitle')}">${t('atlas.spawn').replace('🎯 ', '')}</button>
                <button id="map-btn-reset" class="tool-btn" title="${t('atlas.resetTitle')}">${t('atlas.reset')}</button>
                <button id="map-btn-zoom-in" class="tool-btn icon-btn" title="${t('atlas.zoomInTitle')}">＋</button>
                <button id="map-btn-zoom-out" class="tool-btn icon-btn" title="${t('atlas.zoomOutTitle')}">－</button>
                <button id="map-btn-expand" class="tool-btn icon-btn" title="${t('atlas.expandTitle')}">⛶</button>
              </div>
            </div>

            <div class="atlas-workspace">
              <div class="map-viewport-wrapper" id="map-viewport-container">
                <canvas id="atlas-canvas" class="atlas-canvas" aria-label="${t('atlas.canvasAria')}"></canvas>
                <div id="atlas-stale" class="atlas-stale hidden" aria-hidden="true"><div class="contour-art"></div></div>
                <div class="viewport-tip">
                  <span>${t('atlas.viewportTip')}</span>
                </div>
              </div>

              <aside class="atlas-control-panel" id="atlas-control-sidebar" aria-label="${t('atlas.controlsAria')}">
                <div class="ctrl-group">
                  <div class="ctrl-group-title">
                    <span>${t('atlas.layerTitle')}</span>
                  </div>

                  <label class="ctrl-toggle-row">
                    <span class="toggle-text">
                      <strong>${t('atlas.toggleChunkInfo')}</strong>
                      <small>${t('atlas.toggleChunkInfo.hint')}</small>
                    </span>
                    <input type="checkbox" id="toggle-chunk-info" class="switch-input">
                    <span class="switch-slider"></span>
                  </label>

                  <label class="ctrl-toggle-row">
                    <span class="toggle-text"><strong>${t('atlas.toggleChunkColors')}</strong><small>${t('atlas.toggleChunkColors.hint')}</small></span>
                    <input type="checkbox" id="toggle-chunk-colors" class="switch-input"><span class="switch-slider"></span>
                  </label>
                  <label class="ctrl-toggle-row">
                    <span class="toggle-text">
                      <strong>${t('atlas.toggleProjection')}</strong>
                      <small>${t('atlas.toggleProjection.hint')}</small>
                    </span>
                    <input type="checkbox" id="toggle-projection" class="switch-input">
                    <span class="switch-slider"></span>
                  </label>

                  <label class="ctrl-toggle-row">
                    <span class="toggle-text">
                      <strong>${t('atlas.toggleClimate')}</strong>
                      <small>${t('atlas.toggleClimate.hint')}</small>
                    </span>
                    <input type="checkbox" id="toggle-climate-mode" class="switch-input">
                    <span class="switch-slider"></span>
                  </label>

                  <div class="climate-tags-panel" id="climate-tags-panel">
                    <div class="climate-tags-title">${t('atlas.climateTags')}</div>
                    <div class="climate-pills-row" id="climate-pills-container">
                      ${CLIMATES.map((c, i) => `
                        <button class="climate-pill" data-climate="${i}">
                          <i style="background:${c.color}"></i>
                          <span>${climateName(i)}</span>
                          <b id="pill-percent-${i}">0%</b>
                        </button>
                      `).join('')}
                    </div>
                  </div>
                </div>

                <div class="ctrl-group">
                  <div class="ctrl-group-title">
                    <span>${t('atlas.filterTitle')}</span>
                    <span class="red-line-badge">${t('atlas.filterBadge')}</span>
                  </div>

                  <div class="filter-controls-area">
                    <select id="chunk-type-filter" class="filter-dropdown" aria-label="${t('atlas.filterAria')}">
                      <option value="">${t('atlas.filterAll')}</option>
                      ${this.generateChunkFilterOptions()}
                    </select>

                    <div class="quick-filter-row">
                      <button class="qf-btn" data-qf="river">${t('atlas.qf.river')}</button>
                      <button class="qf-btn" data-qf="water">${t('atlas.qf.water')}</button>
                      <button class="qf-btn" data-qf="mountain">${t('atlas.qf.mountain')}</button>
                      <button class="qf-btn" data-qf="snow">${t('atlas.qf.snow')}</button>
                      <button class="qf-btn clear-btn" data-qf="clear">${t('atlas.qf.clear')}</button>
                    </div>

                    <div id="filter-match-status" class="filter-match-status hidden">
                      <span id="filter-match-text">${t('atlas.filterMatchCount', { count: 0 })}</span>
                    </div>
                  </div>
                </div>

                <div class="ctrl-group">
                  <div class="ctrl-group-title">
                    <span>${t('atlas.detailTitle')}</span>
                    <small id="selected-chunk-badge" class="detail-badge">${t('atlas.detailBadge')}</small>
                  </div>
                  <div id="chunk-detail-card" class="chunk-detail-card">
                    <div class="detail-placeholder">${t('atlas.detailPlaceholder')}</div>
                  </div>
                </div>

              </aside>
            </div>
          </div>

          <footer class="atlas-footer">
            <span class="atlas-footer-note">${t('atlas.footerRight')}</span>
            <button id="footer-enter-btn" class="footer-enter">${t('creator.enterWorld')} <span>→</span></button>
          </footer>
        </section>
      </div>
    `;
    const panel = this.el('world-settings-panel');
    const scroll = document.createElement('div'); scroll.className = 'creator-scroll';
    const tabs = document.createElement('div'); tabs.className = 'creator-tabs'; tabs.setAttribute('role', 'tablist');
    const sections = ['presets', 'custom', 'preview'].map((name, index) => {
      const section = document.createElement('div'); section.id = `creator-tab-${name}`;
      section.setAttribute('role', 'tabpanel'); section.setAttribute('aria-labelledby', `creator-tab-button-${name}`);
      section.hidden = index !== 0; scroll.append(section);
      const button = document.createElement('button'); button.textContent = t(`creator.tab.${name}`);
      button.id = `creator-tab-button-${name}`; button.setAttribute('role', 'tab');
      button.setAttribute('aria-controls', section.id); button.setAttribute('aria-selected', String(index === 0));
      button.tabIndex = index === 0 ? 0 : -1;
      button.onclick = () => {
        sections.forEach((item, i) => { item.hidden = i !== index; });
        Array.from(tabs.children).forEach((item, i) => { item.setAttribute('aria-selected', String(i === index)); (item as HTMLElement).tabIndex = i === index ? 0 : -1; });
      };
      button.onkeydown = event => {
        if (!['ArrowLeft', 'ArrowRight'].includes(event.code)) return;
        event.preventDefault(); const next = (index + (event.code === 'ArrowRight' ? 1 : 2)) % 3;
        const target = tabs.children[next] as HTMLButtonElement; target.click(); target.focus();
      };
      tabs.append(button); return section;
    });
    const heading = this.el('menu-title'); heading.remove();
    this.root.querySelector('.menu-header .wordmark')!.textContent = t('creator.title');
    const intro = panel.querySelector('.menu-intro'); intro?.remove();
    const children = Array.from(panel.children);
    const nameLabel = panel.querySelector('[for="world-name"]')!;
    const nameField = this.input('world-name').parentElement!;
    sections[0].append(nameLabel, nameField, this.el('recommended-presets'));
    children.forEach(child => { if (child.parentElement === panel && !child.classList.contains('menu-actions') && child.id !== 'menu-status') sections[1].append(child); });
    sections[2].append(this.el('atlas-control-sidebar'));
    panel.prepend(tabs, scroll);
    this.el('map-btn-spawn').insertAdjacentHTML('afterbegin', icon('flag-pin'));
    this.el('atlas-main-panel').insertAdjacentHTML('beforeend', `<img class="atlas-compass" src="${iconUrl('compass-rose')}" alt="">`);

  }

  private generateChunkFilterOptions(): string {
    // 分组区间与气候带一一对应
    const groups: { label: string; min: number; max: number }[] = [
      { label: t('atlas.chunkGroup.base'), min: 1, max: 10 },
      { label: climateName(0), min: 101, max: 109 },
      { label: climateName(1), min: 201, max: 208 },
      { label: climateName(3), min: 301, max: 308 },
      { label: climateName(4), min: 401, max: 409 },
      { label: t('creator.artificial'), min: 9000, max: 9999 },
    ];

    return groups.map(g => {
      const types = CHUNK_TYPES.filter(t2 => (t2.generate || t2.artificial) && t2.id >= g.min && t2.id <= g.max);
      if (types.length === 0) return '';
      return `
        <optgroup label="${g.label}">
          ${types.map(type => `<option value="${type.id}">${formatChunkId(type.id)} ${chunkName(type)}</option>`).join('')}
        </optgroup>
      `;
    }).join('');
  }

  private initFormValues(seed: string, weights: ClimateWeights): void {
    this.input('world-seed').value = seed;
    this.input('start-real-date').value = dateInputValue(todayDate());
    this.input('start-yuan-year').value = String(DEFAULT_YUAN_DATE.year);
    this.input('start-yuan-month').value = String(DEFAULT_YUAN_DATE.month);
    this.input('start-yuan-day').value = String(DEFAULT_YUAN_DATE.day);
    weights.forEach((v, i) => {
      this.input(`climate-${i}`).value = String(Math.round(v * 10000) / 100);
    });
    this.updateWeights();
  }

  private startUnixMs(): number {
    const mode = this.el<HTMLSelectElement>('calendar-mode').value as 'real' | 'yuan';
    const parts = mode === 'real' ? this.input('start-real-date').value.split('-').map(Number)
      : ['start-yuan-year', 'start-yuan-month', 'start-yuan-day'].map(id => this.input(id).value.trim() ? Number(this.input(id).value) : NaN);
    return startDateUnixMs(mode, { year: parts[0], month: parts[1], day: parts[2] }, -new Date().getTimezoneOffset());
  }

  destroy(): void { this.stopColors(); this.atlasView?.destroy(); }

  private updateArtificialFields(): void {
    const enabled = this.input('artificial-enabled').checked;
    const expansion = this.input('natural-expansion').valueAsNumber;
    const evolution = this.input('natural-evolution');
    const speed = Math.min(evolution.valueAsNumber, expansion);
    evolution.max = String(expansion); evolution.value = String(speed);
    this.el('artificial-speeds').style.setProperty('--distance-limit', String(expansion / 4));
    this.el('artificial-options').classList.toggle('hidden', !enabled);
    this.input('natural-expansion').disabled = !enabled;
    evolution.disabled = !enabled || expansion === 0;
    this.el('natural-expansion-value').textContent = expansion === 0 ? t('creator.artificial.off') : `×${expansion}`;
    this.el('natural-evolution-value').textContent = speed === 0 ? t('creator.artificial.off') : `×${speed}`;
    const independent = enabled && speed > 0;
    this.el('independence-option').classList.toggle('hidden', !independent);
    this.el('independence-hint').classList.toggle('hidden', !independent);
    this.input('flag-independence').disabled = !independent;
    if (!independent) this.input('flag-independence').checked = false;
    const spawn = this.input('spawn-as-flag');
    spawn.disabled = !enabled;
    if (!enabled) {
      if (spawn.checked) this.spawnClearedByLock = true;
      spawn.checked = false;
    } else if (this.spawnClearedByLock) {
      spawn.checked = true;
      this.spawnClearedByLock = false;
    }
    const human = this.el<HTMLDetailsElement>('fold-human');
    human.classList.toggle('locked', !enabled);
    if (!enabled) human.open = false;
  }

  private updateSchemeChip(): void {
    try { this.el('color-scheme-core').style.background = hexColor(this.input('faction-main-color').value); } catch { }
    try { this.el('color-scheme-chip').style.background = hexColor(this.input('faction-trim-color').value); } catch { }
  }

  private markStale(): void {
    this.el('atlas-stale').classList.toggle('hidden', !this.mapShown);
  }

  private updateWeights(): void {
    this.updateArtificialFields();
    this.el('tectonic-value').textContent = this.input('tectonic-activity').value;
    const ratio = Number(this.input('land-ratio').value);
    const precipitation = Number(this.input('precipitation').value);
    const precipitationLabel = precipitation === 0 ? t('creator.precipitation.none') : precipitation === 1 ? t('creator.precipitation.rich')
      : precipitation === .5 ? t('creator.precipitation.balanced') : precipitation === .8 ? t('creator.precipitation.wet') : '';
    this.el('precipitation-value').textContent = `${precipitation.toFixed(1)}${precipitationLabel ? ` · ${precipitationLabel}` : ''}`;
    this.el('land-ratio-value').textContent = this.el<HTMLSelectElement>('generation-mode').value === 'earth' ? t('creator.landRatio.earth')
      : ratio === 0 ? t('creator.landRatio.water') : ratio === 1 ? t('creator.landRatio.land')
      : ratio === .5 ? t('creator.landRatio.balanced') : t('creator.landRatio.percent', {value: Math.round(ratio * 100)});
    try {
      this.generationSettings();
      this.playerSettings();
      renName(this.input('ren-ming').value, renName(this.defaultRenName));
      // 星球气候由温度与纬度决定，平面权重不参与校验。
      if (this.el<HTMLSelectElement>('generation-mode').value !== 'plane') {
        this.el('footer-enter-btn').removeAttribute('disabled'); this.el('browse-map').removeAttribute('disabled');
        return;
      }
      const weights = this.weights();
      weights.forEach((v, i) => {
        const out = this.el(`percent-${i}`);
        if (out) out.textContent = `${(v * 100).toFixed(1)}%`;
      });
      this.el('footer-enter-btn')?.removeAttribute('disabled');
      this.el('browse-map')?.removeAttribute('disabled');
      const bar = this.root.querySelector('.climate-bar');
      if (bar) {
        bar.innerHTML = weights
          .map((w, i) => `<span style="width:${w * 100}%;background:${CLIMATES[i].color}"></span>`)
          .join('');
      }
    } catch (e) {
      CLIMATES.forEach((_, i) => {
        const out = this.el(`percent-${i}`);
        if (out) out.textContent = '—';
      });
      this.el('footer-enter-btn')?.setAttribute('disabled', '');
      this.el('browse-map')?.setAttribute('disabled', '');
      this.error(e);
    }
  }

  private initEventListeners(): void {
    const changed = () => {
      this.status(this.mapShown ? t('creator.changed') : '');
      this.generator = null; this.updateGenerationFields();
      this.markStale();
    };
    this.el<HTMLSelectElement>('generation-mode').onchange = changed;
    this.el('creator-back').onclick = this.back;
    this.el<HTMLSelectElement>('calendar-mode').onchange = () => {
      const yuan = this.el<HTMLSelectElement>('calendar-mode').value === 'yuan';
      this.el('real-date-fields').classList.toggle('hidden', yuan);
      this.el('yuan-date-fields').classList.toggle('hidden', !yuan);
      this.status(t('creator.calendar.switched'));
    };
    this.el<HTMLSelectElement>('planet-view').onchange = () => {
      this.atlasView?.setPlanetView(this.el<HTMLSelectElement>('planet-view').value as 'globe' | 'projection');
    };
    this.el('map-btn-overview').onclick = () => {
      this.atlasView?.setPlanetView(this.el<HTMLSelectElement>('planet-view').value as 'globe' | 'projection');
      this.atlasView?.showOverview();
    };
    // 种子与气候输入监听
    this.root.querySelectorAll<HTMLInputElement>('.world-settings input:not([type=file])').forEach(input => {
      input.addEventListener('input', () => {
        this.generator = null;
        this.status(this.mapShown ? t('creator.changed') : '');
        this.updateWeights();
        this.markStale();
      });
    });

    this.el('color-scheme-toggle').addEventListener('click', () => {
      const body = this.el('color-scheme-body');
      const open = body.hidden;
      body.hidden = !open;
      this.el('color-scheme-toggle').setAttribute('aria-expanded', String(open));
    });
    this.el('color-scheme-body').addEventListener('input', () => this.updateSchemeChip());
    this.root.querySelectorAll<HTMLElement>('details.fold-group').forEach(group => {
      group.querySelector('summary')!.addEventListener('click', event => {
        if (group.classList.contains('locked')) event.preventDefault();
      });
    });

    this.el('random-seed').onclick = () => {
      this.input('world-seed').value = String(crypto.getRandomValues(new Uint32Array(1))[0]);
      this.generator = null;
      this.status(t('creator.seed.randomized'));
      this.markStale();
    };

    // 快速预设按钮
    this.root.querySelectorAll<HTMLButtonElement>('[data-preset]').forEach(btn => {
      btn.onclick = () => {
        const preset = btn.dataset.preset;
        this.applyPreset(preset);
      };
    });

    this.el('browse-map').onclick = () => this.preview();

    const handleEnter = () => {
      try {
        const options = this.settings();
        this.enter(options);
      } catch (e) {
        this.error(e);
      }
    };
    this.el('footer-enter-btn').onclick = handleEnter;


    // ---- 舆图右侧窗口控制项绑定 ----

    // 1. 显示/隐藏区块信息（编号、类型）
    const toggleInfo = this.el<HTMLInputElement>('toggle-chunk-info');
    toggleInfo.onchange = () => {
      if (this.atlasView) {
        this.atlasView.setShowChunkInfo(toggleInfo.checked);
      }
    };

    // 2. 显示/隐藏具体投影
    const toggleProj = this.el<HTMLInputElement>('toggle-projection');
    const toggleColors=this.el<HTMLInputElement>('toggle-chunk-colors');
    toggleColors.onchange=()=>this.atlasView?.setShowChunkColors(toggleColors.checked);
    toggleProj.onchange = () => {
      if (this.atlasView) {
        this.atlasView.setShowProjection(toggleProj.checked);
      }
    };

    // 3. 显示气候分类
    const toggleClimate = this.el<HTMLInputElement>('toggle-climate-mode');
    toggleClimate.onchange = () => {
      if (this.atlasView) {
        this.atlasView.setShowClimate(toggleClimate.checked);
      }
    };

    // 气候单个标签过滤点击
    this.root.querySelectorAll<HTMLButtonElement>('.climate-pill').forEach(pill => {
      pill.onclick = () => {
        if (!this.atlasView) return;
        const climateIdx = Number(pill.dataset.climate);
        const current = this.atlasView.getActiveClimateFilter();
        const next = current === climateIdx ? null : climateIdx;
        this.atlasView.setActiveClimateFilter(next);
        this.root.querySelectorAll('.climate-pill').forEach(p => p.classList.remove('active'));
        if (next !== null) pill.classList.add('active');
      };
    });

    // 4. 筛选特定区块（红色轮廓线高亮）
    const typeFilterSelect = this.el<HTMLSelectElement>('chunk-type-filter');
    typeFilterSelect.onchange = () => {
      if (!this.atlasView) return;
      const val = typeFilterSelect.value;
      if (val === '') {
        this.atlasView.setFilterTypeId(null);
      } else {
        this.atlasView.setFilterTypeId(Number(val));
      }
      this.updateQuickFilterActive('');
    };

    // 快捷筛选按钮
    this.root.querySelectorAll<HTMLButtonElement>('[data-qf]').forEach(btn => {
      btn.onclick = () => {
        if (!this.atlasView) return;
        const qf = btn.dataset.qf;
        if (qf === 'clear') {
          typeFilterSelect.value = '';
          this.atlasView.clearFilter();
          this.updateQuickFilterActive('');
          this.root.querySelectorAll('.climate-pill').forEach(p => p.classList.remove('active'));
        } else {
          typeFilterSelect.value = '';
          this.atlasView.setFilterCategory(qf || null);
          this.updateQuickFilterActive(qf || '');
        }
      };
    });

    // 地图顶部工具栏按钮
    this.el('map-btn-spawn')?.addEventListener('click', () => {
      this.atlasView?.centerSpawn();
    });
    this.el('map-btn-reset')?.addEventListener('click', () => {
      this.atlasView?.resetView();
    });
    this.el('map-btn-zoom-in')?.addEventListener('click', () => {
      this.atlasView?.zoomBy(1.25);
    });
    this.el('map-btn-zoom-out')?.addEventListener('click', () => {
      this.atlasView?.zoomBy(0.8);
    });
    this.el('map-btn-expand')?.addEventListener('click', () => {
      this.toggleExpandMap();
    });
  }

  private applyPreset(preset?: string): void {
    if (!preset) return;
    const presets: Record<string, { weights: ClimateWeights; landRatio: number }> = {
      balanced: { weights: [20, 20, 20, 20, 20], landRatio: .5 },
      islands: { weights: [45, 35, 15, 5, 0], landRatio: .2 },
      mountains: { weights: [10, 15, 35, 30, 10], landRatio: .8 },
      arctic: { weights: [0, 5, 15, 35, 45], landRatio: .5 },
    };
    const target = presets[preset];
    if (target) {
      this.setGenerationForm(normalizeGeneration({...this.generationSettings(), landRatio: target.landRatio}));
      target.weights.forEach((w, i) => {
        this.input(`climate-${i}`).value = String(w);
      });
      this.generator = null;
      this.updateWeights();
      this.status(t('creator.presetApplied', { desc: t(`creator.preset.${preset}.desc`) }));
      this.markStale();
    }
  }

  private updateQuickFilterActive(activeQf: string): void {
    this.root.querySelectorAll<HTMLButtonElement>('[data-qf]').forEach(b => {
      b.classList.toggle('active', b.dataset.qf === activeQf && activeQf !== 'clear');
    });
  }

  private toggleExpandMap(): void {
    this.isFullscreenMap = !this.isFullscreenMap;
    const container = this.el('menu-layout-container');
    const settingsPanel = this.el('world-settings-panel');


    if (this.isFullscreenMap) {
      container.classList.add('map-fullscreen');
      settingsPanel.classList.add('collapsed');

    } else {
      container.classList.remove('map-fullscreen');
      settingsPanel.classList.remove('collapsed');

    }

    setTimeout(() => {
      this.atlasView?.requestRender();
    }, 120);
  }

  private preview(): void {
    try {
      const options = this.settings();
      if (!this.generator) {
        this.generator = new WorldGenerator(options.seed, options.climateWeights, options.generation);
      }

      const artificial = new ArtificialWorld(this.generator, options.unixMs, options.calendarType, options.utcOffsetMinutes);
      const spawn = playerFlagSpawn(artificial, options.playerFaction!);
      this.mapShown = true;
      this.el('atlas-placeholder').classList.add('hidden');
      this.el('atlas-content').classList.remove('hidden');
      this.el('atlas-stale').classList.add('hidden');

      const canvas = this.el<HTMLCanvasElement>('atlas-canvas');
      if (!this.atlasView) {
        this.atlasView = new AtlasView({
          canvas,
          generator: this.generator, artificial,
          onChunkSelect: (info) => this.showChunkDetail(info),
          onStatsUpdate: (stats) => this.updateViewportStats(stats),
        });
      } else {
        this.atlasView.setGenerator(this.generator, artificial);
      }
      this.atlasView.centerAt(spawn.cx, spawn.cz);
      this.atlasView.setShowChunkInfo(this.input('toggle-chunk-info').checked);
      this.atlasView.setShowChunkColors(this.input('toggle-chunk-colors').checked);
      this.atlasView.setShowProjection(this.input('toggle-projection').checked);
      this.atlasView.setShowClimate(this.input('toggle-climate-mode').checked);
      if (options.generation.mode === 'planet') {
        this.atlasView.setPlanetView(this.el<HTMLSelectElement>('planet-view').value as 'globe' | 'projection');
        this.atlasView.showOverview();
      }

      this.status(t('creator.mapReady', { seed: options.seed }));
    } catch (e) {
      this.error(e);
    }
  }

  private showChunkDetail(info: TerritoryChunkInfo): void {
    const card = this.el('chunk-detail-card');
    const badge = this.el('selected-chunk-badge');
    const def = getChunkTypeDef(info.type);
    const code = formatChunkId(info.type);
    const climate = CLIMATES[info.climate];
    const arrow = info.displayFlow >= 0 ? FLOW_DIRECTIONS[info.displayFlow].arrow : '—';
    const flowText = info.displayFlow >= 0 ? `${arrow} (${FLOW_DIRECTIONS[info.displayFlow].dx}, ${FLOW_DIRECTIONS[info.displayFlow].dz})` : t('detail.noFlow');

    if (badge) {
      badge.textContent = `(${info.cx}, ${info.cz})`;
    }

    card.innerHTML = `
      <div class="detail-header">
        <span class="detail-type-tag" style="background:${def.mapColor}">
          ${code} · ${chunkName(def)}
        </span>
        <span class="detail-climate-tag" style="border-color:${climate.color};color:${climate.color}">
          ${climateName(info.climate)}
        </span>
      </div>

      <div class="detail-grid">
        <div class="detail-item">
          <span class="detail-label">${t('detail.coord')}</span>
          <span class="detail-val">(${info.cx}, ${info.cz})</span>
        </div>
        <div class="detail-item">
          <span class="detail-label">${t('detail.elevation')}</span>
          <span class="detail-val">${t('detail.meters', { value: info.elevation.toFixed(1) })}</span>
        </div>
        <div class="detail-item">
          <span class="detail-label">${t('detail.temperature')}</span>
          <span class="detail-val">${info.temperature.toFixed(1)} °C</span>
        </div>
        ${this.generator?.generation.mode === 'planet' ? (() => {
          const coord = planetCoordinates(info.cx, info.cz, this.generator!.generation.planet.equatorChunks);
          return `<div class="detail-item"><span class="detail-label">${t('detail.latlon')}</span><span class="detail-val">${coord.longitude.toFixed(2)}° / ${coord.latitude.toFixed(2)}°</span></div>`;
        })() : ''}
        <div class="detail-item">
          <span class="detail-label">${t('detail.flow')}</span>
          <span class="detail-val">${flowText}</span>
        </div>
        ${info.discharge > 0 ? `
          <div class="detail-item">
            <span class="detail-label">${t('detail.discharge')}</span>
            <span class="detail-val">${info.discharge.toFixed(1)}</span>
          </div>
          <div class="detail-item">
            <span class="detail-label">${t('detail.riverWidth')}</span>
            <span class="detail-val">${t('detail.riverWidthValue', { value: info.riverWidth.toFixed(0) })}</span>
          </div>
          ${info.riverDepth > 0 ? `<div class="detail-item">
            <span class="detail-label">${t('detail.riverDepth')}</span>
            <span class="detail-val">${t('detail.riverWidthValue', { value: info.riverDepth.toFixed(1) })}</span>
          </div>` : ''}
        ` : ''}
      </div>

      <div class="detail-desc">${chunkDescription(def)}</div>
      ${info.territory ? `<div class="detail-desc">${t('territory.detail', { id: escapeHtml(info.territory.name ?? info.territory.id), level: info.territory.level })}<br><span style="color:${info.territory.mainColor}">■ ${info.territory.mainColor}</span> · <span style="color:${info.territory.trimColor}">■ ${info.territory.trimColor}</span></div>` : ''}
    `;
  }

  private updateViewportStats(stats: ViewportStats): void {
    // 顶部坐标胶囊
    const pill = this.el('map-coord-pill');
    if (pill) {
      const level = stats.level === 'globe' ? t('detail.level.globe') : stats.level === 'overview' ? t('detail.level.overview') : t('detail.level.chunks');
      pill.textContent = t('detail.pill', {
        level,
        cx: stats.centerCx,
        cz: stats.centerCz,
        scale: (stats.scale * 100).toFixed(1),
        unit: stats.level === 'chunks' ? t('detail.unit.chunks') : t('detail.unit.sampledShort'),
        count: stats.visibleChunks,
      });
    }

    // 气候标签百分比
    stats.climatePercents.forEach((pct, i) => {
      const el = this.el(`pill-percent-${i}`);
      if (el) el.textContent = `${pct.toFixed(0)}%`;
    });

    // 筛选结果提示
    const matchStatus = this.el('filter-match-status');
    const matchText = this.el('filter-match-text');
    if (stats.activeFilterName) {
      matchStatus.classList.remove('hidden');
      matchText.innerHTML = t('detail.matchStatus', {
        name: stats.activeFilterName,
        count: stats.matchedCount,
        unit: stats.level === 'chunks' ? t('detail.unit.chunks') : t('detail.unit.sampled'),
        percent: stats.matchedPercent.toFixed(1),
      }) + (stats.level === 'chunks' ? '' : `<br>${t('detail.matchStatusHint')}`);
    } else {
      matchStatus.classList.add('hidden');
    }
  }
}
