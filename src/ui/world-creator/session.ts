import { renName } from '../../entities/PlayerIdentity';
import { profileHeightRange, type PlayerProfile } from '../../entities/PlayerProfile';
import type { PlayMode } from '../../core/PlayMode';
import type { WorldGeneration } from '../../systems/world/WorldSettings';
import { bindColorControls } from '../ColorControls';
import { t } from '../../i18n';
import type { PlanetView } from '../PlanetAtlasLayer';
import { CreatorLayout } from './layout';
import { CreatorForm } from './form';
import { createAtlasLoading } from './loading';
import { PreviewController, type AtlasFactory, type PrepareWorldFn, type PreviewControlState, type PreviewTiming } from './preview';
import type { StartOptions } from './options';
import { mountAtlasLayerPanel } from '../game/AtlasLayerPanel';
import { defaultLayers } from '../atlas/layers.mjs';
import type { AtlasDisplayState } from '../../systems/world/WorldSave';

export interface CreatorSessionOptions {
  root: HTMLElement;
  seed: string;
  enter: (options: StartOptions) => void;
  generation: WorldGeneration;
  back: () => void;
  defaultRenName: string;
  playMode: PlayMode;
  defaultProfile: PlayerProfile;
  /** 测试注入：生成服务/视图/时钟 */
  preview?: {
    prepare?: PrepareWorldFn;
    createAtlas?: AtlasFactory;
    createArtificialWorker?: () => Worker;
    now?: () => number;
  };
  onPreviewPhase?: (timing: PreviewTiming) => void;
}

function errorMessage(error: unknown): string {
  return error instanceof Error ? error.message : String(error);
}

/** 非世界参数不参与签名，不中断在途 */
const NON_PREVIEW_FIELDS = new Set(['world-name', 'ren-ming', 'profile-height', 'creation-time-speed']);

/** 创建页流程装配：表单、布局与预览控制器各自持有职责 */
export class CreatorSession {
  readonly previewKey = crypto.randomUUID();
  private readonly layout: CreatorLayout;
  private readonly form: CreatorForm;
  private readonly preview: PreviewController;
  private readonly events = new AbortController();
  private readonly stopColors: () => void;
  private readonly enter: (options: StartOptions) => void;
  private readonly back: () => void;
  private expandTimer?: ReturnType<typeof setTimeout>;
  private mapShown = false;
  private readonly display: AtlasDisplayState = { layers: defaultLayers(), density: 2 };
  private stopLayers: () => void = () => {};

  constructor(options: CreatorSessionOptions) {
    this.enter = options.enter;
    this.back = options.back;
    this.layout = new CreatorLayout(options.root, options.playMode, this.events.signal);
    this.layout.input('ren-ming').value = renName(options.defaultRenName);
    const range = profileHeightRange(options.playMode);
    this.layout.input('profile-height').value = String(Math.max(range.min, Math.min(range.max, options.defaultProfile.heightCm)));
    this.layout.input('faction-main-color').value = options.defaultProfile.mainColor;
    this.layout.input('faction-trim-color').value = options.defaultProfile.trimColor;
    this.stopColors = bindColorControls(options.root);
    this.form = new CreatorForm(this.layout, {
      playMode: options.playMode,
      defaultRenName: options.defaultRenName,
      defaultProfile: options.defaultProfile,
    });
    this.preview = new PreviewController({
      worldKey: this.previewKey,
      playMode: options.playMode,
      prepare: options.preview?.prepare,
      createAtlas: options.preview?.createAtlas,
      createArtificialWorker: options.preview?.createArtificialWorker,
      now: options.preview?.now,
      hooks: {
        onStatsUpdate: stats => this.layout.updateViewportStats(stats),
        onPhase: options.onPreviewPhase,
      },
      ui: {
        canvas: this.layout.el<HTMLCanvasElement>('atlas-canvas'),
        createLoading: () => createAtlasLoading(this.layout.el('atlas-main-panel'), this.layout.el('atlas-loading')),
        showMap: () => { this.mapShown = true; this.layout.showMap(); },
        setStatus: message => this.layout.status(message),
        setError: message => this.layout.status(message),
        setBusy: busy => {
          const button = this.layout.el<HTMLButtonElement>('browse-map');
          button.setAttribute('aria-busy', String(busy));
          button.toggleAttribute('data-preview-busy', busy);
        },
        readControls: (): PreviewControlState => ({
          layers: this.display.layers,
          density: this.display.density,
          planetView: this.layout.el<HTMLSelectElement>('planet-view').value as PlanetView,
        }),
      },
    });
    this.form.syncSchemeChip();
    this.form.initialize(options.seed);
    this.form.setGenerationForm(options.generation);
    const error = this.refreshFormState();
    if (!error && !this.layout.el<HTMLButtonElement>('browse-map').disabled) this.layout.status('');
    this.bindEvents();
  }

  destroy(): void {
    this.events.abort();
    clearTimeout(this.expandTimer);
    this.preview.destroy();
    this.stopColors();
    this.stopLayers();
    this.layout.destroy();
  }

  private refreshFormState(): unknown | null {
    const error = this.form.refresh();
    this.layout.setFormEnabled(!error);
    if (error) this.layout.status(errorMessage(error));
    this.mountLayers();
    return error;
  }

  private mountLayers(): void {
    this.stopLayers();
    this.stopLayers = mountAtlasLayerPanel(this.layout.root, this.display, () => {
      this.preview.atlas?.setMapLayers(this.display.layers.map(layer => ({ ...layer })));
      this.preview.atlas?.setContourDensity(this.display.density);
    }, { available: id => id !== 'chunks' && id !== 'rain' && (id !== 'civilization' || this.layout.input('artificial-enabled').checked) });
  }

  private formChanged(field?: { id: string }): void {
    const affectsPreview = !field || !NON_PREVIEW_FIELDS.has(field.id);
    if (affectsPreview) {
      this.preview.invalidate();
      this.form.syncGenerationFields();
    }
    const error = this.refreshFormState();
    if (!error && affectsPreview) this.layout.status(this.mapShown ? t('creator.changed') : '');
    if (affectsPreview) this.layout.markStale(this.mapShown);
  }

  private startPreview(): void {
    try {
      const options = this.form.settings();
      void this.preview.start(options).catch(error => this.layout.status(errorMessage(error)));
    } catch (error) {
      this.layout.status(errorMessage(error));
    }
  }

  private enterWorld(): void {
    try {
      const options = this.form.settings();
      this.preview.invalidate();
      this.enter({ ...options, worldKey: this.previewKey });
    } catch (error) {
      this.layout.status(errorMessage(error));
    }
  }

  private applyPreset(preset?: string): void {
    const applied = this.form.applyPreset(preset);
    if (!applied) return;
    this.preview.invalidate();
    this.layout.root.querySelectorAll<HTMLButtonElement>('[data-preset]').forEach(button => button.classList.toggle('active', button.dataset.preset === applied));
    const error = this.refreshFormState();
    if (!error) {
      this.layout.status(t('creator.presetApplied', { desc: applied.startsWith('earth-') ? t(`creator.earthPrecision.${applied.slice(6)}`) : t(`creator.preset.${applied}`) }));
    }
    this.layout.markStale(this.mapShown);
  }

  private calendarChanged(): void {
    this.preview.invalidate();
    const yuan = this.layout.el<HTMLSelectElement>('calendar-mode').value === 'yuan';
    this.layout.el('real-date-fields').classList.toggle('hidden', yuan);
    this.layout.el('yuan-date-fields').classList.toggle('hidden', !yuan);
    this.layout.status(t('creator.calendar.switched'));
    this.layout.markStale(this.mapShown);
  }

  private toggleExpandMap(): void {
    const container = this.layout.el('menu-layout-container');
    const settingsPanel = this.layout.el('world-settings-panel');
    const fullscreen = container.classList.toggle('map-fullscreen');
    settingsPanel.classList.toggle('collapsed', fullscreen);
    clearTimeout(this.expandTimer);
    this.expandTimer = setTimeout(() => this.preview.atlas?.requestRender(), 120);
  }

  private bindEvents(): void {
    const signal = this.events.signal;
    const root = this.layout.root;

    this.layout.el('creator-back').addEventListener('click', () => this.back(), { signal });
    this.layout.el<HTMLSelectElement>('macro-terrain').addEventListener('change', () => this.formChanged(), { signal });
    this.layout.el<HTMLSelectElement>('macro-climate').addEventListener('change', () => this.formChanged(), { signal });
    this.layout.el<HTMLSelectElement>('calendar-mode').addEventListener('change', () => this.calendarChanged(), { signal });
    this.layout.el<HTMLSelectElement>('planet-view').addEventListener('change', () => {
      this.preview.atlas?.setPlanetView(this.layout.el<HTMLSelectElement>('planet-view').value as PlanetView);
    }, { signal });
    this.layout.el('map-btn-overview').addEventListener('click', () => {
      this.preview.atlas?.setPlanetView(this.layout.el<HTMLSelectElement>('planet-view').value as PlanetView);
      this.preview.atlas?.showOverview();
    }, { signal });

    // 种子与气候输入监听
    root.querySelectorAll<HTMLInputElement>('.world-settings input:not([type=file])').forEach(input => {
      if (input.closest('[data-layer-panel]')) return;
      input.addEventListener('input', () => this.formChanged(input), { signal });
    });

    this.layout.el('color-scheme-toggle').addEventListener('click', () => {
      const body = this.layout.el('color-scheme-body');
      const open = body.hidden;
      body.hidden = !open;
      this.layout.el('color-scheme-toggle').setAttribute('aria-expanded', String(open));
    }, { signal });
    this.layout.el('color-scheme-body').addEventListener('input', () => this.form.syncSchemeChip(), { signal });
    root.querySelectorAll<HTMLElement>('details.fold-group').forEach(group => {
      group.querySelector('summary')!.addEventListener('click', event => {
        if (group.classList.contains('locked')) event.preventDefault();
      }, { signal });
    });

    this.layout.el('random-seed').addEventListener('click', () => {
      this.form.randomizeSeed();
      this.preview.invalidate();
      this.layout.status(t('creator.seed.randomized'));
      this.layout.markStale(this.mapShown);
    }, { signal });

    root.querySelectorAll<HTMLButtonElement>('[data-preset]').forEach(button => {
      button.addEventListener('click', () => this.applyPreset(button.dataset.preset), { signal });
    });

    this.layout.el('browse-map').addEventListener('click', () => this.startPreview(), { signal });
    this.layout.el('footer-enter-btn').addEventListener('click', () => this.enterWorld(), { signal });

    // 地图顶部工具栏按钮
    this.layout.el('map-btn-spawn').addEventListener('click', () => this.preview.atlas?.centerSpawn(), { signal });
    this.layout.el('map-btn-reset').addEventListener('click', () => this.preview.atlas?.resetView(), { signal });
    this.layout.el('map-btn-zoom-in').addEventListener('click', () => this.preview.atlas?.zoomBy(1.25), { signal });
    this.layout.el('map-btn-zoom-out').addEventListener('click', () => this.preview.atlas?.zoomBy(0.8), { signal });
    this.layout.el('map-btn-expand').addEventListener('click', () => this.toggleExpandMap(), { signal });
  }
}
