// What firmware wrote this log, inferred from evidence in the log itself.
//
// The problem this solves is one the sibling A3EM dashboard hit first and stated well: a message
// that describes what the firmware does becomes a lie the moment the firmware changes, and nothing
// in a constant snapshot or a record-grammar check can see it. This app had exactly that — the
// charger-storm message describes a de-bounce that firmware before 626b34bc did not have, so on an
// older log it asserts behaviour that was not there.
//
// The method is theirs too, including the part that is easy to get wrong:
//
//   DETECT FROM EVIDENCE, NEVER FROM A VERSION STRING. A .ttg carries no firmware version, and even
//   if it did, pinning behaviour to release numbers means every new build looks unrecognised and
//   falls back to a stale entry — which is worse than not checking at all.
//
// The addition here is the third state. Absence of evidence is not evidence of absence when the log
// is short: DIAGNOSTICS records are written once per TimeAlignedTask pass, so a ten-minute log from
// current firmware contains none and looks ancient. Anything derived from an absence therefore has
// to know whether the log ran long enough for that absence to mean something.

import { STORAGE_TYPE, TIME_ALIGNED_INTERVAL_S } from './constants.ts';
import type { LogRecord, ParseReport } from './log.ts';

/**
 * `yes` — seen in the log. `no` — not seen, and the log ran long enough that it would have been.
 * `unknown` — not seen, but the log is too short for that to mean anything.
 */
export type Evidence = 'yes' | 'no' | 'unknown';

export interface FirmwareEra {
  /** Records carry their own length, so an unknown record type can be stepped over. */
  readonly framedRecords: boolean;
  /** The device records why it restarted. */
  readonly recordsResetReason: Evidence;
  /** The device pairs experiment time with raw RTC time, which is what measures an outage. */
  readonly writesTimeAnchors: Evidence;
  /** The device reports near-miss counters. */
  readonly writesDiagnostics: Evidence;
  /**
   * The charge-status ISR compares against the last reported state and de-bounces.
   *
   * Inferred rather than observed: de-bounce (626b34bc) landed BEFORE the diagnostics record that
   * reports its suppressed-edge counter (688c4dee), so a log containing diagnostics is necessarily
   * from firmware that has it. The converse does not hold, which is why this is Evidence and not a
   * boolean.
   */
  readonly chargerDebounced: Evidence;
  /** Human-readable account of what the inference rested on. */
  readonly evidence: readonly string[];
}

/** How long a log has to run before the absence of a periodic record means anything. */
const CONCLUSIVE_ABSENCE_SECONDS = 2 * TIME_ALIGNED_INTERVAL_S;

export function detectFirmwareEra(
  records: readonly LogRecord[],
  report: ParseReport,
  spanSeconds: number,
): FirmwareEra {
  const seen = new Set(records.map((record) => record.type));
  const longEnough = spanSeconds >= CONCLUSIVE_ABSENCE_SECONDS;
  const evidence: string[] = [];

  const judge = (type: number, what: string, periodic: boolean): Evidence => {
    if (seen.has(type)) {
      evidence.push(`${what} present`);
      return 'yes';
    }
    // A per-event record can legitimately be absent from a healthy run — a log with no reboot has no
    // reset record — so only the PERIODIC ones can be concluded absent from a long enough log.
    if (periodic && longEnough) {
      evidence.push(`no ${what} in ${Math.round(spanSeconds)}s, which is long enough to expect one`);
      return 'no';
    }
    evidence.push(`no ${what}, but the log is too short or the event too rare to conclude anything`);
    return 'unknown';
  };

  const framedRecords = report.formatVersion === 2;
  evidence.push(framedRecords ? 'records are framed' : `stream format ${report.formatVersion ?? 'v1'}`);

  const writesDiagnostics = judge(STORAGE_TYPE.STORAGE_TYPE_DIAGNOSTICS, 'diagnostics records', true);
  const writesTimeAnchors = judge(STORAGE_TYPE.STORAGE_TYPE_TIME_ANCHOR, 'time anchors', true);
  const recordsResetReason = judge(STORAGE_TYPE.STORAGE_TYPE_RESET_REASON, 'reset records', false);

  // Diagnostics imply de-bounce; their absence implies nothing either way, because the two shipped
  // separately and a log could come from between them.
  const chargerDebounced: Evidence = writesDiagnostics === 'yes' ? 'yes' : 'unknown';

  return { framedRecords, recordsResetReason, writesTimeAnchors, writesDiagnostics, chargerDebounced, evidence };
}

/** One line for the UI, saying how much of the analysis this log can support. */
export function describeEra(era: FirmwareEra): string {
  if (era.writesDiagnostics === 'yes') {
    return 'Written by current firmware: every check below applies.';
  }
  if (era.writesDiagnostics === 'no') {
    return (
      'Written by firmware older than the diagnostics record. Near-miss counters, and anything ' +
      'depending on them, are not available for this log — they were not being recorded.'
    );
  }
  return (
    'Too short to tell which firmware wrote it. Checks that rely on periodic records are reported ' +
    'as unavailable rather than as absent.'
  );
}
