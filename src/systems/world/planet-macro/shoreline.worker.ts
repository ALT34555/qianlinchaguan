import {installShorelineWorker} from './ShorelineWorkerRuntime';
import type {ShorelineWorkerRequest} from './ShorelineProtocol';
const port=self as unknown as Worker;
const receive=installShorelineWorker(port);
port.onmessage=(e:MessageEvent<ShorelineWorkerRequest>)=>receive(e.data);
