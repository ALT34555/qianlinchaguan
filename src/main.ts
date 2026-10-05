import './ui/theme.css';
import './ui/style.css';
import './ui/start-screen.css';
import './ui/menu.css';
import './ui/simple.css';
import { DEFAULT_RENDER_DISTANCE, DEFAULT_SEED } from './core/config';
import { Game } from './core/Game';
import { StartScreen, type StartOptions } from './ui/StartScreen';
import { DEFAULT_CLIMATE_WEIGHTS, normalizeClimateWeights, normalizeGeneration } from './systems/world/WorldSettings';
import { parseWorldSave } from './systems/world/WorldSave';
import { loadSettings, saveSettings, SETTINGS_KEY } from './core/GameSettings';
import { LocalSaveStore } from './systems/world/LocalSaveStore';
import { FileSaveStore } from './systems/world/FileSaveStore';
import { initI18n, onLocaleChange, t } from './i18n';

const params = new URLSearchParams(location.search);
const rdRaw = Number(params.get('rd'));
const renderDistance = Number.isFinite(rdRaw) && rdRaw > 0 ? Math.max(2, Math.min(16, Math.round(rdRaw))) : DEFAULT_RENDER_DISTANCE;
const menuRoot = document.getElementById('world-menu')!;
let started = false;
// 延迟访问浏览器存储，禁用存储时仍可打开菜单、生成世界和导出文件。
const desktopSettings = (window as Window & { desktopSettings?: { read(): string | null; write(value: string): void } }).desktopSettings;
const storage = {
  getItem: (key: string) => key === SETTINGS_KEY && desktopSettings ? desktopSettings.read() : localStorage.getItem(key),
  setItem: (key: string, value: string) => { if (key === SETTINGS_KEY && desktopSettings) desktopSettings.write(value); else localStorage.setItem(key, value); },
};

// 语言必须先于任何界面创建：URL 的 ?lang= 参数 > 已保存偏好 > 浏览器语言 > 基准语言。
initI18n({ storage, preferred: params.get('lang') });
// 开始界面只显示工程名；进入游戏后 Game 会把标题改成「茜林茶馆 · 舆图 / 山川」。
document.title = t('app.name');
const loadingEl = document.getElementById('loading')!;
loadingEl.textContent = t('loading.terrain');
onLocaleChange(() => {
  document.title = t('app.name');
  loadingEl.textContent = t('loading.terrain');
});

let settings = loadSettings(storage);
const saveStore = new FileSaveStore(new LocalSaveStore(storage), fetch, '/api/saves', params.get('saveLocation') ?? 'userdata/saves');

function enterWorld(options: StartOptions): void {
  if (started) return;
  // 先验证出生点，失败时保持设置界面可用。
  const game = new Game(document.getElementById('app')!, {
    start: document.getElementById('start-screen')!,
    loading: document.getElementById('loading')!,
    waterTint: document.getElementById('water-tint')!,
  }, { ...options, initialPlayer: options.save?.player, renderDistance, showOverlay: params.get('overlay') === '1',
    settings, saveStore, persistSettings: value => { settings = saveSettings(storage, value); } });
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
const menu = new StartScreen(menuRoot, params.get('seed') ?? String(DEFAULT_SEED), weights, enterWorld, generation, {
  store: saveStore, getSettings: () => settings, applySettings: value => { settings = saveSettings(storage, value); },
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
