import menuLock from '../../content/assets/ui/icons/menu-lock.svg?raw';
import layerDrag from '../../content/assets/ui/icons/layer-drag.svg?raw';

const assets = import.meta.glob('../../content/assets/ui/icons/*.svg', { eager: true, query: '?url', import: 'default' }) as Record<string, string>;
const padAssets = import.meta.glob('../../content/assets/ui/control_pad/*.svg', { eager: true, query: '?url', import: 'default' }) as Record<string, string>;

// 统一解析图标URL
export function iconUrl(name: string): string {
  return assets[`../../content/assets/ui/icons/${name}.svg`] ?? padAssets[`../../content/assets/ui/control_pad/${name}.svg`];
}
// 优先解析控制盘图标
export function padIconUrl(name: string): string {
  return padAssets[`../../content/assets/ui/control_pad/${name}.svg`] ?? assets[`../../content/assets/ui/icons/${name}.svg`];
}
export function icon(name: string, className = 'ui-icon'): string {
  if(name==='menu-lock')return menuLock.replace(/<\?xml[^>]*>\s*/,'').replace(/\.i-/g,'.ui-lock-icon .i-').replace(/<style>[\s\S]*?<\/style>/,'')
    .replace('<svg ',`<svg class="ui-lock-icon ${className}" aria-hidden="true" style="--li-ink:var(--q-ink);--li-acc:var(--q-primary-ui);--li-plane:var(--q-ink);--li-paper:var(--q-surface)" `);
  return `<img class="${className}" src="${iconUrl(name)}" alt="" draggable="false">`;
}
export function padIcon(name: string, className = 'ui-icon'): string { return `<img class="${className}" src="${padIconUrl(name)}" alt="" draggable="false">`; }


/** 图层拖拽点：红点跟主色同步 */
export function layerDragIcon(className='map-layer-drag-icon'){
  return layerDrag.replace(/<\?xml[^>]*>\s*/,'').replace(/<style>[\s\S]*?<\/style>/,'')
    .replace('<svg ',`<svg class="${className}" aria-hidden="true" `);
}
