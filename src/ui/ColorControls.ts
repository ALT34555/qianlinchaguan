import { hexColor } from '../systems/world/artificial/PlayerFaction';
import { t } from '../i18n';

export function colorControl(id: string, label: string, value: string): string {
  const channels = ['R', 'G', 'B'];
  return `<fieldset class="faction-color" data-color="${id}"><legend>${label}</legend>
    <label class="color-hex"><span data-swatch style="background:${value}"></span><span>HEX</span><input id="${id}" value="${value}" maxlength="7" aria-label="${label} HEX" autocomplete="off"></label>
    ${channels.map((c, i) => `<label class="color-channel"><span>${c}</span><input data-channel="${i}" type="range" min="0" max="255" step="1" value="${parseInt(value.slice(1 + i * 2, 3 + i * 2), 16)}" aria-label="${label} ${c}"><output></output></label>`).join('')}
    <small data-color-error role="status"></small></fieldset>`;
}

export function bindColorControls(root: HTMLElement): () => void {
  const events = new AbortController();
  root.querySelectorAll<HTMLElement>('[data-color]').forEach(group => {
    const hex = group.querySelector<HTMLInputElement>('.color-hex input')!;
    const channels = [...group.querySelectorAll<HTMLInputElement>('[data-channel]')];
    const swatch = group.querySelector<HTMLElement>('[data-swatch]')!;
    const error = group.querySelector<HTMLElement>('[data-color-error]')!;
    const sync = (canonical = false) => {
      try {
        const color = hexColor(hex.value);
        if (canonical) hex.value = color;
        channels.forEach((input, i) => { input.value = String(parseInt(color.slice(1 + i * 2, 3 + i * 2), 16)); });
        swatch.style.background = color; hex.setCustomValidity(''); error.textContent = '';
      } catch { hex.setCustomValidity(t('player.colorError')); error.textContent = t('player.colorError'); }
      channels.forEach(input => { input.parentElement!.querySelector('output')!.textContent = input.value; });
    };
    hex.addEventListener('input', () => sync(), { signal: events.signal });
    hex.addEventListener('change', () => sync(true), { signal: events.signal });
    channels.forEach(input => input.addEventListener('input', () => {
      hex.value = '#' + channels.map(c => Number(c.value).toString(16).padStart(2, '0')).join(''); sync();
    }, { signal: events.signal }));
    sync();
  });
  return () => events.abort();
}
