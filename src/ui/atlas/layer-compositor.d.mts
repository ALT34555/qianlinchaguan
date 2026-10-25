import type {LayerId, LayerState} from './layers.mjs';

export interface LayerRenderContext {
  state: any;
  layer: LayerState;
  tiles?: any[];
  store?: any;
  focusId?: any;
  style?: string;
  key?: string;
  owner?: any;
  compositor: LayerCompositor;
  styleFor?: (id: LayerId) => string;
}

export interface LayerRenderer {
  readonly id: LayerId;
  readonly blendMode: GlobalCompositeOperation;
  draw(ctx: CanvasRenderingContext2D, context: LayerRenderContext): void;
  isInvalid?(prev: unknown, next: unknown): boolean;
  dispose?(): void;
}

export class LayerCompositor {
  projections: Map<string, {key: string; canvas: HTMLCanvasElement}>;
  renderers: Map<LayerId, LayerRenderer>;
  screenGrid?: any;
  daylight?: {key: string; canvas: HTMLCanvasElement};

  focused(style: string, tile: any, focusId?: any): string;
  projectLayer(id: LayerId | 'base' | 'backdrop', tiles: any[], state: any, key: string): HTMLCanvasElement;
  daylightImage(state: any, key: string, hours: number, world: any): HTMLCanvasElement;
  draw(ctx: CanvasRenderingContext2D, state: any, enabledLayers: LayerState[], context: any): void;
  dispose(): void;
}
