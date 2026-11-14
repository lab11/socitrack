// When a tag was meant to record, when it did, and where the two part company. Pure.
//
// A tag records between the deployment's start and end and, when daily hours are set, only inside them;
// outside them it shuts its storage down by design. So an hour with nothing in it means something only
// against that schedule: a scheduled hour with nothing recorded is the finding, an hour the schedule never
// asked for is not, and an hour on the charger is explained. Everything here is computed on the tag's
// own terms (Unix time and the daily window on its UTC clock) and laid out on the study's calendar only
// at the end.

import type { ExperimentDetails } from './log.ts';
import { DAY_SECONDS, dayNumberToDate, localClock, wallClockToUnix } from './timezone.ts';

/** Stretches as [from, to, from, to, …], Unix seconds, in time order. */
export type Intervals = Float64Array | readonly number[];

/**
 * Whether the tag was scheduled to record at this instant: inside the deployment and, with daily hours,
 * inside them on its own clock. Mirrors the firmware's own test, including a window that runs past
 * midnight and one whose start and end are equal, which never stops it.
 */
export function isScheduledAt(details: ExperimentDetails, unixSeconds: number): boolean {
  if (unixSeconds < details.experimentStartTime || unixSeconds > details.experimentEndTime) return false;
  if (!details.useDailyTimes) return true;
  const timeOfDay = ((Math.floor(unixSeconds) % DAY_SECONDS) + DAY_SECONDS) % DAY_SECONDS;
  const { dailyStartTime: start, dailyEndTime: end } = details;
  if (start < end) return timeOfDay >= start && timeOfDay <= end;
  if (start > end) return timeOfDay >= start || timeOfDay <= end;
  return true;
}

/** The stretches between `from` and `to` the tag was scheduled to record. */
export function scheduledIntervals(details: ExperimentDetails, from: number, to: number): number[] {
  const lo = Math.max(from, details.experimentStartTime);
  const hi = Math.min(to, details.experimentEndTime);
  if (!(hi > lo)) return [];
  const { dailyStartTime: start, dailyEndTime: end } = details;
  if (!details.useDailyTimes || start === end) return [lo, hi];
  const out: number[] = [];
  for (let day = Math.floor(lo / DAY_SECONDS) - 1; day * DAY_SECONDS <= hi; day += 1) {
    const base = day * DAY_SECONDS;
    const windowFrom = base + start;
    const windowTo = start < end ? base + end : base + DAY_SECONDS + end;
    const a = Math.max(lo, windowFrom);
    const b = Math.min(hi, windowTo);
    if (b > a) out.push(a, b);
  }
  return out;
}

/** `base` with every stretch in `remove` taken out. Both in time order. */
export function subtractIntervals(base: Intervals, remove: Intervals): number[] {
  const out: number[] = [];
  for (let i = 0; i + 1 < base.length; i += 2) {
    let from = base[i]!;
    const to = base[i + 1]!;
    for (let j = 0; j + 1 < remove.length && from < to; j += 2) {
      const cutFrom = remove[j]!;
      const cutTo = remove[j + 1]!;
      if (cutTo <= from || cutFrom >= to) continue;
      if (cutFrom > from) out.push(from, cutFrom);
      from = Math.max(from, cutTo);
    }
    if (to > from) out.push(from, to);
  }
  return out;
}

/** Seconds of `intervals` falling between `from` and `to`. */
export function overlapSeconds(intervals: Intervals, from: number, to: number): number {
  let total = 0;
  for (let i = 0; i + 1 < intervals.length; i += 2) {
    const a = Math.max(from, intervals[i]!);
    const b = Math.min(to, intervals[i + 1]!);
    if (b > a) total += b - a;
  }
  return total;
}

/**
 * Stretches of at least `minSeconds` the tag was scheduled to record but was neither recording nor on
 * its charger, between `from` and `to`.
 */
export function missedStretches(
  details: ExperimentDetails, from: number, to: number, recording: Intervals, charging: Intervals, minSeconds: number,
): number[] {
  const missed = subtractIntervals(subtractIntervals(scheduledIntervals(details, from, to), recording), charging);
  const out: number[] = [];
  for (let i = 0; i + 1 < missed.length; i += 2) if (missed[i + 1]! - missed[i]! >= minSeconds) out.push(missed[i]!, missed[i + 1]!);
  return out;
}

/**
 * What one hour of one tag looked like.
 *
 *   ranging   recording, and in range of another tag for at least some of it
 *   alone     recording, but no other tag in range: not a fault, just nobody nearby
 *   charging  on the charger for most of it
 *   missed    scheduled, but neither recording nor charging: the thing to look for
 *   idle      inside the deployment, outside its daily hours
 *   after     after the logs end, so nothing can be said
 *   outside   before the deployment, after it, or an hour the clocks skipped
 */
export type CoverageState = 'ranging' | 'alone' | 'charging' | 'missed' | 'idle' | 'after' | 'outside';

export interface CoverageCell {
  /** Local hour of the day, 0-23. */
  readonly hour: number;
  readonly start: number;
  readonly end: number;
  /** Fractions of the hour scheduled, recording, and on the charger. */
  readonly scheduled: number;
  readonly recording: number;
  readonly charging: number;
  /** Ranging rounds that measured at least one other tag. */
  readonly rounds: number;
  /** Of the rounds the tag could have run while recording, the share that measured anyone; null when not recording. */
  readonly share: number | null;
  readonly state: CoverageState;
}

export interface CoverageDay {
  /** `YYYY-MM-DD` on the study's calendar. */
  readonly date: string;
  readonly cells: readonly CoverageCell[];
}

export interface CoverageInput {
  /** The study's IANA zone: rows are its days and columns its hours. */
  readonly timezone: string;
  readonly details: ExperimentDetails | null;
  /** The span to lay out, normally the deployment's first record to its last. */
  readonly from: number;
  readonly to: number;
  /** When the logs end: hours after it are unknown rather than missed. */
  readonly knownUntil: number;
  readonly recording: Intervals;
  readonly charging: Intervals;
  /** Ranging rounds that measured anyone, per minute: [Unix second the minute starts, rounds]. */
  readonly roundsByMinute: ReadonlyArray<readonly [number, number]>;
  /** Ranging rounds a tag runs in a second. */
  readonly roundsPerSecond: number;
}

/** A grid of every hour of the span, on the study's calendar, saying what the tag did in it. */
export function coverageGrid(input: CoverageInput): CoverageDay[] {
  const { timezone, details, from, to } = input;
  if (!(to > from)) return [];
  const clock = localClock(timezone, from, to);
  const minutes = new Map<number, number>();
  for (const [minute, count] of input.roundsByMinute) minutes.set(minute, (minutes.get(minute) ?? 0) + count);

  const days: CoverageDay[] = [];
  for (let day = clock.dayNumber(from); day <= clock.dayNumber(to); day += 1) {
    const date = dayNumberToDate(day);
    const starts = Array.from({ length: 25 }, (_, hour) =>
      hour < 24 ? wallClockToUnix(`${date}T${String(hour).padStart(2, '0')}:00`, timezone) : wallClockToUnix(`${dayNumberToDate(day + 1)}T00:00`, timezone));
    const cells: CoverageCell[] = [];
    for (let hour = 0; hour < 24; hour += 1) {
      const start = starts[hour]!;
      const end = starts[hour + 1]!;
      const length = end - start;
      // An hour the clocks skip has no length; it is drawn empty rather than left out, so columns line up.
      if (!(length > 0)) {
        cells.push({ hour, start, end: start, scheduled: 0, recording: 0, charging: 0, rounds: 0, share: null, state: 'outside' });
        continue;
      }
      let scheduledMinutes = 0;
      let rounds = 0;
      for (let minute = Math.floor(start / 60) * 60; minute < end; minute += 60) {
        if (details && isScheduledAt(details, minute + 30)) scheduledMinutes += 1;
        rounds += minutes.get(minute) ?? 0;
      }
      const scheduled = details ? Math.min(1, (scheduledMinutes * 60) / length) : 0;
      const recordingSeconds = overlapSeconds(input.recording, start, end);
      const recording = recordingSeconds / length;
      const charging = overlapSeconds(input.charging, start, end) / length;
      const share = recordingSeconds > 0 ? Math.min(1, rounds / (recordingSeconds * input.roundsPerSecond)) : null;
      const inDeployment = details ? end > details.experimentStartTime && start < details.experimentEndTime : start < to && end > from;
      let state: CoverageState;
      if (recording >= 0.05) state = share !== null && share >= 0.02 ? 'ranging' : 'alone';
      else if (charging >= 0.5) state = 'charging';
      else if (start >= input.knownUntil && inDeployment) state = 'after';
      else if (scheduled >= 0.5) state = 'missed';
      else if (inDeployment && details) state = 'idle';
      else state = 'outside';
      cells.push({ hour, start, end, scheduled, recording, charging, rounds, share, state });
    }
    days.push({ date, cells });
  }
  return days;
}
