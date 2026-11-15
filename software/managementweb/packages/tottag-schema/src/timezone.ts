// Wall-clock time in a named time zone, and what a tag has to be told so that it records at the right
// local hours.
//
// Pure. Built on the platform's own IANA database through Intl, so the package carries no timezone
// dependency and never disagrees with the browser about when clocks change.
//
// The reason this exists: a tag keeps one clock, in seconds since 1970, and compares its daily window
// against seconds past midnight on that clock. People think in local time. Every conversion between
// the two happens here, so that nobody setting up a deployment ever has to.

/** Seconds in a day. The firmware's daily window is a pair of values in [0, DAY_SECONDS). */
export const DAY_SECONDS = 86_400;

/** Earliest and latest years accepted in a date field. A tag's clock is an unsigned 32-bit count of seconds. */
export const EARLIEST_YEAR = 2020;
export const LATEST_YEAR = 2105;

const WALL_CLOCK = /^(\d{4})-(\d{2})-(\d{2})T(\d{2}):(\d{2})$/;
const TIME_OF_DAY = /^(\d{2}):(\d{2})(?::\d{2})?$/;

export function isValidTimezone(timezone: string): boolean {
  if (!timezone) return false;
  try {
    new Intl.DateTimeFormat('en-US', { timeZone: timezone });
    return true;
  } catch {
    return false;
  }
}

/** Every IANA zone the runtime knows, or a short practical list where it cannot say. */
export function supportedTimezones(): string[] {
  const intl = Intl as unknown as { supportedValuesOf?: (key: string) => string[] };
  if (typeof intl.supportedValuesOf === 'function') return intl.supportedValuesOf('timeZone');
  return ['UTC', 'America/New_York', 'America/Chicago', 'America/Denver', 'America/Los_Angeles', 'Europe/London'];
}

const partFormatters = new Map<string, Intl.DateTimeFormat>();

/** [year, month 1-12, day, hour 0-23, minute, second] of an instant, as a clock in `timezone` reads it. */
function wallParts(timezone: string, unixSeconds: number): [number, number, number, number, number, number] {
  let formatter = partFormatters.get(timezone);
  if (!formatter) {
    formatter = new Intl.DateTimeFormat('en-US', {
      timeZone: timezone, hourCycle: 'h23',
      year: 'numeric', month: '2-digit', day: '2-digit', hour: '2-digit', minute: '2-digit', second: '2-digit',
    });
    partFormatters.set(timezone, formatter);
  }
  const parts: Record<string, number> = {};
  for (const { type, value } of formatter.formatToParts(new Date(unixSeconds * 1000))) {
    if (type !== 'literal') parts[type] = Number(value);
  }
  // Some engines still render midnight as hour 24 despite the hour cycle asked for.
  return [parts.year!, parts.month!, parts.day!, parts.hour === 24 ? 0 : parts.hour!, parts.minute!, parts.second!];
}

/**
 * `Date.UTC` without its two-digit-year rule, in seconds.
 *
 * `Date.UTC(2, …)` is 1902. The first digit typed into a date field's year arrives as year 0002, and
 * must not be mistaken for a date in the twentieth century.
 */
function utcSeconds(year: number, month: number, day: number, hour: number, minute: number): number {
  const date = new Date(0);
  date.setUTCFullYear(year, month - 1, day);
  date.setUTCHours(hour, minute, 0, 0);
  return date.getTime() / 1000;
}

/** How far `timezone` is ahead of UTC at that instant, in seconds. Negative west of Greenwich. */
export function utcOffsetSecondsAt(timezone: string, unixSeconds: number): number {
  const [year, month, day, hour, minute, second] = wallParts(timezone, unixSeconds);
  const asIfUtc = utcSeconds(year, month, day, hour, minute) + second;
  // Rounded to the minute, absorbing the sub-minute historical offsets some zones carry.
  return Math.round((asIfUtc - Math.floor(unixSeconds)) / 60) * 60;
}

/**
 * The instant a `YYYY-MM-DDTHH:MM` wall-clock reading names in `timezone`. NaN when it is not a
 * complete, real date and time.
 *
 * A reading that falls in the hour skipped when clocks go forward is taken as the instant an hour
 * later, which is what the clock on the wall would show by then.
 */
export function wallClockToUnix(value: string, timezone: string): number {
  const match = WALL_CLOCK.exec(value);
  if (!match || !isValidTimezone(timezone)) return Number.NaN;
  const [year, month, day, hour, minute] = match.slice(1).map(Number) as [number, number, number, number, number];
  if (year < EARLIEST_YEAR || year > LATEST_YEAR || month < 1 || month > 12 || hour > 23 || minute > 59) return Number.NaN;
  const naive = utcSeconds(year, month, day, hour, minute);
  // 31 February rolls over into March in Date arithmetic; a real date survives the round trip.
  if (new Date(naive * 1000).getUTCDate() !== day) return Number.NaN;
  // The offset depends on the instant and the instant on the offset. Reading the offset again at the
  // first estimate settles it, which only matters either side of a clock change.
  const estimated = utcOffsetSecondsAt(timezone, naive);
  const firstPass = naive - estimated;
  const settled = utcOffsetSecondsAt(timezone, firstPass);
  const candidates = settled === estimated ? [firstPass] : [firstPass, naive - settled];
  for (const candidate of candidates) {
    if (unixToWallClock(candidate, timezone) === value) return candidate;
  }
  // Inside the hour the clocks skip, no instant reads this way; the later one is where the clock is by then.
  return Math.max(...candidates);
}

/** The `YYYY-MM-DDTHH:MM` reading of an instant on a clock in `timezone`. Empty for a non-finite instant. */
export function unixToWallClock(unixSeconds: number, timezone: string): string {
  if (!Number.isFinite(unixSeconds) || !isValidTimezone(timezone)) return '';
  const [year, month, day, hour, minute] = wallParts(timezone, unixSeconds);
  const pad = (value: number) => String(value).padStart(2, '0');
  // Four digits whatever the year: a datetime-local field rejects "2-10-15".
  return `${String(year).padStart(4, '0')}-${pad(month)}-${pad(day)}T${pad(hour)}:${pad(minute)}`;
}

/** Seconds past midnight for `HH:MM`, or NaN when it is not a complete time of day. */
export function parseTimeOfDay(value: string): number {
  const match = TIME_OF_DAY.exec(value);
  if (!match) return Number.NaN;
  const hour = Number(match[1]);
  const minute = Number(match[2]);
  if (hour > 23 || minute > 59) return Number.NaN;
  return hour * 3600 + minute * 60;
}

/** `HH:MM` for seconds past midnight, wrapped into one day. */
export function timeOfDayInput(seconds: number): string {
  const wrapped = ((Math.round(seconds) % DAY_SECONDS) + DAY_SECONDS) % DAY_SECONDS;
  return `${String(Math.floor(wrapped / 3600)).padStart(2, '0')}:${String(Math.floor((wrapped % 3600) / 60)).padStart(2, '0')}`;
}

const wrapDay = (seconds: number): number => ((seconds % DAY_SECONDS) + DAY_SECONDS) % DAY_SECONDS;

/** A change of the clocks in `timezone`: when, and the offsets either side of it. */
export interface ClockChange {
  /** The first whole minute on the new offset, in Unix seconds. */
  readonly at: number;
  readonly offsetBefore: number;
  readonly offsetAfter: number;
}

/** Every change of the clocks in `timezone` strictly inside [start, end). Empty for an unusable span. */
export function clockChanges(timezone: string, start: number, end: number): ClockChange[] {
  if (!Number.isFinite(start) || !Number.isFinite(end) || end <= start || !isValidTimezone(timezone)) return [];
  const changes: ClockChange[] = [];
  // Hourly is fine enough to see every change in practice; the minute is then found by bisection.
  const STEP = 3600;
  let previousAt = start;
  let previous = utcOffsetSecondsAt(timezone, start);
  for (let at = start + STEP; at < end + STEP; at += STEP) {
    const probe = Math.min(at, end - 1);
    const offset = utcOffsetSecondsAt(timezone, probe);
    if (offset !== previous) {
      let low = previousAt;
      let high = probe;
      while (high - low > 60) {
        const middle = Math.floor((low + high) / 2);
        if (utcOffsetSecondsAt(timezone, middle) === previous) low = middle;
        else high = middle;
      }
      changes.push({ at: high, offsetBefore: previous, offsetAfter: offset });
      previous = offset;
    }
    previousAt = probe;
    if (probe >= end - 1) break;
  }
  return changes;
}

/**
 * What to do with a daily window when the clocks change during a deployment.
 *
 * A tag cannot follow a change of the clocks: its window is fixed on its own clock, so on one side of
 * the change it runs an hour away from the local hours asked for. Which side keeps the hours asked
 * for is a choice only the person running the study can make, because it trades data against what
 * participants agreed to.
 *
 *   start — the hours asked for until the change, shifted after it
 *   end   — shifted until the change, the hours asked for after it
 *   cover — wide enough to include the hours asked for on every day, at the cost of recording an
 *           extra hour on each day (before the change on one end, after it on the other)
 */
export type ClockChangePolicy = 'start' | 'end' | 'cover';

/** A daily window as a tag stores it: seconds past midnight on the tag's own clock. */
export interface DeviceDailyWindow {
  readonly start: number;
  readonly end: number;
}

/**
 * The window a tag must be given so that it records from `localStart` to `localEnd` (seconds past
 * local midnight in `timezone`) during a deployment running from `start` to `end`.
 */
export function deviceDailyWindow(
  localStart: number,
  localEnd: number,
  timezone: string,
  start: number,
  end: number,
  policy: ClockChangePolicy,
): DeviceDailyWindow {
  const offsetStart = utcOffsetSecondsAt(timezone, start);
  const changes = clockChanges(timezone, start, end);
  const offsetEnd = changes.length ? changes[changes.length - 1]!.offsetAfter : offsetStart;
  const at = (offset: number, from: number, to: number): DeviceDailyWindow => ({ start: wrapDay(from - offset), end: wrapDay(to - offset) });

  if (!changes.length || policy === 'start') return at(offsetStart, localStart, localEnd);
  if (policy === 'end') return at(offsetEnd, localStart, localEnd);

  // 'cover': widen the window by however far the clocks move, on whichever end needs it, so that
  // both before and after the change it still includes the hours asked for.
  const delta = offsetEnd - offsetStart;
  const length = wrapDay(localEnd - localStart) || DAY_SECONDS;
  if (length + Math.abs(delta) >= DAY_SECONDS) return at(offsetStart, localStart, localEnd);
  return at(offsetStart, localStart - Math.max(0, delta), localEnd + Math.max(0, -delta));
}

/** Where a tag's daily window falls on the local clock at a given instant, as seconds past local midnight. */
export function localDailyWindow(window: DeviceDailyWindow, timezone: string, atUnix: number): DeviceDailyWindow {
  const offset = utcOffsetSecondsAt(timezone, atUnix);
  return { start: wrapDay(window.start + offset), end: wrapDay(window.end + offset) };
}

// --- Saying it to people ----------------------------------------------------------------------------

/** "Sat, Oct 10, 2026, 7:00 AM", on a clock in `timezone`, in the reader's locale. */
export function formatDateTime(unixSeconds: number, timezone: string, locale?: string): string {
  if (!Number.isFinite(unixSeconds) || !isValidTimezone(timezone)) return '—';
  return new Intl.DateTimeFormat(locale, {
    timeZone: timezone, weekday: 'short', year: 'numeric', month: 'short', day: 'numeric', hour: 'numeric', minute: '2-digit',
  }).format(new Date(unixSeconds * 1000));
}

/** "Sunday, November 1", on a clock in `timezone`. */
export function formatDay(unixSeconds: number, timezone: string, locale?: string): string {
  if (!Number.isFinite(unixSeconds) || !isValidTimezone(timezone)) return '—';
  return new Intl.DateTimeFormat(locale, { timeZone: timezone, weekday: 'long', month: 'long', day: 'numeric' })
    .format(new Date(unixSeconds * 1000));
}

/** "Oct 10, 2026", on a clock in `timezone`. */
export function formatDate(unixSeconds: number, timezone: string, locale?: string): string {
  if (!Number.isFinite(unixSeconds) || !isValidTimezone(timezone)) return '—';
  return new Intl.DateTimeFormat(locale, { timeZone: timezone, year: 'numeric', month: 'short', day: 'numeric' })
    .format(new Date(unixSeconds * 1000));
}

/** "7:00 AM" (or "07:00", by locale) for seconds past midnight. */
export function formatTimeOfDay(seconds: number, locale?: string): string {
  if (!Number.isFinite(seconds)) return '—';
  const wrapped = wrapDay(Math.round(seconds));
  return new Intl.DateTimeFormat(locale, { timeZone: 'UTC', hour: 'numeric', minute: '2-digit' })
    .format(new Date(wrapped * 1000));
}

/** "Central Time" where the runtime can name the zone that way, otherwise its IANA name. */
export function describeTimezone(timezone: string, locale?: string): string {
  if (!isValidTimezone(timezone)) return timezone;
  try {
    const parts = new Intl.DateTimeFormat(locale, { timeZone: timezone, timeZoneName: 'longGeneric' }).formatToParts(new Date());
    const name = parts.find((part) => part.type === 'timeZoneName')?.value;
    if (name && !/^GMT[+-]/.test(name)) return name;
  } catch {
    // Older engines without 'longGeneric'.
  }
  return timezone;
}

/** "2 days, 14 hours", "5 hours", "45 minutes". */
export function formatSpan(seconds: number): string {
  if (!Number.isFinite(seconds) || seconds <= 0) return '—';
  const days = Math.floor(seconds / DAY_SECONDS);
  const hours = Math.floor((seconds % DAY_SECONDS) / 3600);
  const minutes = Math.floor((seconds % 3600) / 60);
  const unit = (count: number, word: string) => `${count} ${word}${count === 1 ? '' : 's'}`;
  const parts: string[] = [];
  if (days) parts.push(unit(days, 'day'));
  if (hours) parts.push(unit(hours, 'hour'));
  if (!days && minutes) parts.push(unit(minutes, 'minute'));
  return parts.join(', ') || 'under a minute';
}

// --- Many instants on one clock -------------------------------------------------------------------------

/**
 * A study's clock over one span, fast enough to place a million instants.
 *
 * `utcOffsetSecondsAt` asks Intl for every instant, which is right for a handful of times on a screen
 * and far too slow for every ranging round in a week. The offset only changes when the clocks do, so
 * this finds those changes once and answers from them, falling back to Intl outside the span it was
 * built for.
 */
export interface LocalClock {
  readonly timezone: string;
  /** Seconds the zone is ahead of UTC at this instant. */
  offsetAt(unixSeconds: number): number;
  /** Days since 1970-01-01 on the local calendar: the same number for every instant of one local date. */
  dayNumber(unixSeconds: number): number;
}

export function localClock(timezone: string, from: number, to: number): LocalClock {
  const lo = Math.floor(Math.min(from, to));
  const hi = Math.ceil(Math.max(from, to));
  const first = utcOffsetSecondsAt(timezone, lo);
  const changes = clockChanges(timezone, lo, hi + 1);
  const offsetAt = (unixSeconds: number): number => {
    if (!(unixSeconds >= lo && unixSeconds <= hi)) return utcOffsetSecondsAt(timezone, unixSeconds);
    let offset = first;
    for (const change of changes) {
      if (unixSeconds < change.at) break;
      offset = change.offsetAfter;
    }
    return offset;
  };
  return {
    timezone,
    offsetAt,
    dayNumber: (unixSeconds) => Math.floor((unixSeconds + offsetAt(unixSeconds)) / DAY_SECONDS),
  };
}

/** `YYYY-MM-DD` for a local day number from `LocalClock.dayNumber`. */
export function dayNumberToDate(day: number): string {
  return new Date(day * DAY_SECONDS * 1000).toISOString().slice(0, 10);
}

/** The instant a local day begins: midnight, or the first minute after it where the clocks skip midnight. */
export function localDayStart(day: number, timezone: string): number {
  return wallClockToUnix(`${dayNumberToDate(day)}T00:00`, timezone);
}
