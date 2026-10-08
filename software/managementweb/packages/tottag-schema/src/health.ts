// What a deployment log says about the deployment.
//
// The v2 format was built so that a loss has a known size and location, and the reset and time-anchor
// records were added so that a reboot and a clock change stop being things a reader infers from a gap.
// This module is what makes use of that: it turns a parsed log into the summary a person offloading
// four tags actually needs, which is not a list of records.
//
// Every quantity here was arrived at by hand while auditing a real 3.9-day four-device deployment
// (Storage_Redesign.md §15.8). That audit found things no existing tool would have surfaced — a task
// stall named in the diagnostics, a whole-system stall on another device, and 19,677 spurious charging
// records on a third — and each of those is a check below rather than a thing someone has to notice.
//
// Nothing here fetches anything. It takes a ParseResult and returns numbers.

import {
  BATTERY_CHECK_INTERVAL_S, BATTERY_EVENT_DEBOUNCE_MS, RESET_DIAGNOSTIC, STORAGE_FLUSH_TIMEOUT_S, TIME_ALIGNED_INTERVAL_S,
  TIME_BASE_CHANGE_THRESHOLD_MS, WATCHDOG_RESET_WINDOW_S,
} from './constants.ts';
import { parseExperimentDetails } from './log.ts';
import type { LogRecord, ParseResult } from './log.ts';
import { detectFirmwareEra, type FirmwareEra } from './firmwareEra.ts';

// --- Types ------------------------------------------------------------------------------------------

export type RebootKind = 'graceful' | 'watchdog' | 'other';

/**
 * How a stall presented, which the diagnostic and the silence together determine.
 *
 * The distinction is the most useful thing a reset record carries, and it is not obvious from either
 * signal alone:
 *
 * - `single-task` — a task was named, and the silence before the reset is under
 *   STORAGE_FLUSH_TIMEOUT_S. The rest of the system kept running and logging; what was lost is the
 *   unflushed page and nothing else.
 * - `whole-system` — no cause was recorded, and the silence exceeds the flush window. Recording a
 *   stall requires some monitored task to call the pet and be told a peer is late, so nothing
 *   recorded means nothing was running. The device genuinely stopped.
 * - `fault` — a handler ran and named a fault rather than a stall.
 * - `indeterminate` — the two signals disagree, which is worth surfacing rather than resolving.
 */
export type StallShape = 'single-task' | 'whole-system' | 'fault' | 'indeterminate';

export interface RebootEvent {
  readonly ms: number;
  readonly kind: RebootKind;
  readonly causes: readonly string[];
  readonly diagnosticLabel: string | null;
  /** Seconds between the last record before the reset and the reset record itself. */
  readonly silenceSeconds: number;
  /**
   * Real seconds between the anchors either side of the reset, from the device's own RTC-derived
   * clock rather than from the log clock.
   *
   * This is what the time anchor was added for. It is immune to the offset having moved, so it
   * measures the outage rather than the log's account of it — the distinction that made a hang
   * indistinguishable from a spurious reset before anchors existed. Null when the reset is not
   * bracketed by two anchors.
   */
  readonly outageSeconds: number | null;
  /** Null unless this was a watchdog reset. */
  readonly shape: StallShape | null;
  /**
   * True when a charging record precedes this reset by a few seconds: the charger caused the reboot.
   *
   * Deliberately not "a charging record at the same millisecond". Every boot writes the current
   * charge state as one of its first records, so a same-millisecond charging record accompanies
   * *every* reset, watchdog resets included, and keying on it would call all of them charger events.
   * A transition that caused the reboot appears BEFORE it, at the graceful flush.
   */
  readonly chargerTransition: boolean;
}

export interface ClockHealth {
  /** Anchors seen. One per TimeAlignedTask loop, plus one per boot, plus one per re-basing. */
  readonly anchorCount: number;
  /** network - local, in ms, at the first and last anchor. */
  readonly offsetStartMs: number;
  readonly offsetEndMs: number;
  /** Largest single step in the offset between consecutive anchors, in ms. */
  readonly largestOffsetStepMs: number;
  /**
   * How far this device's clock moved relative to the network's over the run, in ms.
   *
   * Absorbed by the offset mechanism, so it affects absolute time only and never agreement between
   * devices. Over a long run it is a direct measurement of relative crystal error.
   */
  readonly offsetDriftMs: number;
  /**
   * Whether the device's own clock advanced monotonically across every anchor.
   *
   * It is derived from the RTC, which survives a reset, so it should never step backwards. If it
   * does, the RTC restarted — power was actually removed — and experiment-relative time before and
   * after that point are measured from different origins.
   */
  readonly localClockMonotonic: boolean;
  /** Median seconds between consecutive periodic anchors. Expect TIME_ALIGNED_INTERVAL_S. */
  readonly medianIntervalSeconds: number;
}

export interface CadenceHealth {
  /** Fraction of each day the deployment was configured to record. 1 when there is no daily window. */
  readonly dutyCycle: number;
  /**
   * True when the log's own span cannot be used as elapsed time, because the device's time base
   * stepped mid-run. Reporting a percentage against it would be arithmetic on two different clocks.
   */
  readonly unavailable: boolean;
  /** Loop iterations the elapsed time implies, at the real 299.38 s period. */
  readonly expected: number;
  readonly voltageRecords: number;
  readonly anchorRecords: number;
  /**
   * Fraction of the expected 300 s heartbeat actually present, from the voltage records.
   * Slightly over 1.0 is healthy; anchors run higher still because of the extra per-boot one.
   */
  readonly captured: number;
}

export interface IntegrityHealth {
  readonly pagesAdvertised: number | null;
  readonly pagesDelivered: number;
  readonly holes: number;
  readonly crcFailures: number;
  readonly shortPages: number;
  readonly truncated: boolean;
  /** Sequence numbers that are absent from an otherwise contiguous run. */
  readonly sequenceGaps: number;
  readonly duplicateSeqs: number;
  /** Backward page bounds at or above TIME_BASE_CHANGE_THRESHOLD_MS: worth a warning. */
  readonly significantTimeSteps: number;
  /** Backward page bounds below that threshold: routine clock adjustment. */
  readonly benignTimeSteps: number;
  readonly clean: boolean;
}

export interface PageFillHealth {
  readonly pages: number;
  readonly meanPayloadBytes: number;
  /** Pages holding a single 300 s heartbeat batch and nothing else. */
  readonly heartbeatOnlyPages: number;
  readonly pagesPerDay: number;
  readonly payloadBytes: number;
}

export interface Anomaly {
  readonly severity: 'warning' | 'note';
  readonly code: string;
  readonly message: string;
}

/**
 * Conditions the firmware recovered from, taken from the last diagnostics record of each boot.
 *
 * These are the counters that used to be reachable only from a console. They matter because the
 * absence of a fault in a log is not evidence that nothing went wrong: §15.3 spent two runs unable to
 * locate "a task went more than 60 s late roughly once per device per 19 hours" because the stall
 * latch is withdrawn on recovery and nothing counted the near-misses.
 */
export interface NearMissHealth {
  /** Diagnostics records seen. Zero means firmware older than the record type. */
  readonly samples: number;
  /** Watchdog pets refused because some task was late, summed over boots. */
  readonly watchdogDeclines: number;
  /** Distinct episodes of lateness per task, summed over boots. */
  readonly watchdogLate: ReadonlyMap<string, number>;
  /** Charger interrupts discarded as chatter. Non-zero means a physically bouncing pin. */
  readonly chargerSuppressedEdges: number;
  /** BLE buffer allocations that returned NULL. Any non-zero value means a dropped packet or event. */
  readonly wsfAllocFailures: number;
  readonly wsfLargestFailedLength: number;
  /** Worst observed headroom in any WSF pool, as buffers still free at the peak. */
  readonly tightestPoolHeadroom: number | null;
  readonly tightestPool: number | null;
  /**
   * The device reported pool figures that cannot be true — a peak above the pool's own capacity, or
   * a capacity of zero for a pool that had allocations.
   *
   * Kept as a flag rather than being silently dropped: it says something real about the device, just
   * not what the counters claim.
   */
  readonly poolFiguresImplausible: boolean;
  /** Builds that wrote the log, as revision plus " (modified)" for uncommitted changes. Empty when the log has no diagnostics records. */
  readonly firmwareBuilds: readonly string[];
  /** Records discarded because the storage queue was full, summed over boots. */
  readonly recordsDropped: number;
  /** UWB radio recoveries, summed over boots. */
  readonly radioWakeFailures: number;
  readonly radioIrqStuck: number;
  readonly radioRxArmLate: number;
  readonly radioTxLate: number;
  /** Bluetooth controller restarts by the self-check, summed over boots. */
  readonly bleResets: number;
  /** Least free stack seen per task, in words, across the whole log. */
  readonly lowestStackWords: ReadonlyMap<string, number>;
  /** Flash blocks retired while this log was being written. */
  readonly nandBlocksRetired: number;
}

/** A task stack with less than this many words never touched is worth flagging. */
export const LOW_STACK_WARNING_WORDS = 64;

export interface DeploymentHealth {
  /**
   * Which firmware wrote this, inferred from evidence in the log.
   *
   * Every message below that describes device BEHAVIOUR has to be conditioned on this. A log from
   * before the charger de-bounce landed is not wrong, it is just older, and telling its reader that
   * the firmware de-bounces would be a plain falsehood about the run in front of them.
   */
  readonly era: FirmwareEra;
  readonly spanSeconds: number;
  readonly recordCount: number;
  readonly integrity: IntegrityHealth;
  readonly reboots: readonly RebootEvent[];
  readonly watchdogResets: number;
  /** Seconds of records lost to watchdog resets, as a fraction of the run. */
  readonly runTimeLostFraction: number;
  readonly clock: ClockHealth;
  readonly cadence: CadenceHealth;
  readonly pageFill: PageFillHealth;
  readonly recordCounts: ReadonlyMap<string, number>;
  /** Per-peer ranging record counts, by peer UID. */
  readonly rangingPeers: ReadonlyMap<number, number>;
  readonly batteryStartMv: number | null;
  readonly batteryEndMv: number | null;
  readonly nearMisses: NearMissHealth;
  readonly anomalies: readonly Anomaly[];
}

// --- Helpers ------------------------------------------------------------------------------------------

const median = (values: number[]): number => {
  if (!values.length) return 0;
  const sorted = [...values].sort((a, b) => a - b);
  return sorted[Math.floor(sorted.length / 2)]!;
};

// --- Analysis ---------------------------------------------------------------------------------------

export function analyseDeployment(result: ParseResult): DeploymentHealth {
  const { records, report } = result;
  const anomalies: Anomaly[] = [];

  const spanMs = records.length ? records[records.length - 1]!.ms - records[0]!.ms : 0;
  const spanSeconds = spanMs / 1000;
  const era = detectFirmwareEra(records, report, spanSeconds);
  // Biggest single jump between consecutive anchors. A run that steps once and then holds is not
  // drifting: reporting 34344 s of movement as "117772 ppm" would describe a crystal that does not
  // exist. Computed here because both the clock report and the cadence denominator depend on it.
  const anchorRecords = records.filter((record) => record.kind === 'anchor');
  let timeBaseStepMs = 0;
  for (let index = 1; index < anchorRecords.length; index += 1) {
    const previous = anchorRecords[index - 1] as { offsetMs: number };
    const current = anchorRecords[index] as { offsetMs: number };
    timeBaseStepMs = Math.max(timeBaseStepMs, Math.abs(current.offsetMs - previous.offsetMs));
  }

  // --- record census -------------------------------------------------------------------------------
  const recordCounts = new Map<string, number>();
  const rangingPeers = new Map<number, number>();
  const anchors: Array<Extract<LogRecord, { kind: 'anchor' }>> = [];
  const diagnostics: Array<Extract<LogRecord, { kind: 'diagnostics' }>> = [];
  const voltages: Array<Extract<LogRecord, { kind: 'voltage' }>> = [];

  let chargingCount = 0;

  for (const record of records) {
    recordCounts.set(record.kind, (recordCounts.get(record.kind) ?? 0) + 1);
    if (record.kind === 'anchor') anchors.push(record);
    else if (record.kind === 'diagnostics') diagnostics.push(record);
    else if (record.kind === 'voltage') voltages.push(record);
    else if (record.kind === 'charging') {

      chargingCount += 1;
    } else if (record.kind === 'ranges') {
      for (const peer of record.ranges.keys()) rangingPeers.set(peer, (rangingPeers.get(peer) ?? 0) + 1);
    }
  }

  // --- integrity -----------------------------------------------------------------------------------
  const seqs = report.pages.map((page) => page.seq);
  const uniqueSeqs = new Set(seqs);
  let sequenceGaps = 0;
  for (let i = 1; i < seqs.length; i += 1) {
    if (seqs[i]! !== seqs[i - 1]! + 1) sequenceGaps += 1;
  }
  const significantTimeSteps = report.timeDiscontinuities.filter(
    (step) => step.previousLast - step.thisFirst >= TIME_BASE_CHANGE_THRESHOLD_MS,
  ).length;

  const integrity: IntegrityHealth = {
    pagesAdvertised: report.totalPages,
    pagesDelivered: report.pagesRead,
    holes: report.holes.length,
    crcFailures: report.crcFailures.length,
    shortPages: report.shortPages.length,
    truncated: report.truncated,
    sequenceGaps,
    duplicateSeqs: seqs.length - uniqueSeqs.size,
    significantTimeSteps,
    benignTimeSteps: report.timeDiscontinuities.length - significantTimeSteps,
    clean:
      report.holes.length === 0 &&
      report.crcFailures.length === 0 &&
      report.shortPages.length === 0 &&
      !report.truncated &&
      sequenceGaps === 0 &&
      seqs.length === uniqueSeqs.size,
  };

  if (integrity.holes || integrity.crcFailures) {
    anomalies.push({
      severity: 'warning',
      code: 'pages-lost',
      message:
        `${integrity.holes} page(s) the device could not read, and ${integrity.crcFailures} page(s) that failed ` +
        'CRC here. Worth asking for both again, but they are not the same thing: a CRC failure means the bytes ' +
        'were damaged on the way over and a retransmission should fix it, whereas a page the device could not ' +
        'read will most likely come back unreadable again, because the flash content itself is bad. Ask before ' +
        'treating the file as final.',
    });
  }
  if (integrity.truncated) {
    anomalies.push({
      severity: 'warning',
      code: 'truncated',
      message: `Transfer ended after ${report.pagesRead} of ${report.totalPages} advertised pages.`,
    });
  }
  if (significantTimeSteps) {
    anomalies.push({
      severity: 'warning',
      code: 'time-rebased',
      message:
        `${significantTimeSteps} backward page boundary(ies) of at least ${TIME_BASE_CHANGE_THRESHOLD_MS} ms. ` +
        'A full download is unaffected, but a date-limited one may be missing data it asked for.',
    });
  }

  // --- reboots -------------------------------------------------------------------------------------
  // A reboot's cost is the silence before its reset record. Under the flush timeout that silence is
  // the unflushed page and nothing more; past it, the device stopped executing. That single
  // comparison is what separates the two stall shapes, and it needs no second device to make.
  const reboots: RebootEvent[] = [];
  let lostToWatchdogSeconds = 0;

  for (let i = 0; i < records.length; i += 1) {
    const record = records[i]!;
    if (record.kind !== 'reset') continue;
    const silenceSeconds = i === 0 ? 0 : (record.ms - records[i - 1]!.ms) / 1000;

    // Bracket the reset with the nearest anchor on each side and measure across it on the device's
    // own clock, which the reboot does not touch.
    const before = anchors.filter((anchor) => anchor.ms < record.ms).pop();
    const after = anchors.find((anchor) => anchor.ms >= record.ms);
    const outageSeconds = before && after ? (after.localMs - before.localMs) / 1000 : null;

    // A charging record shortly BEFORE the reset is the transition that caused it. One at the same
    // instant is the boot-time status report that accompanies every reset of any kind.
    const chargerTransition = records
      .slice(Math.max(0, i - 40), i)
      .some((other) => other.kind === 'charging' && record.ms - other.ms > 0 && record.ms - other.ms < 10_000);
    const kind: RebootKind = record.isWatchdog
      ? 'watchdog'
      : record.causes.some((cause) => cause === 'SW Power-On')
        ? 'graceful'
        : 'other';

    let shape: StallShape | null = null;
    if (record.isWatchdog) {
      lostToWatchdogSeconds += silenceSeconds;
      const named =
        record.diagnostic >= RESET_DIAGNOSTIC.RESET_DIAGNOSTIC_STALL_TIME_ALIGNED &&
        record.diagnostic <= RESET_DIAGNOSTIC.RESET_DIAGNOSTIC_STALL_MULTIPLE;
      const nothing = record.diagnostic === RESET_DIAGNOSTIC.RESET_DIAGNOSTIC_NOTHING_RECORDED;
      const quiet = silenceSeconds > STORAGE_FLUSH_TIMEOUT_S;
      if (record.diagnostic >= RESET_DIAGNOSTIC.RESET_DIAGNOSTIC_HARD_FAULT && !nothing) shape = 'fault';
      else if (named && !quiet) shape = 'single-task';
      else if (nothing && quiet) shape = 'whole-system';
      else shape = 'indeterminate';
    }

    reboots.push({
      ms: record.ms,
      kind,
      causes: record.causes,
      diagnosticLabel: record.diagnosticLabel,
      silenceSeconds,
      outageSeconds,
      shape,
      chargerTransition,
    });
  }

  const watchdogResets = reboots.filter((reboot) => reboot.kind === 'watchdog').length;
  if (watchdogResets) {
    const shapes = new Map<string, number>();
    for (const reboot of reboots) {
      if (reboot.shape) shapes.set(reboot.shape, (shapes.get(reboot.shape) ?? 0) + 1);
    }
    const described = [...shapes].map(([shape, count]) => `${count} ${shape}`).join(', ');
    // Explain only the shapes actually present. Describing what a whole-system stall would mean on a
    // log that contains none reads as a warning about something that did not happen.
    const shapeNotes: string[] = [];
    if (shapes.has('single-task')) {
      shapeNotes.push(
        'A single-task stall means the rest of the system kept running and logging, so what was lost is the ' +
          'unflushed page and nothing else.',
      );
    }
    if (shapes.has('whole-system')) {
      shapeNotes.push('A whole-system stall means no task was running to record a cause: the device genuinely stopped.');
    }
    if (shapes.has('fault')) {
      shapeNotes.push('A fault means a handler ran and named a fault rather than a stall.');
    }
    if (shapes.has('indeterminate')) {
      shapeNotes.push(
        'An indeterminate shape means the recorded cause and the length of the silence disagree, which is worth ' +
          'looking at rather than resolving automatically.',
      );
    }
    anomalies.push({
      severity: 'warning',
      code: 'watchdog-reset',
      message:
        `${watchdogResets} watchdog reset(s) (${described}). The watchdog contains a hang rather than causing ` +
        `one: each costs at most the ~${Math.round(WATCHDOG_RESET_WINDOW_S)} s reset window plus the unflushed ` +
        `page. ${shapeNotes.join(' ')}`,
    });
  }

  // --- clock ---------------------------------------------------------------------------------------
  let clock: ClockHealth = {
    anchorCount: anchors.length,
    offsetStartMs: 0,
    offsetEndMs: 0,
    largestOffsetStepMs: 0,
    offsetDriftMs: 0,
    localClockMonotonic: true,
    medianIntervalSeconds: 0,
  };
  if (anchors.length >= 2) {
    const first = anchors[0]!;
    const last = anchors[anchors.length - 1]!;
    let largestStep = 0;
    let monotonic = true;
    const intervals: number[] = [];
    for (let i = 1; i < anchors.length; i += 1) {
      const step = Math.abs(anchors[i]!.offsetMs - anchors[i - 1]!.offsetMs);
      if (step > largestStep) largestStep = step;
      const interval = (anchors[i]!.localMs - anchors[i - 1]!.localMs) / 1000;
      if (interval < 0) monotonic = false;
      if (interval > 0) intervals.push(interval);
    }
    clock = {
      anchorCount: anchors.length,
      offsetStartMs: first.offsetMs,
      offsetEndMs: last.offsetMs,
      largestOffsetStepMs: largestStep,
      offsetDriftMs: last.offsetMs - first.offsetMs,
      localClockMonotonic: monotonic,
      medianIntervalSeconds: median(intervals),
    };

    if (!monotonic) {
      anomalies.push({
        severity: 'warning',
        code: 'rtc-restarted',
        message:
          "The device's own clock steps backwards inside this log. Experiment-relative timestamps either " +
          'side of that point are measured from different origins, so durations spanning it are not ' +
          'meaningful. Two things can cause it: power was removed and the RTC restarted, which is much the ' +
          'likelier, or something wrote the BLE timestamp characteristic mid-run — that write is not guarded ' +
          'against an active deployment.',
      });
    }
    if (timeBaseStepMs >= TIME_BASE_CHANGE_THRESHOLD_MS) {
      anomalies.push({
        severity: 'note',
        code: 'time-base-step',
        message:
          `The device's time base moved by ${(timeBaseStepMs / 1000).toFixed(1)} s in one step, and by ` +
          `${(clock.offsetDriftMs / 1000).toFixed(1)} s over the run. That is a correction, not drift: the tag ` +
          'adopted the network\'s clock when it joined, which it does whenever its own RTC disagrees by more ' +
          `than ${TIME_BASE_CHANGE_THRESHOLD_MS} ms. Ranges and agreement between devices are unaffected — ` +
          'they are measured on the shared clock. What it does mean is that this tag\'s RTC was wrong by about ' +
          'that much when the run started, which is worth fixing before the next deployment.',
      });
    } else if (spanSeconds > 3600 && Math.abs(clock.offsetDriftMs) > 1000) {
      anomalies.push({
        severity: 'note',
        code: 'network-clock-drift',
        message:
          `This device's clock moved ${(clock.offsetDriftMs / 1000).toFixed(1)} s relative to the network over ` +
          `the run (${((Math.abs(clock.offsetDriftMs) / 1000 / spanSeconds) * 1e6).toFixed(0)} ppm). Absorbed ` +
          'by the offset mechanism; it affects absolute time only, not agreement between devices.',
      });
    }
  }

  // --- cadence -------------------------------------------------------------------------------------
  // A deployment with a daily window is asleep outside it BY DESIGN, so the span is not the time the
  // device was supposed to be running. Measuring against the span reports a healthy tag as having
  // lost half its run — which is exactly what a real 13-hours-a-day deployment produced: 54.2%
  // "shortfall" on a log with zero holes, zero CRC failures and no gaps.
  //
  // The device compares rtc_get_time_of_day() against the stored window, and both are UTC, so the
  // duty cycle is computable from the details without knowing anything about the site's timezone.
  const details = report.details ? parseExperimentDetails(report.details) : null;
  const dutyCycle = (() => {
    if (!details?.useDailyTimes) return 1;
    const start = details.dailyStartTime;
    const end = details.dailyEndTime;
    // A window running past midnight is expressed as start > end, which the firmware handles. An end
    // beyond 86399 is out of range — rtc_get_time_of_day() never reaches it, so the window in effect
    // runs to midnight, and that is what has to be modelled rather than what was intended.
    const effectiveEnd = end > 86399 ? 86400 : end;
    const awake = effectiveEnd > start ? effectiveEnd - start : 86400 - start + effectiveEnd;
    return Math.min(1, Math.max(0, awake / 86400));
  })();
  // A time-base step means the span measured on log timestamps is not elapsed time. One real file
  // jumped 9.5 hours mid-run, and dividing its span by the heartbeat period gave 130% of the
  // expected records — a meaningless number that would have been read as one.
  //
  // Scaled against the heartbeat rather than against the firmware's re-basing threshold: a step
  // SMALLER than one heartbeat interval cannot account for even one missing or extra record, so it
  // does not invalidate the denominator. A 4 s correction in an 8-minute log is real and reported,
  // and still leaves the cadence figure usable.
  const spanUnusable = timeBaseStepMs / 1000 >= TIME_ALIGNED_INTERVAL_S;
  const rawExpected = (spanSeconds * dutyCycle) / TIME_ALIGNED_INTERVAL_S;
  // Below a handful of expected records the ratio is dominated by where the window happened to fall:
  // five heartbeats in 1284 s is 117% of expected and entirely healthy. Reporting a percentage there
  // invites a reader to act on rounding.
  const cadenceUnavailable = spanUnusable || rawExpected <= 10;
  const expected = cadenceUnavailable ? 0 : rawExpected;
  const cadence: CadenceHealth = {
    dutyCycle,
    /** True when a time-base step makes the span unusable as a denominator. */
    unavailable: cadenceUnavailable,
    expected,
    voltageRecords: voltages.length,
    anchorRecords: anchors.length,
    captured: expected > 0 ? voltages.length / expected : 0,
  };
  if (expected > 10 && cadence.captured < 0.98) {
    anomalies.push({
      severity: 'warning',
      code: 'cadence-shortfall',
      message:
        `Only ${(cadence.captured * 100).toFixed(1)}% of the expected heartbeat records are present. The ` +
        `heartbeat is nominally every ${BATTERY_CHECK_INTERVAL_S} s but measures ${TIME_ALIGNED_INTERVAL_S} s, ` +
        'because the FreeRTOS tick divisor truncates. ' +
        (dutyCycle < 1
          ? `This deployment records ${(dutyCycle * 24).toFixed(1)} hours a day, and the figure already ` +
            'accounts for the hours it was meant to be asleep, so the shortfall is on top of that.'
          : 'This figure is against the measured period, so the shortfall is real time the device was not ' +
            'running rather than an artefact of the nominal number.'),
    });
  }

  // --- page fill -----------------------------------------------------------------------------------
  const payloadBytes = report.pages.reduce((sum, page) => sum + page.payloadLength, 0);
  const pageFill: PageFillHealth = {
    pages: report.pages.length,
    payloadBytes,
    meanPayloadBytes: report.pages.length ? payloadBytes / report.pages.length : 0,
    // A page holding two or three records and under 64 bytes is one 300 s heartbeat batch and
    // nothing else: an idle device paying a whole NAND page for 18 bytes. Not a defect, but it is
    // most of the pages in a real file and it explains the transfer size.
    heartbeatOnlyPages: report.pages.filter((page) => page.payloadLength > 0 && page.payloadLength < 64).length,
    pagesPerDay: spanSeconds > 0 ? (report.pages.length * 86400) / spanSeconds : 0,
  };

  // --- device-level anomalies ------------------------------------------------------------------------
  // Not storage problems, but the log is the only place they are visible, and the audit that
  // motivated this module found each of them by hand.
  if (chargingCount > 100) {
    anomalies.push({
      severity: 'warning',
      code: 'charging-event-storm',
      message:
        `${chargingCount} charging records. ` +
        (era.chargerDebounced === 'yes'
          ? `This firmware accepts a charger edge only when the state actually changed and at most once ` +
            `per ${BATTERY_EVENT_DEBOUNCE_MS} ms, counting anything faster into charger_suppressed_edges, so a ` +
            'genuine plug/unplug pattern is single digits per deployment. A count this high means either real ' +
            'repeated connector movement or a charger the de-bounce window is too short for — the suppressed-edge ' +
            'count below tells those apart.'
          : 'This log predates the diagnostics record, so it cannot be confirmed whether the firmware that wrote ' +
            'it de-bounced charger edges at all. Firmware before that reported every interrupt unconditionally, ' +
            'and a chattering pin then becomes one record per edge. Either way a genuine plug/unplug pattern is ' +
            'single digits per deployment, so this is worth looking at.'),
    });
  }
  const gracefulReboots = reboots.filter((reboot) => reboot.kind === 'graceful').length;
  if (gracefulReboots > 0 && reboots.filter((reboot) => reboot.chargerTransition).length === gracefulReboots) {
    anomalies.push({
      severity: 'note',
      code: 'charger-reboots',
      message:
        `All ${gracefulReboots} graceful reboot(s) are charger transitions. Plugging or unplugging the tag ` +
        'flushes and resets it, costing a few seconds of records each time. Expected, not a fault.',
    });
  }

  // --- near misses -----------------------------------------------------------------------------------
  // The counters are cumulative since boot and reset on every reboot, so the total for a run is the sum of
  // the LAST diagnostics record of each boot. Reading only the final record would report whatever happened
  // since the last restart and discard the rest, which on a log with several reboots is most of it.
  //
  // Boot boundaries come from the reset records, which are unambiguous. The counter-decrease check is a
  // backstop for the case §12.21 warns about -- a device that resets before its reset record reaches flash,
  // where the only remaining evidence of the boundary is a counter that went backwards.
  const perBoot: Array<Extract<LogRecord, { kind: 'diagnostics' }>> = [];
  let pending: Extract<LogRecord, { kind: 'diagnostics' }> | null = null;
  for (const record of records) {
    if (record.kind === 'reset' && pending) {
      perBoot.push(pending);
      pending = null;
    } else if (record.kind === 'diagnostics') {
      if (pending && (record.watchdogDeclines < pending.watchdogDeclines ||
                      record.chargerSuppressedEdges < pending.chargerSuppressedEdges ||
                      record.wsfAllocFailures < pending.wsfAllocFailures)) {
        perBoot.push(pending);
      }
      pending = record;
    }
  }
  if (pending) perBoot.push(pending);
  const watchdogLate = new Map<string, number>();
  let tightestPoolHeadroom: number | null = null;
  let tightestPool: number | null = null;
  let poolFiguresImplausible = false;
  for (const sample of diagnostics) {
    for (let pool = 0; pool < sample.wsfPoolSize.length; pool += 1) {
      const size = sample.wsfPoolSize[pool]!;
      const peak = sample.wsfPoolPeak[pool]!;
      // A pool cannot have had more buffers outstanding than it owns, and a pool that exists cannot
      // own none. Either means the numbers did not come from where they claim to, so report that
      // rather than a derived figure — "-118 buffers spare" is the shape of an answer nobody can act
      // on, and printing it teaches the reader to distrust the whole panel.
      if (peak > size || (peak > 0 && size === 0)) {
        poolFiguresImplausible = true;
        continue;
      }
      if (!size) continue;
      const headroom = size - peak;
      if (tightestPoolHeadroom === null || headroom < tightestPoolHeadroom) {
        tightestPoolHeadroom = headroom;
        tightestPool = pool;
      }
    }
  }
  if (poolFiguresImplausible) {
    tightestPoolHeadroom = null;
    tightestPool = null;
  }
  for (const sample of perBoot) {
    for (const [task, episodes] of sample.watchdogLate) {
      watchdogLate.set(task, (watchdogLate.get(task) ?? 0) + episodes);
    }
  }
  const sumPerBoot = (pick: (d: (typeof perBoot)[number]) => number) => perBoot.reduce((sum, d) => sum + pick(d), 0);
  const lowestStackWords = new Map<string, number>();
  for (const sample of diagnostics) {
    for (const [task, words] of sample.stackFreeWords) {
      if (words !== null) lowestStackWords.set(task, Math.min(words, lowestStackWords.get(task) ?? words));
    }
  }
  const badBlocks = diagnostics.map((d) => d.nandBadBlocks);

  const nearMisses: NearMissHealth = {
    samples: diagnostics.length,
    watchdogDeclines: perBoot.reduce((sum, d) => sum + d.watchdogDeclines, 0),
    watchdogLate,
    chargerSuppressedEdges: perBoot.reduce((sum, d) => sum + d.chargerSuppressedEdges, 0),
    wsfAllocFailures: perBoot.reduce((sum, d) => sum + d.wsfAllocFailures, 0),
    wsfLargestFailedLength: Math.max(0, ...diagnostics.map((d) => d.wsfLargestFailedLength)),
    tightestPoolHeadroom,
    tightestPool,
    poolFiguresImplausible,
    firmwareBuilds: [...new Set(diagnostics.map((d) => d.firmwareRevision + (d.firmwareModified ? ' (modified)' : '')))].sort(),
    recordsDropped: sumPerBoot((d) => d.recordsDropped),
    radioWakeFailures: sumPerBoot((d) => d.radioWakeFailures),
    radioIrqStuck: sumPerBoot((d) => d.radioIrqStuck),
    radioRxArmLate: sumPerBoot((d) => d.radioRxArmLate),
    radioTxLate: sumPerBoot((d) => d.radioTxLate),
    bleResets: sumPerBoot((d) => d.bleResets),
    lowestStackWords,
    nandBlocksRetired: badBlocks.length ? Math.max(...badBlocks) - Math.min(...badBlocks) : 0,
  };

  if (nearMisses.watchdogDeclines) {
    const named = [...watchdogLate].map(([task, n]) => `${task} x${n}`).join(', ');
    anomalies.push({
      severity: 'warning',
      code: 'watchdog-near-miss',
      message:
        `${nearMisses.watchdogDeclines} watchdog pet(s) were refused because a task was late (${named || 'task not identified'}). ` +
        'A decline is one health evaluation that found somebody late, so a single stall spanning several ' +
        'evaluations is counted more than once; the per-task episode counts are the distinct stalls. All of ' +
        'them recovered before the reset window — this is the counter that makes an intermittent stall ' +
        'locatable in time rather than merely known to exist.',
    });
  }
  if (nearMisses.chargerSuppressedEdges) {
    anomalies.push({
      severity: 'warning',
      code: 'charger-pin-chatter',
      message:
        `${nearMisses.chargerSuppressedEdges} charger interrupt(s) fell inside the de-bounce window. They are ` +
        'deferred rather than dropped: the firmware leaves the change outstanding, and TimeAlignedTask ' +
        'reconciles the pin against the recorded state on its next pass, so the transitions do reach the log ' +
        'but one may be stamped up to a heartbeat late. What the count tells you is that the pin on this unit ' +
        'is physically bouncing.',
    });
  }
  if (nearMisses.poolFiguresImplausible) {
    anomalies.push({
      severity: 'warning',
      code: 'pool-figures-implausible',
      message:
        'The device reported BLE buffer figures that cannot be true — a pool with more buffers ' +
        'outstanding than it owns, or none at all. The counters are not being read from where they ' +
        'should be, so nothing about buffer headroom can be concluded from this log. Everything else ' +
        'here is unaffected; these are the only fields involved.',
    });
  }
  if (nearMisses.wsfAllocFailures) {
    anomalies.push({
      severity: 'warning',
      code: 'wsf-pool-exhausted',
      message:
        `${nearMisses.wsfAllocFailures} BLE buffer allocation(s) returned NULL, largest request ` +
        `${nearMisses.wsfLargestFailedLength} bytes. The stack drops a packet or an event silently each ` +
        'time; during a download that ends the transfer.',
    });
  } else if (tightestPoolHeadroom !== null && tightestPoolHeadroom <= 1) {
    anomalies.push({
      severity: 'note',
      code: 'wsf-pool-tight',
      message:
        `WSF pool ${tightestPool} peaked with only ${tightestPoolHeadroom} buffer(s) spare. No allocation ` +
        'failed, but there is little margin left before one does.',
    });
  }
  if (nearMisses.recordsDropped) {
    anomalies.push({
      severity: 'warning',
      code: 'records-dropped',
      message:
        `${nearMisses.recordsDropped} record(s) were discarded because the storage queue was full. That data ` +
        'is missing from the log, and nothing else in it marks where.',
    });
  }
  const radioTrouble = ([
    [nearMisses.radioWakeFailures, 'wake-up(s) needing a radio reset'],
    [nearMisses.radioIrqStuck, 'stuck radio interrupt(s)'],
    [nearMisses.radioRxArmLate, 'round(s) aborted by a late receive'],
    [nearMisses.radioTxLate, 'late transmission(s)'],
  ] as const).filter(([count]) => count > 0);
  if (radioTrouble.length) {
    anomalies.push({
      severity: 'warning',
      code: 'uwb-radio-trouble',
      message:
        `The UWB radio needed recovering: ${radioTrouble.map(([count, label]) => `${count} ${label}`).join(', ')}. ` +
        'Each one costs ranging for at least a round.',
    });
  }
  if (nearMisses.bleResets) {
    anomalies.push({
      severity: 'warning',
      code: 'ble-controller-restarts',
      message:
        `The self-check restarted the Bluetooth controller ${nearMisses.bleResets} time(s) because ` +
        'advertising or scanning had stalled.',
    });
  }
  const lowStacks = [...lowestStackWords].filter(([, words]) => words < LOW_STACK_WARNING_WORDS);
  if (lowStacks.length) {
    anomalies.push({
      severity: 'warning',
      code: 'stack-nearly-exhausted',
      message:
        `Task stack(s) came within ${LOW_STACK_WARNING_WORDS} words of overflowing: ` +
        `${lowStacks.map(([task, words]) => `${task} ${words} free`).join(', ')}.`,
    });
  }
  if (nearMisses.nandBlocksRetired) {
    anomalies.push({
      severity: 'note',
      code: 'flash-blocks-retired',
      message: `${nearMisses.nandBlocksRetired} flash block(s) were retired while this log was being written.`,
    });
  }

  return {
    era,
    spanSeconds,
    recordCount: records.length,
    integrity,
    reboots,
    watchdogResets,
    runTimeLostFraction: spanSeconds > 0 ? lostToWatchdogSeconds / spanSeconds : 0,
    clock,
    cadence,
    pageFill,
    recordCounts,
    rangingPeers,
    batteryStartMv: voltages.length ? voltages[0]!.millivolts : null,
    batteryEndMv: voltages.length ? voltages[voltages.length - 1]!.millivolts : null,
    nearMisses,
    anomalies,
  };
}

// --- Cross-device -------------------------------------------------------------------------------------

export interface LinkAgreement {
  readonly a: number;
  readonly b: number;
  /** Timestamps at which both devices logged this link. */
  readonly common: number;
  /** Timestamps at which either did. */
  readonly union: number;
  /** Fraction of the union present in both logs. */
  readonly sharedFraction: number;
  /** Of the common timestamps, the fraction whose two range values agree within 100 mm. */
  readonly agreeingFraction: number;
}

/**
 * Compare two devices' logs across the link between them.
 *
 * A range between two devices is **one physical measurement logged at both ends**, so this is a
 * direct test of two things nothing inside a single file can check: that the two devices share a
 * time base, and that the values survived the round trip. A timestamp present in one log and absent
 * from the other is a lost record; two different values at the same timestamp would be corruption.
 *
 * On a healthy four-device deployment this reads ~99.5% shared and ~100% agreeing.
 */
export function compareLink(
  a: { uid: number; result: ParseResult },
  b: { uid: number; result: ParseResult },
  toleranceMm = 100,
): LinkAgreement {
  const rangesTo = (result: ParseResult, peer: number): Map<number, number> => {
    const out = new Map<number, number>();
    for (const record of result.records) {
      if (record.kind !== 'ranges') continue;
      const millimetres = record.ranges.get(peer);
      if (millimetres !== undefined) out.set(record.ms, millimetres);
    }
    return out;
  };

  const fromA = rangesTo(a.result, b.uid);
  const fromB = rangesTo(b.result, a.uid);
  let common = 0;
  let agreeing = 0;
  for (const [ms, value] of fromA) {
    const other = fromB.get(ms);
    if (other === undefined) continue;
    common += 1;
    if (Math.abs(value - other) <= toleranceMm) agreeing += 1;
  }
  const union = new Set([...fromA.keys(), ...fromB.keys()]).size;
  return {
    a: a.uid,
    b: b.uid,
    common,
    union,
    sharedFraction: union ? common / union : 0,
    agreeingFraction: common ? agreeing / common : 0,
  };
}
