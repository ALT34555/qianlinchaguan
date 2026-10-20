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
export function icon(name: string, className = 'ui-icon'): string { return `<img class="${className}" src="${iconUrl(name)}" alt="" draggable="false">`; }
export function padIcon(name: string, className = 'ui-icon'): string { return `<img class="${className}" src="${padIconUrl(name)}" alt="" draggable="false">`; }

