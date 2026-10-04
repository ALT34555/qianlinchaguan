/**
 * 区块 Worker 池：把生成任务分发给多个 Worker，限制每个 Worker 的并发数。
 */
import type { ClimateWeights } from './WorldSettings';
import type { ChunkResultMessage, WorkerRequest } from './ChunkProtocol';

export class ChunkWorkerPool {
  private readonly workers: Worker[] = [];
  private readonly inFlight: number[] = [];
  private readonly perWorker = 2;

  constructor(seed: number, climateWeights: ClimateWeights, private readonly onResult: (msg: ChunkResultMessage) => void, count?: number) {
    const n = count ?? Math.max(1, Math.min(4, (navigator.hardwareConcurrency || 4) - 1));
    for (let i = 0; i < n; i++) {
      const w = new Worker(new URL('./chunk.worker.ts', import.meta.url), { type: 'module' });
      const idx = i;
      w.onmessage = (e: MessageEvent<ChunkResultMessage>) => {
        this.inFlight[idx]--;
        this.onResult(e.data);
      };
      w.onerror = (e) => console.error('[ChunkWorkerPool] worker error', e);
      w.postMessage({ kind: 'init', seed, climateWeights } satisfies WorkerRequest);
      this.workers.push(w);
      this.inFlight.push(0);
    }
  }

  get capacity(): number {
    let free = 0;
    for (const f of this.inFlight) free += this.perWorker - f;
    return free;
  }

  /** 提交任务，若所有 Worker 都已满载则返回 false。 */
  request(cx: number, cz: number): boolean {
    let best = -1;
    for (let i = 0; i < this.workers.length; i++) {
      if (this.inFlight[i] < this.perWorker && (best < 0 || this.inFlight[i] < this.inFlight[best])) best = i;
    }
    if (best < 0) return false;
    this.inFlight[best]++;
    this.workers[best].postMessage({ kind: 'generate', cx, cz } satisfies WorkerRequest);
    return true;
  }

  dispose(): void {
    for (const w of this.workers) w.terminate();
    this.workers.length = 0;
  }
}
