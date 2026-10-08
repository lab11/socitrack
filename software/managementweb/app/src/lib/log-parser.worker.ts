// Parsing and analysis, off the main thread.
//
// The fixtures in this repo are 26 KB and parse instantly, which is exactly why this is easy to
// skip. A real deployment file is measured in megabytes — the 3.9-day capture offloaded 1.0 MB per
// device, and a two-week run is several times that — and parsing one synchronously locks the page
// for as long as it takes. A frozen tab is indistinguishable from a crashed one to the person
// looking at it.

import { analyseDeployment, missingSeqs, parse, summarizeRadio } from '@tottag/schema';

export interface ParseRequest {
  readonly id: number;
  readonly bytes: ArrayBuffer;
}

self.onmessage = (event: MessageEvent<ParseRequest>) => {
  const { id, bytes } = event.data;
  try {
    const result = parse(new Uint8Array(bytes));
    const health = analyseDeployment(result);
    // Maps survive structured cloning, but flattening here keeps the reply a plain object and makes
    // the boundary explicit rather than relying on the algorithm's finer points.
    self.postMessage({
      id,
      report: result.report,
      recordCount: result.records.length,
      experimentStartTime: result.experimentStartTime,
      // Computed here because the caller's repair loop needs it and the report is large
      missingSeqs: missingSeqs(result.report),
      // The records stay in this worker, so the radio check gets their per-minute summary instead
      radio: summarizeRadio(result),
      health: {
        ...health,
        recordCounts: [...health.recordCounts],
        rangingPeers: [...health.rangingPeers],
        nearMisses: {
          ...health.nearMisses,
          watchdogLate: [...health.nearMisses.watchdogLate],
          lowestStackWords: [...health.nearMisses.lowestStackWords],
        },
      },
    });
  } catch (error) {
    self.postMessage({ id, error: error instanceof Error ? error.message : String(error) });
  }
};
