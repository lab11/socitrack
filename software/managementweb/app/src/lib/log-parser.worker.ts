// Parsing and analysis, off the main thread.
//
// The fixtures in this repo are 26 KB and parse instantly, which is exactly why this is easy to
// skip. A real deployment file is measured in megabytes — the 3.9-day capture offloaded 1.0 MB per
// device, and a two-week run is several times that — and parsing one synchronously locks the page
// for as long as it takes. A frozen tab is indistinguishable from a crashed one to the person
// looking at it.
//
// The worker also keeps each log's ranging rounds (logAnalysis.ts says how much that costs), so the
// views that need every round can ask for a window or a day's figures without the page holding them.

import { handle, type AnalysisRequest } from './logAnalysis.ts';

self.onmessage = (event: MessageEvent<{ id: number; request: AnalysisRequest }>) => {
  const { id, request } = event.data;
  try {
    const { reply, transfer } = handle(request);
    self.postMessage({ id, reply }, { transfer });
  } catch (error) {
    self.postMessage({ id, error: error instanceof Error ? error.message : String(error) });
  }
};
