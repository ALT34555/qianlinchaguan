function syncRange(input: HTMLInputElement): void {
  const min = Number(input.min || 0);
  const max = Number(input.max || 100);
  const value = Number(input.value);
  const progress = max > min ? Math.max(0, Math.min(100, (value - min) / (max - min) * 100)) : 0;
  input.style.setProperty('--range-progress', `${progress}%`);
  const setting = input.dataset.rangeStops;
  const step = Number(input.step);
  const stops = setting === 'step' && step > 0 && (max - min) / step <= 24
    ? Array.from({ length: Math.floor((max - min) / step) + 1 }, (_, index) => min + index * step)
    : setting && setting !== 'step' ? setting.split(',').map(Number) : [max];
  const layers = [...new Set([...stops, max])]
    .filter(stop => Number.isFinite(stop) && stop > value + 1e-7 && stop <= max)
    .map(stop => {
      const ratio = ((stop - min) / (max - min)).toFixed(6);
      return `linear-gradient(var(--q-line) 0 0) calc(6.5px + (100% - 13px) * ${ratio}) center / 1px 11px no-repeat`;
    });
  input.style.setProperty('--range-ticks', layers.join(', ') || 'none');
}

function syncRanges(root: ParentNode): void {
  if (root instanceof HTMLInputElement && root.type === 'range') syncRange(root);
  root.querySelectorAll<HTMLInputElement>('input[type="range"]').forEach(syncRange);
}

export function initRanges(): void {
  syncRanges(document);
  document.addEventListener('input', event => {
    if (event.target instanceof HTMLInputElement && event.target.type === 'range') syncRange(event.target);
  });
  document.addEventListener('change', event => {
    if (event.target instanceof HTMLInputElement && event.target.type === 'range') syncRange(event.target);
  });
  new MutationObserver(records => {
    for (const record of records) {
      if (record.type === 'attributes') syncRanges(record.target as HTMLInputElement);
      else for (const node of record.addedNodes) if (node instanceof Element) syncRanges(node);
    }
  }).observe(document.body, { subtree: true, childList: true, attributes: true, attributeFilter: ['min', 'max', 'value'] });
}
