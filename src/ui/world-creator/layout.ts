import { t } from '../../i18n';
import { renderSupport } from '../../core/RenderSupport';
import { icon, iconUrl } from '../Icons';
import { colorControl } from '../ColorControls';
import { DEFAULT_PLAYER_FACTION } from '../../systems/world/artificial/PlayerFaction';
import { profileHeightRange } from '../../entities/PlayerProfile';
import { DEFAULT_ARTIFICIAL } from '../../systems/world/artificial/ArtificialSettings';
import { fitBookmarks } from '../adapt/bookmarks';
import type { PlayMode } from '../../core/PlayMode';
import type { ViewportStats } from '../AtlasView';
import type { FieldAccess } from './fields';

/** 创建页布局与视口统计 */
export class CreatorLayout implements FieldAccess {
  private stopBookmarks: () => void = () => {};

  constructor(readonly root: HTMLElement, playMode: PlayMode, signal: AbortSignal) {
    this.render(playMode, signal);
    this.stopBookmarks = fitBookmarks(this.el('world-settings-panel'));
  }

  el<T extends HTMLElement = HTMLElement>(id: string): T {
    return this.root.querySelector(`#${id}`) as T;
  }

  input(id: string): HTMLInputElement {
    return this.el<HTMLInputElement>(id);
  }

  status(message: string): void {
    const el = this.el('menu-status');
    if (el) el.textContent = message;
  }

  setFormEnabled(enabled: boolean): void {
    for (const id of ['footer-enter-btn', 'browse-map']) {
      const button = this.el(id);
      if (enabled) button.removeAttribute('disabled');
      else button.setAttribute('disabled', '');
    }
  }

  markStale(visible: boolean): void {
    this.el('atlas-stale').classList.toggle('hidden', !visible);
  }

  showMap(): void {
    this.el('atlas-placeholder').classList.add('hidden');
    this.el('atlas-content').classList.remove('hidden');
    this.el('atlas-stale').classList.add('hidden');
  }

  updateViewportStats(stats: ViewportStats): void {
    // 底栏舆图状态
    const pill = this.el('map-coord-pill');
    if (pill) {
      pill.classList.remove('hidden');
      const level = stats.level === 'globe' ? t('detail.level.globe') : stats.level === 'overview' ? t('detail.level.overview') : t('detail.level.chunks');
      pill.textContent = t('detail.pill', {
        level,
        cx: stats.centerCx,
        cz: stats.centerCz,
        scale: (stats.scale * 100).toFixed(1),
        unit: stats.level === 'chunks' ? t('detail.unit.chunks') : t('detail.unit.sampledShort'),
        count: stats.visibleChunks,
      });
      pill.title = pill.textContent;
    }

  }

  destroy(): void {
    this.stopBookmarks();
    this.stopBookmarks = () => {};
  }

  private render(playMode: PlayMode, signal: AbortSignal): void {
    this.root.innerHTML = `
      <header class="menu-header">
        <a class="wordmark" href="./">${t('app.name')}</a>
        <div class="header-right">
          <button id="creator-back" class="menu-back">${t('creator.back')}</button>
        </div>
      </header>

      <div class="menu-layout" id="menu-layout-container">
        <section class="world-settings atlas-frame" id="world-settings-panel" aria-labelledby="menu-title">
          <h1 id="menu-title">${t('creator.title')}</h1>
          <p class="menu-intro">${t('creator.intro')}</p>

          <div id="recommended-presets" class="world-preset-columns">
            <section class="preset-column"><h2>${t('creator.category.planet')}</h2><div class="presets-row">
              ${['primordial', 'frozen', 'habitable', 'archipelago', 'redhot'].map(id => `<button class="preset-pill" data-preset="${id}">${t(`creator.preset.${id}`)}</button>`).join('')}
            </div></section>
          </div>

          <label class="field-title" for="world-name">${t('creator.worldName')}</label>
          <div class="seed-field"><input id="world-name" maxlength="80" value="${t('creator.worldNamePlaceholder')}" autocomplete="off"></div>

          <label class="field-title" for="ren-ming">${t('player.worldName')}</label>
          <div class="seed-field"><input id="ren-ming" maxlength="40" autocomplete="off"></div>
          <label class="field-title" for="profile-height">${t('profile.height')}</label>
          <div class="seed-field"><input id="profile-height" type="number" min="${profileHeightRange(playMode).min}" max="${profileHeightRange(playMode).max}" step="1"></div>

          <label class="field-title" for="generation-mode">${t('creator.generationMode')}</label>
          <select id="generation-mode" class="filter-dropdown" disabled>
            <option value="planet-macro">${t('creator.mode.planetMacro')}</option>
          </select>
          <p id="lightweight-note" class="setting-note hidden">${t('creator.lightweightNote')}</p>
          <label class="field-title" for="macro-terrain">${t('creator.macroTerrain')}</label>
          <select id="macro-terrain" class="filter-dropdown">
              ${['balanced','archipelago','mountains','rift'].map(profile=>`<option value="${profile}">${t(`creator.macroTerrain.${profile}`)}</option>`).join('')}
          </select>

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
          </section>

          <details class="fold-group" id="fold-geo">
            <summary><span class="fold-name">${t('creator.fold.geo')}</span><span class="fold-mark" aria-hidden="true"></span></summary>
            <div class="fold-body">
              <div class="planet-fields">
                <label class="field-wide">${t('creator.macroClimate')}<select id="macro-climate" class="filter-dropdown">
                <option value="wind-field">${t('creator.macroClimate.wind')}</option>
                </select></label>
                <label class="tectonic-setting"><span class="tectonic-heading">${t('creator.macroSize')} <output id="macro-size-value" for="macro-size"></output></span><input id="macro-size" aria-label="${t('creator.macroSize')}" data-range-stops="step" type="range" min="0" max="2" step="1"></label>
                <label class="tectonic-setting"><span class="tectonic-heading">${t('creator.macroScale')} <output id="macro-scale-value" for="macro-scale"></output></span><input id="macro-scale" aria-label="${t('creator.macroScale')}" data-range-stops="step" type="range" min="0" max="6" step="1"></label>
                <p id="macro-dimensions" class="setting-note"></p>
                <label>${t('creator.poleTemp')} <small>${t('creator.poleTemp.hint')}</small><input id="pole-temperature" type="number" min="-100" max="100" step="1"></label>
                <label>${t('creator.equatorTemp')} <small>${t('creator.equatorTemp.hint')}</small><input id="equator-temperature" type="number" min="-100" max="100" step="1"></label>
                <label class="hidden">${t('creator.monsoon')} <small>${t('creator.monsoon.hint')}</small><input id="monsoon-direction" type="number" min="0" max="359" step="1"></label>
                <label class="hidden">${t('creator.equatorChunks')} <small>${t('creator.equatorChunks.hint')}</small><input id="equator-chunks" type="number" min="256" max="640000" step="4"></label>
                <label class="tectonic-setting"><span class="tectonic-heading">${t('creator.tectonicActivity')} <output id="tectonic-value" for="tectonic-activity">5</output></span><small>${t('creator.tectonicActivity.hint')}</small><input id="tectonic-activity" aria-label="${t('creator.tectonicActivity')}" data-range-stops="step" type="range" min="0" max="9" step="1"></label>
              </div>
              <label class="coverage-setting tectonic-setting">
                <span class="tectonic-heading">${t('creator.landRatio')} <output id="land-ratio-value" for="land-ratio">${t('creator.landRatio.balanced')}</output></span>
                <small>${t('creator.landRatio.hint')}</small>
                <input id="land-ratio" aria-label="${t('creator.landRatio')}" data-range-stops="0,0.5,1" type="range" min="0" max="1" step="0.1" value="0.5">
                <span class="coverage-labels"><span>${t('creator.landRatio.water')}</span><span>${t('creator.landRatio.balanced')}</span><span>${t('creator.landRatio.land')}</span></span>
              </label>
              <label class="coverage-setting tectonic-setting">
                <span class="tectonic-heading">${t('creator.precipitation')} <output id="precipitation-value" for="precipitation">1 · ${t('creator.macroRain.baseline')}</output></span>
                <small>${t('creator.macroRain.hint')}</small>
                <input id="precipitation" aria-label="${t('creator.precipitation')}" data-range-stops="0,1,2" type="range" min="0" max="2" step="0.25" value="1">
                <span class="coverage-labels"><span>${t('creator.precipitation.none')}</span><span>${t('creator.macroRain.baseline')}</span><span>${t('creator.precipitation.rich')}</span></span>
              </label>
            </div>
          </details>

          <details class="fold-group" id="fold-sky">
            <summary><span class="fold-name">${t('creator.fold.sky')}</span><span class="fold-mark" aria-hidden="true"></span></summary>
            <div class="fold-body">
              <label class="field-title" for="creation-time-speed">${t('settings.speed')} <small>${t('settings.speed.hint')}</small></label>
              <div class="seed-field"><input id="creation-time-speed" type="number" min="0" max="86400" step="0.5" value="72"></div>
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
            <summary><span class="fold-name">${t('creator.fold.human')}</span><span class="fold-lock" aria-hidden="true">${icon('menu-lock', 'fold-lock-icon')}</span><span class="fold-lock-hint">${t('creator.fold.locked')}</span><span class="fold-mark" aria-hidden="true"></span></summary>
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
                <label class="field-title" for="initial-civilization-limit">${t('creator.initialCivilizationLimit')}</label>
                <div class="seed-field"><input id="initial-civilization-limit" type="number" min="0" max="256" step="1" value="${DEFAULT_ARTIFICIAL.initialCivilizationLimit}"></div>
                <p class="setting-note">${t('creator.initialCivilizationLimit.hint')}</p>
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

        <section class="atlas-panel atlas-frame" id="atlas-main-panel" aria-label="${t('atlas.panelAria')}">
          <div id="atlas-placeholder" class="atlas-placeholder">
            <h3 class="placeholder-caption">${t('atlas.previewTitle')}</h3>
          </div>
          <div id="atlas-loading" class="atlas-loading hidden" role="status" aria-live="polite">
            <div class="atlas-loading-flower" aria-hidden="true"></div>
            <p class="atlas-loading-label">${t('atlas.loading')}</p>
          </div>

          <div id="atlas-content" class="atlas-content hidden">
            <div class="atlas-header-bar">
              <div class="atlas-title-group">
                <h2>${t('atlas.title')}</h2>
              </div>
              <div class="atlas-view-tools">
                <span id="planet-view-tools" class="planet-view-tools hidden">

                  <button id="map-btn-overview" class="tool-btn" ${playMode==='survival'?'hidden':''} title="${t('atlas.overviewTitle')}">${t('atlas.overview')}</button>
                </span>
                <button id="map-btn-spawn" class="tool-btn" title="${t('atlas.spawnTitle')}">${t('atlas.spawn')}</button>
                <button id="map-btn-reset" class="tool-btn" title="${t('atlas.resetTitle')}">${t('atlas.reset')}</button>
                <button id="map-btn-zoom-in" class="tool-btn icon-btn" title="${t('atlas.zoomInTitle')}">＋</button>
                <button id="map-btn-zoom-out" class="tool-btn icon-btn" title="${t('atlas.zoomOutTitle')}">－</button>
                <button id="map-btn-expand" class="tool-btn icon-btn" title="${t('atlas.expandTitle')}" aria-label="${t('atlas.expandTitle')}"><svg viewBox="0 0 16 16" width="14" height="14" fill="none" stroke="currentColor" stroke-width="1.8" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true" style="display:inline-block;vertical-align:middle"><path d="M2 5V2h3m6 0h3v3m0 6v3h-3m-6 0H2v-3"/></svg></button>
              </div>
            </div>

            <div class="atlas-workspace">
              <div class="map-viewport-wrapper atlas-frame" id="map-viewport-container">
                <canvas id="atlas-canvas" class="atlas-canvas" aria-label="${t('atlas.canvasAria')}"></canvas>
                <div id="atlas-stale" class="atlas-stale hidden" aria-hidden="true"></div>
                <div class="viewport-tip">
                  <span>${t(playMode==='survival'?'atlas.survivalViewportTip':'atlas.viewportTip')}</span>
                </div>
              </div>


            </div>
          </div>

          <footer class="atlas-footer">
            <span id="map-coord-pill" class="coord-pill atlas-footer-note hidden">${t('atlas.centerPill')}</span>
            <button id="footer-enter-btn" class="footer-enter">${t('creator.enterWorld')} <span>→</span></button>
          </footer>
        </section>
      </div>
    `;
    const panel = this.el('world-settings-panel');
    const scroll = document.createElement('div'); scroll.className = 'creator-scroll';
    const tabs = document.createElement('div'); tabs.className = 'creator-tabs world-bookmark-tabs'; tabs.setAttribute('role', 'tablist');
    const sections = ['presets', 'custom', 'preview'].map((name, index) => {
      const section = document.createElement('div'); section.id = `creator-tab-${name}`;
      section.setAttribute('role', 'tabpanel'); section.setAttribute('aria-labelledby', `creator-tab-button-${name}`);
      section.hidden = index !== 0; scroll.append(section);
      const button = document.createElement('button');
      const label = document.createElement('span'); label.className = 'bookmark-label'; label.textContent = name === 'preview' ? t('game.map.layers') : t(`creator.tab.${name}`);
      button.append(label);
      button.id = `creator-tab-button-${name}`; button.setAttribute('role', 'tab');
      button.hidden = name === 'preview' && playMode === 'survival';
      button.setAttribute('aria-controls', section.id); button.setAttribute('aria-selected', String(index === 0));
      button.tabIndex = index === 0 ? 0 : -1;
      button.addEventListener('click', () => {
        scroll.scrollTop = 0;
        sections.forEach((item, i) => { item.hidden = i !== index; });
        Array.from(tabs.children).forEach((item, i) => { item.setAttribute('aria-selected', String(i === index)); (item as HTMLElement).tabIndex = i === index ? 0 : -1; });
      }, { signal });
      button.addEventListener('keydown', event => {
        const vertical = tabs.getAttribute('aria-orientation') === 'vertical';
        const [previous, nextKey] = vertical ? ['ArrowUp', 'ArrowDown'] : ['ArrowLeft', 'ArrowRight'];
        if (![previous, nextKey].includes(event.code)) return;
        event.preventDefault();
        const visible = Array.from(tabs.children).filter(item => !(item as HTMLElement).hidden) as HTMLButtonElement[];
        const next = (visible.indexOf(button) + (event.code === nextKey ? 1 : visible.length - 1)) % visible.length;
        const target = visible[next]; target.click(); target.focus();
      }, { signal });
      tabs.append(button); return section;
    });
    const heading = this.el('menu-title'); heading.remove();
    this.root.querySelector('.menu-header .wordmark')!.textContent = `${t('creator.title')} · ${t(`mode.${playMode}`)}`;
    const intro = panel.querySelector('.menu-intro'); intro?.remove();
    const children = Array.from(panel.children);
    const nameLabel = panel.querySelector('[for="world-name"]')!;
    const nameField = this.input('world-name').parentElement!;
    sections[0].append(nameLabel, nameField, this.el('recommended-presets'));
    children.forEach(child => { if (child.parentElement === panel && !child.classList.contains('menu-actions') && child.id !== 'menu-status') sections[1].append(child); });
    sections[2].setAttribute('data-layer-panel', '');
    sections[2].innerHTML = `<label class="field-title" for="planet-view">${t('atlas.globeViewAria')}</label>
                  <select id="planet-view" aria-label="${t('atlas.globeViewAria')}" class="filter-dropdown">
                    <option value="globe">${t('atlas.view.globe')}</option><option value="projection">${t('atlas.view.projection')}</option><option value="mercator">${t('atlas.view.mercator')}</option>
                  </select>
      <div class="map-layer-list" data-layer-list></div>`;
    sections[2].querySelector<HTMLSelectElement>('#planet-view')!.value = renderSupport.available ? 'globe' : 'mercator';
    const frame = document.createElement('div'); frame.className = 'bookmark-content-frame';
    frame.append(scroll, ...Array.from(panel.children)); panel.append(tabs, frame);
    this.el('map-btn-spawn').insertAdjacentHTML('afterbegin', icon('flag-pin'));
    this.el('atlas-main-panel').insertAdjacentHTML('beforeend', `<img class="atlas-compass" src="${iconUrl('compass-rose')}" alt="">`);
  }
}
