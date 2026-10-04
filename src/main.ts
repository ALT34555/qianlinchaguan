import './ui/style.css';
import './ui/start-screen.css';
import { DEFAULT_RENDER_DISTANCE, DEFAULT_SEED } from './core/config';
import { Game } from './core/Game';
import { StartScreen, type StartOptions } from './ui/StartScreen';
import { DEFAULT_CLIMATE_WEIGHTS, normalizeClimateWeights } from './systems/world/WorldSettings';
import { parseWorldSave } from './systems/world/WorldSave';

const params = new URLSearchParams(location.search);
const rdRaw = Number(params.get('rd'));
const renderDistance = Number.isFinite(rdRaw) && rdRaw > 0 ? Math.max(2, Math.min(16, Math.round(rdRaw))) : DEFAULT_RENDER_DISTANCE;
const menuRoot = document.getElementById('world-menu')!;
let started = false;

function enterWorld(options: StartOptions): void {
  if (started) return;
  // 先验证出生点，失败时保持设置界面可用。
  const game = new Game(document.getElementById('app')!, {
    start: document.getElementById('start-screen')!,
    loading: document.getElementById('loading')!,
    waterTint: document.getElementById('water-tint')!,
  }, { ...options, initialPlayer: options.save?.player, renderDistance, showOverlay: params.get('overlay') === '1' });
  started = true;
  menuRoot.classList.add('hidden');
  ['start-screen', 'loading', 'crosshair'].forEach(id => document.getElementById(id)!.classList.remove('hidden'));
  game.start();
}

let weights = normalizeClimateWeights(DEFAULT_CLIMATE_WEIGHTS);
let initialError = '';
try { if (params.has('climate')) weights = normalizeClimateWeights(params.get('climate')!.split(',').map(Number)); }
catch { initialError = '链接中的气候比例无效，已恢复均衡配比。'; }
new StartScreen(menuRoot, params.get('seed') ?? String(DEFAULT_SEED), weights, enterWorld);
if (params.get('resume') === '1') {
  try {
    const raw = sessionStorage.getItem('loadWorldSave');
    sessionStorage.removeItem('loadWorldSave');
    const url = new URL(location.href); url.searchParams.delete('resume'); history.replaceState(null, '', url);
    if (raw) { const save = parseWorldSave(raw); enterWorld({ ...save, save }); }
  } catch (e) { initialError = e instanceof Error ? e.message : '存档加载失败。'; }
}
if (initialError) document.getElementById('menu-status')!.textContent = initialError;
