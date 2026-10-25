import type { AtlasDisplayState } from '../../systems/world/WorldSave';
import { LAYER_DEFS, type LayerId } from '../atlas/layers.mjs';
import { layerDragIcon } from '../Icons';
import { t } from '../../i18n';

/** available 为假的图层不渲染行 */
export interface AtlasLayerPanelOptions {
  available?: (id: LayerId) => boolean;
}

export function mountAtlasLayerPanel(root: HTMLElement, state: AtlasDisplayState, commit: () => void,
  options: AtlasLayerPanelOptions = {}): () => void {
  const panel = root.querySelector<HTMLElement>('[data-layer-panel]');
  const list = root.querySelector<HTMLElement>('[data-layer-list]');
  if (!panel || !list) return () => {};
  const events = new AbortController();
  let dragging: AbortController | undefined;
  const toggle=root.querySelector<HTMLButtonElement>('[data-layers]');
  toggle?.setAttribute('aria-expanded','false');
  toggle?.addEventListener('click', () => {
    panel.classList.toggle('hidden');
    toggle.setAttribute('aria-expanded',String(!panel.classList.contains('hidden')));
  }, { signal: events.signal });

  const shown = (id: LayerId): boolean => options.available?.(id) !== false;

  const paint = (): void => {
    dragging?.abort();dragging=undefined;
    list.innerHTML = '';
    for (const layer of state.layers) {
      if (!shown(layer.id)) continue;
      const def = LAYER_DEFS.find(d => d.id === layer.id)!;
      const row = document.createElement('div');
      row.className = `map-layer-row${layer.on ? '' : ' is-off'}`;
      row.dataset.id = layer.id;
      const slider = def.opacity ? `<input type="range" min="0" max="100" step="1" data-opacity value="${Math.round(layer.opacity * 100)}" aria-label="${t('layer.opacity')}">` : '';
      row.innerHTML = `<button type="button" class="icon-btn map-layer-drag" aria-label="${t('layer.drag')} ${t(`layer.${layer.id}`)}">${layerDragIcon()}</button>
        <label class="map-layer-check"><input type="checkbox" ${layer.on ? 'checked' : ''}><span>${t(`layer.${layer.id}`)}</span></label>
        ${slider}`;
      list.append(row);

      const id = layer.id;
      row.querySelector<HTMLInputElement>('input[type=checkbox]')?.addEventListener('change', event => {
        const target = state.layers.find(l => l.id === id)!;
        target.on = (event.target as HTMLInputElement).checked;
        row.classList.toggle('is-off', !target.on);
        commit();
      });
      const opacity = row.querySelector<HTMLInputElement>('[data-opacity]');
      opacity?.addEventListener('input', () => {
        state.layers.find(l => l.id === id)!.opacity = Number(opacity.value) / 100;
        commit();
      });
      const move=(targetId:string):void=>{
        if(targetId===id)return;
        const layers = state.layers;
        const from = layers.findIndex(l => l.id === id);
        const to = layers.findIndex(l => l.id === targetId);
        if (from < 0 || to < 0) return;
        const [moved] = layers.splice(from, 1);
        layers.splice(to, 0, moved);
        commit();
        paint();
        list.querySelector<HTMLButtonElement>(`[data-id="${id}"] .map-layer-drag`)?.focus({preventScroll:true});
      };
      const handle=row.querySelector<HTMLButtonElement>('.map-layer-drag')!;
      let pointer:number|null=null,targetId=id;
      handle.addEventListener('pointerdown',event=>{
        if(event.button!==0||pointer!==null||dragging)return;
        event.preventDefault();
        pointer=event.pointerId;targetId=id;
        row.classList.add('is-dragging');
        dragging=new AbortController();
        document.addEventListener('pointermove',track,{signal:dragging.signal});
        document.addEventListener('pointerup',finish,{signal:dragging.signal});
        document.addEventListener('pointercancel',finish,{signal:dragging.signal});
      });
      const track=(event:PointerEvent):void=>{
        if(event.pointerId!==pointer)return;
        const target=document.elementFromPoint(event.clientX,event.clientY)?.closest<HTMLElement>('.map-layer-row');
        if(!target||!list.contains(target))return;
        targetId=target.dataset.id as typeof id;
        for(const item of list.children)item.classList.toggle('is-drop-target',item===target&&target!==row);
      };
      const finish=(event:PointerEvent):void=>{
        if(event.pointerId!==pointer)return;
        dragging?.abort();dragging=undefined;
        pointer=null;row.classList.remove('is-dragging');
        for(const item of list.children)item.classList.remove('is-drop-target');
        if(event.type==='pointerup')move(targetId);
      };
      handle.addEventListener('keydown',event=>{
        if(event.key!=='ArrowUp'&&event.key!=='ArrowDown')return;
        event.preventDefault();
        const visible=state.layers.filter(l=>shown(l.id));
        const index=visible.findIndex(l=>l.id===id);
        const next=visible[index+(event.key==='ArrowUp'?-1:1)];
        if(next)move(next.id);
      });
    }
  };
  paint();
  return () => { events.abort(); dragging?.abort(); list.replaceChildren(); };
}
