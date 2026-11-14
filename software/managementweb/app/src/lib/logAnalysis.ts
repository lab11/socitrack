// What the log worker does, written once so the worker and the in-page fallback do exactly the same.
//
// No DOM and no worker API here: the worker wires `handle` to its messages, and where workers are
// unavailable parseAsync.ts calls it directly. Both keep the same registry of range stores.
//
// Memory. Parsing a worst-case week holds about 757 MB of records for the moment it takes; nothing here
// keeps them. What stays behind, per log, is its range store (rangeStore.ts): 4 + 2 × peers bytes per
// ranging round, 26 MB for a worst-case week and under 7 MB for a typical one. The page gets the
// charts' series (a few hundred kilobytes) and never the store; it asks for windows and daily figures,
// which come back small. The registry is capped, dropping the store least recently asked for, so a page
// with dozens of week-long logs open cannot grow without bound; a view that needs a dropped store says so.

import {
  analyseDeployment, buildLogCharts, buildRangeStore, chartBuffers, dailyRangingStats, missingSeqs, parse,
  rangeStoreBytes, rangeStoreSpan, rangeWindow, summarizeRadio,
  type DailyOptions, type DailyRow, type DeploymentHealth, type LogCharts, type ParseReport, type RadioSummary,
  type RangeStore, type Series,
} from '@tottag/schema';

/** Range stores kept at once, in bytes, before the least recently used is dropped. */
export const STORE_BUDGET_BYTES = 400 * 1024 * 1024;

export type AnalysisRequest =
  | { readonly kind: 'parse'; readonly key: string; readonly bytes: ArrayBuffer }
  | { readonly kind: 'window'; readonly key: string; readonly peer: number; readonly from: number; readonly to: number; readonly buckets: number }
  | { readonly kind: 'daily'; readonly key: string; readonly options: DailyOptions }
  | { readonly kind: 'release'; readonly keys: readonly string[] | 'all' };

/** A log's parse, flattened for the trip across the worker boundary. */
export interface ParseReply {
  readonly kind: 'parse';
  readonly report: ParseReport;
  readonly recordCount: number;
  readonly experimentStartTime: number;
  readonly missingSeqs: number[];
  readonly radio: RadioSummary;
  readonly health: Record<string, unknown>;
  readonly charts: LogCharts;
  /** Peers the log ranged to, and the span of its rounds, so views can offer them without asking. */
  readonly rangePeers: readonly number[];
  readonly rangeSpan: readonly [number, number] | null;
  readonly storeBytes: number;
}

export type AnalysisReply =
  | ParseReply
  | { readonly kind: 'window'; readonly series: Series | null }
  | { readonly kind: 'daily'; readonly rows: readonly DailyRow[] | null }
  | { readonly kind: 'release' };

/** The registry: insertion order is recency, so the first key is the one to drop. */
const stores = new Map<string, RangeStore>();

function remember(key: string, store: RangeStore): void {
  stores.delete(key);
  stores.set(key, store);
  let total = 0;
  for (const kept of stores.values()) total += rangeStoreBytes(kept);
  for (const [oldest, kept] of stores) {
    if (total <= STORE_BUDGET_BYTES || oldest === key) break;
    total -= rangeStoreBytes(kept);
    stores.delete(oldest);
  }
}

function recall(key: string): RangeStore | null {
  const store = stores.get(key);
  if (!store) return null;
  stores.delete(key);
  stores.set(key, store);
  return store;
}

/** Maps survive structured cloning, but flattening keeps the reply plain and the boundary explicit. */
export function flattenHealth(health: DeploymentHealth): Record<string, unknown> {
  return {
    ...health,
    recordCounts: [...health.recordCounts],
    rangingPeers: [...health.rangingPeers],
    nearMisses: {
      ...health.nearMisses,
      watchdogLate: [...health.nearMisses.watchdogLate],
      lowestStackWords: [...health.nearMisses.lowestStackWords],
    },
  };
}

/** Rebuild the Maps flattenHealth took apart. */
export function rehydrateHealth(health: Record<string, unknown>): DeploymentHealth {
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

/** Answer one request, with the buffers that can be moved rather than copied. */
export function handle(request: AnalysisRequest): { reply: AnalysisReply; transfer: ArrayBuffer[] } {
  switch (request.kind) {
    case 'parse': {
      const result = parse(new Uint8Array(request.bytes));
      const health = analyseDeployment(result);
      const charts = buildLogCharts(result);
      const store = buildRangeStore(result);
      remember(request.key, store);
      return {
        reply: {
          kind: 'parse',
          report: result.report,
          recordCount: result.records.length,
          experimentStartTime: result.experimentStartTime,
          missingSeqs: missingSeqs(result.report),
          radio: summarizeRadio(result),
          health: flattenHealth(health),
          charts,
          rangePeers: store.peers,
          rangeSpan: rangeStoreSpan(store),
          storeBytes: rangeStoreBytes(store),
        },
        transfer: chartBuffers(charts),
      };
    }
    case 'window': {
      const store = recall(request.key);
      const series = store ? rangeWindow(store, request.peer, request.from, request.to, request.buckets) : null;
      return { reply: { kind: 'window', series }, transfer: series ? [series.t.buffer, series.v.buffer] as ArrayBuffer[] : [] };
    }
    case 'daily': {
      const store = recall(request.key);
      return { reply: { kind: 'daily', rows: store ? dailyRangingStats(store, request.options) : null }, transfer: [] };
    }
    case 'release': {
      if (request.keys === 'all') stores.clear();
      else for (const key of request.keys) stores.delete(key);
      return { reply: { kind: 'release' }, transfer: [] };
    }
  }
}
