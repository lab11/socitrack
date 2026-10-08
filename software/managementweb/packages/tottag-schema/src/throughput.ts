// Estimating how long a transfer has left, from how it is actually going.
//
// This replaces a hardcoded 63 kB/s figure measured once on one link. That number was never going
// to be right for everyone: throughput depends on the host's Bluetooth radio and stack, the
// negotiated connection interval, how many packets per event the controller manages, distance, and
// whatever else is sharing 2.4 GHz. An old laptop and a new one differ by more than the margin the
// constant claimed.
//
// Three things make a measured estimate behave rather than jitter:
//
//   1. A WARM-UP. The first moments of a transfer include MTU exchange and a connection-parameter
//      update, so early samples are unrepresentatively slow. Reporting from them produces a
//      frightening first estimate that immediately halves.
//   2. A SLIDING WINDOW, not a running total. A total average cannot fall: if the link degrades
//      half way through, a total-average estimate keeps promising the old rate right up until it
//      is wrong. A window follows the link.
//   3. AN EXPLICIT "not yet" STATE. Returning a number before there is evidence for one is how a
//      progress bar earns distrust, and a researcher who has learned to ignore the estimate has
//      lost the thing it was for.

/** Ignore the first samples; they include MTU exchange and the connection-parameter update. */
export const THROUGHPUT_WARMUP_MS = 1_500;
/** How much history the rate is computed over. Long enough to be steady, short enough to react. */
export const THROUGHPUT_WINDOW_MS = 4_000;
/** Below this there is not enough evidence to quote a rate. */
const MIN_SAMPLES = 3;

export interface ThroughputSample {
  readonly atMs: number;
  readonly bytes: number;
}

export interface ThroughputEstimate {
  /** Bytes per second over the recent window, or null while still warming up. */
  readonly bytesPerSecond: number | null;
  /** Seconds remaining, or null when the rate or the total is unknown. */
  readonly secondsRemaining: number | null;
  /** 0..1, or null when the total is unknown. */
  readonly fraction: number | null;
  /** True while there is not yet enough evidence to quote a rate. */
  readonly measuring: boolean;
}

/**
 * Accumulates progress samples and reports a windowed rate.
 *
 * Deliberately has no clock of its own: the caller passes the time with each sample. That keeps it
 * pure and lets the tests drive it through a whole transfer in microseconds.
 */
export class ThroughputMeter {
  private readonly samples: ThroughputSample[] = [];
  private startedAtMs: number | null = null;

  /** Record cumulative bytes received at a moment in time. */
  record(atMs: number, cumulativeBytes: number): void {
    if (this.startedAtMs === null) this.startedAtMs = atMs;
    this.samples.push({ atMs, bytes: cumulativeBytes });
    // Keep one sample older than the window so the window is always fully spanned.
    const cutoff = atMs - THROUGHPUT_WINDOW_MS;
    let drop = 0;
    while (drop + 1 < this.samples.length && this.samples[drop + 1]!.atMs < cutoff) drop += 1;
    if (drop > 0) this.samples.splice(0, drop);
  }

  estimate(totalBytes: number | null): ThroughputEstimate {
    const latest = this.samples[this.samples.length - 1];
    const oldest = this.samples[0];
    const fraction =
      totalBytes && totalBytes > 0 && latest ? Math.min(1, latest.bytes / totalBytes) : null;

    const warmedUp =
      this.startedAtMs !== null && latest !== undefined && latest.atMs - this.startedAtMs >= THROUGHPUT_WARMUP_MS;
    if (!warmedUp || !latest || !oldest || this.samples.length < MIN_SAMPLES) {
      return { bytesPerSecond: null, secondsRemaining: null, fraction, measuring: true };
    }

    const elapsedMs = latest.atMs - oldest.atMs;
    const movedBytes = latest.bytes - oldest.bytes;
    if (elapsedMs <= 0 || movedBytes <= 0) {
      return { bytesPerSecond: null, secondsRemaining: null, fraction, measuring: true };
    }

    const bytesPerSecond = (movedBytes * 1000) / elapsedMs;
    const remainingBytes = totalBytes === null ? null : Math.max(0, totalBytes - latest.bytes);
    return {
      bytesPerSecond,
      secondsRemaining: remainingBytes === null ? null : remainingBytes / bytesPerSecond,
      fraction,
      measuring: false,
    };
  }
}

/** "about 2 minutes", "about 20 seconds", "less than 5 seconds". */
export function describeRemaining(secondsRemaining: number | null): string {
  if (secondsRemaining === null) return 'working out how long this will take…';
  if (secondsRemaining < 5) return 'less than 5 seconds left';
  if (secondsRemaining < 90) return `about ${Math.round(secondsRemaining / 5) * 5} seconds left`;
  const minutes = Math.round(secondsRemaining / 60);
  return `about ${minutes} minute${minutes === 1 ? '' : 's'} left`;
}

export function describeRate(bytesPerSecond: number | null): string {
  if (bytesPerSecond === null) return 'measuring…';
  return bytesPerSecond >= 1024
    ? `${(bytesPerSecond / 1024).toFixed(0)} kB/s`
    : `${Math.round(bytesPerSecond)} B/s`;
}
