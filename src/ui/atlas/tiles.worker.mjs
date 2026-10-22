import {installTileWorker} from './tile-worker-runtime.mjs';
const receive=installTileWorker(self);
self.onmessage=({data})=>receive(data);
