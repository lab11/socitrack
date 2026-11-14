// The log worker, from the page's side.
//
// One worker, kept alive and shared: spinning one up per file costs more than parsing a small one, and
// it holds each log's ranging rounds between requests (logAnalysis.ts). Requests are tagged so several
// can be in flight without their replies being confused.
//
// Falls back to running in place where workers are unavailable. That path blocks, but a blocked page
// beats a page that cannot read a file at all, and it runs exactly the same code.

import type { DailyOptions, DailyRow, DeploymentHealth, LogCharts, ParseReport, RadioSummary, Series } from '@tottag/schema';
import { handle, rehydrateHealth, type AnalysisReply, type AnalysisRequest, type ParseReply } from './logAnalysis.ts';

export interface ParsedLog {
  readonly report: ParseReport;
  readonly recordCount: number;
  readonly health: DeploymentHealth;
  /** Experiment start in Unix seconds — what a downloaded log is named after. */
  readonly experimentStartTime: number;
  /** Page sequence numbers worth asking the device to resend. Empty when the log is whole. */
  readonly missingSeqs: readonly number[];
  /** What the radio check needs from this log, since the records themselves never leave the worker. */
  readonly radio: RadioSummary;
  /** Everything the log's charts draw, already reduced. */
  readonly charts: LogCharts;
  /** Peers the worker holds every round for, and the span of those rounds. */
  readonly rangePeers: readonly number[];
  readonly rangeSpan: readonly [number, number] | null;
}

let worker: Worker | null = null;
let nextId = 1;
const pending = new Map<number, { resolve: (reply: AnalysisReply) => void; reject: (error: Error) => void }>();

function ensureWorker(): Worker | null {
  if (worker) return worker;
  if (typeof Worker === 'undefined') return null;
  try {
    worker = new Worker(new URL('./log-parser.worker.ts', import.meta.url), { type: 'module' });
  } catch {
    return null;
  }
  worker.onmessage = (event: MessageEvent<{ id: number; reply?: AnalysisReply; error?: string }>) => {
    const { id, reply, error } = event.data;
    const waiting = pending.get(id);
    if (!waiting) return;
    pending.delete(id);
    if (error !== undefined || !reply) waiting.reject(new Error(error ?? 'The log reader gave no answer'));
    else waiting.resolve(reply);
  };
  // A worker that dies takes every in-flight request, and every log's ranging rounds, with it. Failing
  // the requests explicitly means the caller shows an error rather than waiting forever.
  worker.onerror = () => {
    for (const waiting of pending.values()) waiting.reject(new Error('The log reader stopped unexpectedly'));
    pending.clear();
    worker?.terminate();
    worker = null;
  };
  return worker;
}

function call(request: AnalysisRequest, transfer: ArrayBuffer[] = []): Promise<AnalysisReply> {
  const active = ensureWorker();
  if (!active) {
    try {
      return Promise.resolve(handle(request).reply);
    } catch (error) {
      return Promise.reject(error instanceof Error ? error : new Error(String(error)));
    }
  }
  const id = nextId++;
  return new Promise<AnalysisReply>((resolve, reject) => {
    pending.set(id, { resolve, reject });
    active.postMessage({ id, request }, transfer);
  });
}

/** Parse and analyse a log; `key` names it for later requests about its ranging. */
export async function parseLogAsync(bytes: Uint8Array, key: string): Promise<ParsedLog> {
  // Copy before transferring: the caller keeps its own bytes, which the UI still needs for re-export.
  const buffer = bytes.slice().buffer;
  const reply = await call({ kind: 'parse', key, bytes: buffer }, [buffer]) as ParseReply;
  return {
    report: reply.report,
    recordCount: reply.recordCount,
    health: rehydrateHealth(reply.health),
    experimentStartTime: reply.experimentStartTime,
    missingSeqs: reply.missingSeqs,
    radio: reply.radio,
    charts: reply.charts,
    rangePeers: reply.rangePeers,
    rangeSpan: reply.rangeSpan,
  };
}

/** Every distance from one log to one peer between two instants, thinned to about `buckets`. Null when the log's rounds are no longer held. */
export async function requestRangeWindow(key: string, peer: number, from: number, to: number, buckets: number): Promise<Series | null> {
  const reply = await call({ kind: 'window', key, peer, from, to, buckets });
  return reply.kind === 'window' ? reply.series : null;
}

/** Daily time-together figures for one log. Null when the log's rounds are no longer held. */
export async function requestDailyStats(key: string, options: DailyOptions): Promise<readonly DailyRow[] | null> {
  const reply = await call({ kind: 'daily', key, options });
  return reply.kind === 'daily' ? reply.rows : null;
}

/** Let go of logs' ranging rounds, or of every log's. */
export function releaseLogs(keys: readonly string[] | 'all'): void {
  void call({ kind: 'release', keys }).catch(() => undefined);
}
