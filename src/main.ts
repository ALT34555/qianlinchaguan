import { mountModResources } from './modding_api/Resources';
import { applyAppearance } from './ui/Appearance';
import './ui/theme.css';
import './ui/style.css';
import './ui/start-screen.css';
import './ui/menu.css';
import './ui/simple.css';
import './ui/preferences.css';
import './ui/chat.css';
import { randomSeed } from './core/math/Random';
import { MIN_RENDER_DISTANCE, MAX_RENDER_DISTANCE } from './core/RenderDistance';
import { Game } from './core/Game';
import { StartScreen, type StartOptions } from './ui/StartScreen';
import { DEFAULT_CLIMATE_WEIGHTS, normalizeClimateWeights, normalizeGeneration } from './systems/world/WorldSettings';
import { parseWorldSave } from './systems/world/WorldSave';
import { loadSettings, saveSettings, SETTINGS_KEY } from './core/GameSettings';
import { FileSaveStore } from './systems/world/FileSaveStore';
import { initI18n, onLocaleChange, t } from './i18n';

const params = new URLSearchParams(location.search);
const rdRaw = Number(params.get('rd'));
const renderDistanceOverride = Number.isFinite(rdRaw) && rdRaw > 0 ? Math.max(MIN_RENDER_DISTANCE, Math.min(MAX_RENDER_DISTANCE, Math.round(rdRaw))) : undefined;
const menuRoot = document.getElementById('world-menu')!;
let started = false;
// 延迟访问浏览器存储
const desktopSettings = (window as Window & { desktopSettings?: { read(): string | null; write(value: string): void } }).desktopSettings;
const storage = {
  getItem: (key: string) => key === SETTINGS_KEY && desktopSettings ? desktopSettings.read() : localStorage.getItem(key),
  setItem: (key: string, value: string) => { if (key === SETTINGS_KEY && desktopSettings) desktopSettings.write(value); else localStorage.setItem(key, value); },
};

// 语言必须先于任何界面创建
initI18n({ storage, preferred: params.get('lang') });
// 开始界面只显示工程名
document.title = t('app.name');
const loadingEl = document.getElementById('loading')!;
loadingEl.textContent = t('loading.terrain');
onLocaleChange(() => {
  document.title = t('app.name');
  loadingEl.textContent = t('loading.terrain');
});

await mountModResources();
let settings = loadSettings(storage);
if (renderDistanceOverride !== undefined) settings = { ...settings, renderDistance: renderDistanceOverride };
applyAppearance(settings);
const saveStore = new FileSaveStore(fetch, '/api/saves', params.get('saveLocation') ?? 'userdata/saves');

function enterWorld(options: StartOptions): void {
  if (started) return;
  // 先验证出生点，失败时保持设置界面可用。
  const game = new Game(document.getElementById('app')!, {
    start: document.getElementById('start-screen')!,
    loading: document.getElementById('loading')!,
    waterTint: document.getElementById('water-tint')!,
  }, { ...options, initialPlayer: options.save?.player, showOverlay: params.get('overlay') === '1',
    settings, saveStore, persistSettings: value => { settings = saveSettings(storage, value); applyAppearance(settings); } });
  started = true;
  menu.destroy();
  menuRoot.classList.add('hidden');
  game.start();
}

let weights = normalizeClimateWeights(DEFAULT_CLIMATE_WEIGHTS);
let initialError = '';
try { if (params.has('climate')) weights = normalizeClimateWeights(params.get('climate')!.split(',').map(Number)); }
catch { initialError = t('error.climateParam'); }
let generation = normalizeGeneration();
try { if (params.has('generation')) generation = normalizeGeneration(JSON.parse(params.get('generation')!)); }
catch { initialError = t('error.generationParam'); }
const menu = new StartScreen(menuRoot, String(randomSeed()), weights, enterWorld, generation, {
  store: saveStore, getSettings: () => settings, applySettings: (value, persist = true) => {
    applyAppearance(value);
    if (persist) settings = saveSettings(storage, value);
  },
});
if (params.get('resume') === '1') {
  try {
    const raw = sessionStorage.getItem('loadWorldSave');
    sessionStorage.removeItem('loadWorldSave');
    const url = new URL(location.href); url.searchParams.delete('resume'); history.replaceState(null, '', url);
    if (raw) { const save = parseWorldSave(raw); enterWorld({ ...save, save }); }
  } catch (e) { initialError = e instanceof Error ? e.message : t('saves.readFailed'); }
}
if (initialError) document.getElementById('menu-status')!.textContent = initialError;
