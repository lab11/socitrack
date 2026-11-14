// Where the ticks go on a time axis, and how times read on it. Pure.
//
// Ticks fall on round times of the study's own clock — every hour on the hour, every day at midnight —
// in the study's time zone, never the computer's and never UTC. A tick that would land in an hour the
// clocks skip is left out, and one in an hour they repeat is drawn once.

import { dayNumberToDate, localClock, localDayStart, type LocalClock } from '@tottag/schema';

export interface TimeTick {
  /** Unix seconds. */
  readonly t: number;
  /** The time of day, or the date for ticks a day or more apart. */
  readonly label: string;
  /** The date, on the first tick and wherever the date changes; null otherwise. */
  readonly date: string | null;
}

const MINUTE = 60;
const HOUR = 3600;
const DAY = 86_400;
/** Spacings a person reads easily, smallest first. */
const STEPS = [1, 2, 5, 10, 15, 30, MINUTE, 2 * MINUTE, 5 * MINUTE, 10 * MINUTE, 15 * MINUTE, 30 * MINUTE,
  HOUR, 2 * HOUR, 3 * HOUR, 6 * HOUR, 12 * HOUR, DAY, 2 * DAY, 7 * DAY, 14 * DAY, 28 * DAY];

/** The smallest readable spacing that keeps the tick count at or under `maxTicks`. */
export function tickStep(spanSeconds: number, maxTicks: number): number {
  const wanted = spanSeconds / Math.max(1, maxTicks);
  return STEPS.find((step) => step >= wanted) ?? Math.ceil(wanted / (28 * DAY)) * 28 * DAY;
}

const formatters = new Map<string, Intl.DateTimeFormat>();
function formatter(timezone: string, options: Intl.DateTimeFormatOptions, locale?: string): Intl.DateTimeFormat {
  const key = `${locale ?? ''}|${timezone}|${JSON.stringify(options)}`;
  let found = formatters.get(key);
  if (!found) {
    found = new Intl.DateTimeFormat(locale, { timeZone: timezone, ...options });
    formatters.set(key, found);
  }
  return found;
}

/** "7:05 AM", or "7:05:30 AM" with seconds, on the study's clock. */
export function formatClock(t: number, timezone: string, seconds = false, locale?: string): string {
  return formatter(timezone, { hour: 'numeric', minute: '2-digit', ...(seconds ? { second: '2-digit' } : {}) }, locale).format(new Date(t * 1000));
}

/** "Mon, Aug 24", on the study's clock. */
export function formatShortDate(t: number, timezone: string, locale?: string): string {
  return formatter(timezone, { weekday: 'short', month: 'short', day: 'numeric' }, locale).format(new Date(t * 1000));
}

/** "Mon, Aug 24, 2026, 7:05:30 AM", for a readout where the exact second matters. */
export function formatInstant(t: number, timezone: string, seconds = true, locale?: string): string {
  return formatter(timezone, {
    weekday: 'short', year: 'numeric', month: 'short', day: 'numeric', hour: 'numeric', minute: '2-digit',
    ...(seconds ? { second: '2-digit' } : {}),
  }, locale).format(new Date(t * 1000));
}

/**
 * Ticks for the span `from`–`to`, at most about `maxTicks` of them, on round local times.
 *
 * `clock` may be passed when the caller already has one for a wider span.
 */
export function timeTicks(from: number, to: number, timezone: string, maxTicks: number, clock?: LocalClock, locale?: string): TimeTick[] {
  if (!(to > from)) return [];
  const local = clock ?? localClock(timezone, from, to);
  const step = tickStep(to - from, maxTicks);
  const instants: number[] = [];

  if (step < DAY) {
    // Step through local readings and turn each back into an instant. Reading the offset at the
    // estimate and again at the result settles it either side of a change of the clocks.
    let reading = Math.ceil((from + local.offsetAt(from)) / step) * step;
    for (let guard = 0; guard < 10_000; guard += 1, reading += step) {
      const estimate = reading - local.offsetAt(reading - local.offsetAt(from));
      const instant = reading - local.offsetAt(estimate);
      if (instant > to) break;
      // Inside the hour the clocks skip, no instant reads this way.
      if (instant + local.offsetAt(instant) !== reading) continue;
      if (instant >= from && (instants.length === 0 || instant > instants[instants.length - 1]!)) instants.push(instant);
    }
  } else {
    // Weekly ticks fall on Mondays: day 0 was a Thursday, so Monday is the day for which day + 3 divides by 7.
    const days = step / DAY;
    const shift = days % 7 === 0 ? 3 : 0;
    for (let day = local.dayNumber(from); ; day += 1) {
      const midnight = localDayStart(day, timezone);
      if (midnight > to) break;
      if (midnight >= from && (day + shift) % days === 0) instants.push(midnight);
    }
  }

  let previousDay: number | null = null;
  return instants.map((t) => {
    const day = local.dayNumber(t);
    const changed = previousDay === null || day !== previousDay;
    previousDay = day;
    return step >= DAY
      ? { t, label: formatShortDate(t, timezone, locale), date: null }
      : { t, label: formatClock(t, timezone, step < MINUTE, locale), date: changed ? formatShortDate(t, timezone, locale) : null };
  });
}

/** Round values for a value axis: about `count` of them covering `low`–`high`. */
export function valueTicks(low: number, high: number, count = 4): number[] {
  if (!Number.isFinite(low) || !Number.isFinite(high)) return [];
  if (high === low) return [low];
  const rough = (high - low) / Math.max(1, count);
  const magnitude = 10 ** Math.floor(Math.log10(rough));
  const step = [1, 2, 2.5, 5, 10].map((m) => m * magnitude).find((candidate) => candidate >= rough) ?? 10 * magnitude;
  const ticks: number[] = [];
  for (let value = Math.ceil(low / step) * step; value <= high + step * 1e-9; value += step) {
    ticks.push(Number(value.toFixed(10)));
  }
  return ticks;
}

/** The local calendar date of an instant, `YYYY-MM-DD`, for keys and file names. */
export function localDate(t: number, clock: LocalClock): string {
  return dayNumberToDate(clock.dayNumber(t));
}
