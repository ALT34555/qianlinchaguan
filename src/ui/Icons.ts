const assets = import.meta.glob('../../content/assets/ui/icons/*.svg', { eager: true, query: '?url', import: 'default' }) as Record<string, string>;
export function iconUrl(name: string): string { return assets[`../../content/assets/ui/icons/${name}.svg`]; }
export function icon(name: string, className = 'ui-icon'): string { return `<img class="${className}" src="${iconUrl(name)}" alt="" draggable="false">`; }
