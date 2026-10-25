const SVG_NS = 'http://www.w3.org/2000/svg';

function makePart(tag: string, className: string, attributes: Record<string, string>): SVGElement {
  const element = document.createElementNS(SVG_NS, tag);
  element.setAttribute('class', className);
  for (const [name, value] of Object.entries(attributes)) element.setAttribute(name, value);
  return element;
}

function decorate(input: HTMLInputElement): void {
  if (input.classList.contains('q-checkbox-source') || input.classList.contains('switch-input')) return;
  if (input.closest('.save-filter-bookmark, .check')) return;

  const svg = makePart('svg', 'q-checkbox-art', { viewBox: '0 0 28 28', 'aria-hidden': 'true' });
  svg.append(
    makePart('rect', 'cb-f', { x: '0', y: '0', width: '28', height: '28' }),
    makePart('rect', 'cb-b', { x: '.75', y: '.75', width: '26.5', height: '26.5' }),
    makePart('path', 'cb-k', { d: 'M7 14.4 12.4 19.8 21.6 9' }),
  );
  input.classList.add('q-checkbox-source');
  input.insertAdjacentElement('afterend', svg);
}

function scan(root: ParentNode): void {
  if (root instanceof HTMLInputElement && root.type === 'checkbox') decorate(root);
  root.querySelectorAll<HTMLInputElement>('input[type="checkbox"]').forEach(decorate);
}

export function initCheckboxMotion(): void {
  scan(document.body);
  const observer = new MutationObserver(records => {
    for (const record of records) {
      for (const node of record.addedNodes) {
        if (node instanceof Element) scan(node);
      }
    }
  });
  observer.observe(document.body, { childList: true, subtree: true });
  document.addEventListener('change', event => {
    const input = event.target;
    if (!(input instanceof HTMLInputElement) || !input.classList.contains('q-checkbox-source')) return;
    const svg = input.nextElementSibling;
    if (!(svg instanceof SVGElement) || !svg.classList.contains('q-checkbox-art')) return;
    svg.classList.toggle('is-checking', input.checked);
    svg.classList.toggle('is-unchecking', !input.checked);
  }, true);
}
