// `parse` + `analyseDeployment`, run in a worker.
//
// One worker, kept alive and shared: spinning one up per file costs more than parsing a small one,
// and the module holds no state between calls. Requests are tagged so several files can be in
// flight without their replies being confused.
//
// Falls back to parsing in place where workers are unavailable. That path blocks, but a blocked
// page beats a page that cannot read a file at all.

import { analyseDeployment, missingSeqs, parse, summarizeRadio, type DeploymentHealth, type ParseReport, type RadioSummary } from '@tottag/schema';

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
}

interface WorkerReply {
  id: number;
  report?: ParseReport;
  recordCount?: number;
  experimentStartTime?: number;
  missingSeqs?: number[];
  radio?: RadioSummary;
  health?: Record<string, unknown>;
  error?: string;
}

let worker: Worker | null = null;
let nextId = 1;
const pending = new Map<number, { resolve: (log: ParsedLog) => void; reject: (error: Error) => void }>();

/** Rebuild the Maps the worker flattened for transport. */
function rehydrate(health: Record<string, unknown>): DeploymentHealth {
  const nearMisses = health.nearMisses as Record<string, unknown>;
  return {
    ...health,
    recordCounts: new Map(health.recordCounts as Array<[string, number]>),
    rangingPeers: new Map(health.rangingPeers as Array<[number, number]>),
    nearMisses: {
      ...nearMisses,
      watchdogLate: new Map(nearMisses.watchdogLate as Array<[string, number]>),
      lowestStackWords: new Map(nearMisses.lowestStackWords as Array<[string, number]>),
    },
  } as unknown as DeploymentHealth;
}

function ensureWorker(): Worker | null {
  if (worker) return worker;
  if (typeof Worker === 'undefined') return null;
  try {
    worker = new Worker(new URL('./log-parser.worker.ts', import.meta.url), { type: 'module' });
  } catch {
    return null;
  }
  worker.onmessage = (event: MessageEvent<WorkerReply>) => {
    const reply = event.data;
    const waiting = pending.get(reply.id);
    if (!waiting) return;
    pending.delete(reply.id);
    if (reply.error !== undefined) waiting.reject(new Error(reply.error));
    else {
      waiting.resolve({
        report: reply.report!,
        recordCount: reply.recordCount!,
        health: rehydrate(reply.health!),
        experimentStartTime: reply.experimentStartTime!,
        missingSeqs: reply.missingSeqs!,
        radio: reply.radio!,
      });
    }
  };
  // A worker that dies takes every in-flight request with it. Failing them explicitly means the
  // caller shows an error rather than waiting on a promise that never settles.
  worker.onerror = () => {
    for (const waiting of pending.values()) waiting.reject(new Error('The log reader stopped unexpectedly'));
    pending.clear();
    worker?.terminate();
    worker = null;
  };
  return worker;
}

export async function parseLogAsync(bytes: Uint8Array): Promise<ParsedLog> {
  const active = ensureWorker();
  if (!active) {
    const result = parse(bytes);
    return {
      report: result.report,
      recordCount: result.records.length,
      health: analyseDeployment(result),
      experimentStartTime: result.experimentStartTime,
      missingSeqs: missingSeqs(result.report),
      radio: summarizeRadio(result),
    };
  }
  const id = nextId++;
  // Copy before transferring: the caller keeps its own bytes, which the UI still needs for re-export.
  const buffer = bytes.slice().buffer;
  return new Promise<ParsedLog>((resolve, reject) => {
    pending.set(id, { resolve, reject });
    active.postMessage({ id, bytes: buffer }, [buffer]);
  });
}
