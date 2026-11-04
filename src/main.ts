import { mountModResources } from './modding_api/Resources';
import { applyAppearance } from './ui/Appearance';
import './ui/theme.css';
import './ui/style.css';
import './ui/start-screen.css';
import './ui/menu.css';
import './ui/simple.css';
import './ui/preferences.css';
import './ui/chat.css';
// 令牌层最后引入覆盖别名以换肤
import './ui/tokens.css';
// 茶席视觉层排在旧样式之后
import './ui/adapt/tokens.css';
import './ui/adapt/base.css';
import './ui/adapt/components.css';
import './ui/adapt/scenes.css';
import './ui/adapt/motion.css';
import './ui/adapt/teasheet.css';
import './ui/adapt/bookmarks.css';
import './ui/adapt/in-game.css';
import './ui/adapt/buttons.css';
import './ui/adapt/ranges.css';
import './ui/adapt/checkbox-motion.css';
import { initVisuals, showLoading } from './ui/adapt/runtime';
import { initRanges } from './ui/adapt/ranges';
import { initCheckboxMotion } from './ui/adapt/checkbox-motion';
import { randomSeed } from './core/math/Random';
import { MIN_RENDER_DISTANCE, MAX_RENDER_DISTANCE } from './core/RenderDistance';
import { Game } from './core/Game';
import { StartScreen, type StartOptions } from './ui/StartScreen';
import { DEFAULT_CLIMATE_WEIGHTS, normalizeClimateWeights, normalizeGeneration } from './systems/world/WorldSettings';
import { parseWorldSave } from './systems/world/WorldSave';
import { loadSettings, saveSettings, SETTINGS_KEY } from './core/GameSettings';
import './core/DesktopBridge';
import { resolveSaveStore } from './systems/world/SaveBackend';
import { initI18n, onLocaleChange, t } from './i18n';
import { probeDisplayRefresh, overrideDisplayRefreshRate } from './ui/DisplayRefresh';
import { prepareWorld } from './systems/world/PrepareWorld';

async function bootstrap(): Promise<void> {
const params = new URLSearchParams(location.search);
const rdRaw = Number(params.get('rd'));
const renderDistanceOverride = Number.isFinite(rdRaw) && rdRaw > 0 ? Math.max(MIN_RENDER_DISTANCE, Math.min(MAX_RENDER_DISTANCE, Math.round(rdRaw))) : undefined;
const menuRoot = document.getElementById('world-menu')!;
let started = false;
// 延迟访问浏览器存储
const desktopSettings = window.desktopSettings;
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
// 视觉层先就位，首屏即用茶席配色
await initVisuals();
initRanges();
initCheckboxMotion();
const refreshOverride = Number(params.get('refresh'));
if (Number.isFinite(refreshOverride) && refreshOverride > 0) overrideDisplayRefreshRate(refreshOverride);
else probeDisplayRefresh();
let settings = loadSettings(storage);
if (renderDistanceOverride !== undefined) settings = { ...settings, renderDistance: renderDistanceOverride };
applyAppearance(settings);
const desktop = params.get('desktop') === '1';
if (desktop && (!desktopSettings || !window.desktopRuntime)) throw new Error(t('startup.bridgeUnavailable'));
const saveStore = await resolveSaveStore(desktop, params.get('saveLocation') ?? 'userdata/saves');

async function enterWorld(options: StartOptions): Promise<void> {
  if (started) return;
  started=true;
  // 段起始/步数/已完成，用于把消息折算成环形进度
  let stageStart = 0, stageSteps = 0, stageDone = 0;
  const loading=showLoading({title:t('loading.terrain')});
  const progress=()=>stageStart+(stageSteps>0?stageDone/stageSteps:0)*.9;
  const wave=(from:number,steps:number)=>{stageStart=from;stageSteps=steps;stageDone=0;};
  wave(.04,4);
  const ticker=window.setInterval(()=>{if(stageDone<stageSteps)stageDone+=1;},260);
  try{
  const worldKey=options.save?.worldKey??options.worldKey??crypto.randomUUID();
  const runtimeOptions={ ...options,worldKey,initialPlayer:options.save?.player,showOverlay:params.get('overlay')==='1',settings,saveStore,
    persistSettings:(value:typeof settings)=>{settings=saveSettings(storage,value);applyAppearance(settings);} };
  const prepared=await prepareWorld(runtimeOptions,message=>{wave(.26,10);loading.set(message,progress);});
  loading.set(t('loading.terrain'),progress);
  // 先验证出生点，失败时保持设置界面可用。
  const game = new Game(document.getElementById('app')!, {
    start: document.getElementById('start-screen')!,
    loading: document.getElementById('loading')!,
    waterTint: document.getElementById('water-tint')!,
  }, { ...options,worldKey,prepared, initialPlayer: options.save?.player, showOverlay: params.get('overlay') === '1',
    settings, saveStore, persistSettings: value => { settings = saveSettings(storage, value); applyAppearance(settings); } });
  started = true;
  menu.destroy();
  menuRoot.classList.add('hidden');
  game.start();
  loadingEl.classList.add('hidden');
  await loading.done();
  }catch(error){started=false;loading.cancel();loadingEl.classList.add('hidden');document.getElementById('menu-status')!.textContent=error instanceof Error?error.message:String(error);}
  finally{window.clearInterval(ticker);}
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
window.desktopRuntime?.ready();
}

void bootstrap().catch(error => {
  const message = error instanceof Error ? error.message : String(error);
  window.desktopRuntime?.failed(message);
  document.getElementById('loading')?.classList.add('hidden');
  const root = document.getElementById('world-menu')!;
  const status = document.createElement('p');
  status.setAttribute('role', 'alert'); status.textContent = `${t('startup.failed')} ${message}`;
  const retry = document.createElement('button');
  retry.className = 'btn btn--ghost'; retry.textContent = t('startup.retry');
  retry.addEventListener('click', () => location.reload());
  root.replaceChildren(status, retry); root.classList.remove('hidden');
});
