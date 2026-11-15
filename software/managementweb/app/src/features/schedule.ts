// The deployment as a person enters it, and what the tags are told. Pure: no browser APIs.
//
// The draft holds exactly what was typed — wall-clock readings on the study's own clock, and the time
// zone they are on — and the configuration a tag receives is derived from it every time. Two things
// follow from keeping it that way round:
//
//   1. Changing the time zone reinterprets the times already entered, which is what anyone setting up
//      a study in another city expects, instead of quietly relabelling them.
//   2. A date field is never rewritten underneath someone typing into it. The value shown is the value
//      the browser reported, so typing a year digit by digit cannot be interrupted by a conversion.
//
// Nothing here, and nothing on screen, asks anyone to think about UTC.

import {
  clockChanges, deviceDailyWindow, describeTimezone, formatDateTime, formatDay, formatSpan, formatTimeOfDay,
  isValidTimezone, localDailyWindow, parseTimeOfDay, timeOfDayInput, unixToWallClock, validateDeployment,
  wallClockToUnix, type ClockChangePolicy, type DeploymentConfig, type Problem,
} from '@tottag/schema';

export interface DraftDevice {
  /** Full 6-byte EUI, least-significant byte first. */
  readonly eui: Uint8Array;
  readonly label: string;
  /**
   * Start time of a deployment this tag already holds that the person said may be replaced, without
   * its data having been downloaded here. Tied to that deployment, so a different one asks again.
   */
  readonly replaceAllowed?: number | null;
}

export interface DeploymentDraft {
  /** What the person calls it, for the record and its file name. */
  readonly name: string;
  /** IANA zone the times below are on. */
  readonly timezone: string;
  /** `YYYY-MM-DDTHH:MM` on the study's clock, exactly as the field reported it. */
  readonly start: string;
  readonly end: string;
  readonly useDailyTimes: boolean;
  /** `HH:MM` on the study's clock. */
  readonly dailyStart: string;
  readonly dailyEnd: string;
  /** Only consulted when the clocks change during the deployment. */
  readonly clockChangePolicy: ClockChangePolicy;
  readonly devices: readonly DraftDevice[];
}

/** The zone this computer is in, or UTC where the browser cannot say. */
export const localTimezone = (): string => Intl.DateTimeFormat().resolvedOptions().timeZone || 'UTC';

/**
 * A new deployment: from the coming midnight for a week, recording around the clock.
 *
 * The daily hours offered when someone turns them on are 7:00 AM to 10:00 PM, the same as the
 * desktop tool, so that the two start a study the same way.
 */
export function defaultDraft(now: number, timezone: string = localTimezone()): DeploymentDraft {
  const zone = isValidTimezone(timezone) ? timezone : 'UTC';
  const today = unixToWallClock(now, zone).slice(0, 10);
  const midnight = wallClockToUnix(`${today}T00:00`, zone);
  // A day later on the wall clock, whatever the clocks do overnight.
  const tomorrow = unixToWallClock(midnight + 36 * 3600, zone).slice(0, 10);
  const startUnix = wallClockToUnix(`${tomorrow}T00:00`, zone);
  const weekLater = unixToWallClock(startUnix + 7 * 86400 + 12 * 3600, zone).slice(0, 10);
  return {
    name: '',
    timezone: zone,
    start: `${tomorrow}T00:00`,
    end: `${weekLater}T00:00`,
    useDailyTimes: false,
    dailyStart: '07:00',
    dailyEnd: '22:00',
    clockChangePolicy: 'start',
    devices: [],
  };
}

/** A fresh deployment that keeps the settings a lab reuses study after study: the time zone and daily hours. */
export function nextDraft(previous: DeploymentDraft, now: number): DeploymentDraft {
  const fresh = defaultDraft(now, previous.timezone);
  return {
    ...fresh,
    useDailyTimes: previous.useDailyTimes,
    dailyStart: previous.dailyStart,
    dailyEnd: previous.dailyEnd,
    clockChangePolicy: previous.clockChangePolicy,
  };
}

/** What the tags are told. Times that are not yet complete come through as NaN, which validation reports. */
export function draftToConfig(draft: DeploymentDraft): DeploymentConfig {
  const zoneOk = isValidTimezone(draft.timezone);
  const startTime = zoneOk ? wallClockToUnix(draft.start, draft.timezone) : Number.NaN;
  const endTime = zoneOk ? wallClockToUnix(draft.end, draft.timezone) : Number.NaN;
  const localStart = parseTimeOfDay(draft.dailyStart);
  const localEnd = parseTimeOfDay(draft.dailyEnd);

  let dailyStartTime = Number.NaN;
  let dailyEndTime = Number.NaN;
  if (draft.useDailyTimes && zoneOk && Number.isFinite(localStart) && Number.isFinite(localEnd)) {
    // Until the dates are complete there is no deployment to place the hours in; today stands in, so
    // the hours still validate while the dates are being typed.
    const anchor = Number.isFinite(startTime) ? startTime : Date.now() / 1000;
    const until = Number.isFinite(endTime) && endTime > anchor ? endTime : anchor + 1;
    const window = deviceDailyWindow(localStart, localEnd, draft.timezone, anchor, until, draft.clockChangePolicy);
    dailyStartTime = window.start;
    dailyEndTime = window.end;
  }

  return {
    startTime,
    endTime,
    timezone: draft.timezone,
    useDailyTimes: draft.useDailyTimes,
    dailyStartTime: draft.useDailyTimes ? dailyStartTime : 0,
    dailyEndTime: draft.useDailyTimes ? dailyEndTime : 0,
    devices: draft.devices.map((device) => ({ eui: device.eui, label: device.label })),
  };
}

const TIME_CODES = new Set(['missing-start', 'missing-end', 'end-before-start', 'too-long', 'already-over', 'already-started']);

/** Everything wrong with the draft, worst first, including a time zone the browser does not recognise. */
export function draftProblems(draft: DeploymentDraft, now: number): Problem[] {
  const problems = validateDeployment(draftToConfig(draft), { now });
  if (isValidTimezone(draft.timezone)) return problems;
  // Without a zone no time can be placed, so complaints about the times would only be noise.
  return [
    {
      severity: 'error',
      code: 'bad-timezone',
      devices: [],
      message: draft.timezone.trim()
        ? `"${draft.timezone}" is not a time zone this browser recognizes. Pick one from the list, such as America/Chicago.`
        : 'Choose the time zone the study takes place in.',
    },
    ...problems.filter((problem) => !TIME_CODES.has(problem.code)),
  ];
}

export interface ClockChangeNotice {
  /** "Sunday, November 1". */
  readonly day: string;
  /** The hours asked for, e.g. "7:00 AM – 8:00 PM". */
  readonly asked: string;
  /** What each choice records before and after the change. */
  readonly options: Readonly<Record<ClockChangePolicy, { readonly before: string; readonly after: string }>>;
}

export interface ScheduleSummary {
  /** Both ends are complete and the zone is real. */
  readonly complete: boolean;
  readonly starts: string;
  readonly ends: string;
  readonly length: string;
  /** "7:00 AM – 8:00 PM each day" or "Around the clock". */
  readonly hours: string;
  /** "Central Time (America/Chicago)". */
  readonly zone: string;
  readonly clockChange: ClockChangeNotice | null;
}

const range = (start: number, end: number, locale?: string) => `${formatTimeOfDay(start, locale)} – ${formatTimeOfDay(end, locale)}`;

/** The deployment in words, the way the summary beside the form reads it back. */
export function describeSchedule(draft: DeploymentDraft, locale?: string): ScheduleSummary {
  const config = draftToConfig(draft);
  const complete = Number.isFinite(config.startTime) && Number.isFinite(config.endTime) && config.endTime > config.startTime;
  const zoneName = describeTimezone(draft.timezone, locale);
  const zone = zoneName === draft.timezone ? draft.timezone : `${zoneName} (${draft.timezone})`;
  const localStart = parseTimeOfDay(draft.dailyStart);
  const localEnd = parseTimeOfDay(draft.dailyEnd);
  const dailyOk = draft.useDailyTimes && Number.isFinite(localStart) && Number.isFinite(localEnd) && localStart !== localEnd;

  let clockChange: ClockChangeNotice | null = null;
  if (complete && dailyOk) {
    const changes = clockChanges(draft.timezone, config.startTime, config.endTime);
    const last = changes[changes.length - 1];
    if (last) {
      const shown = (policy: ClockChangePolicy) => {
        const window = deviceDailyWindow(localStart, localEnd, draft.timezone, config.startTime, config.endTime, policy);
        const before = localDailyWindow(window, draft.timezone, config.startTime);
        const after = localDailyWindow(window, draft.timezone, last.at);
        return { before: range(before.start, before.end, locale), after: range(after.start, after.end, locale) };
      };
      clockChange = {
        day: formatDay(last.at, draft.timezone, locale),
        asked: range(localStart, localEnd, locale),
        options: { start: shown('start'), end: shown('end'), cover: shown('cover') },
      };
    }
  }

  return {
    complete,
    starts: formatDateTime(config.startTime, draft.timezone, locale),
    ends: formatDateTime(config.endTime, draft.timezone, locale),
    length: complete ? formatSpan(config.endTime - config.startTime) : '—',
    hours: !draft.useDailyTimes ? 'Around the clock' : dailyOk ? `${range(localStart, localEnd, locale)} each day` : '—',
    zone,
    clockChange,
  };
}

/** A file-name-safe version of whatever the deployment is called, or of its labels' shared prefix. */
export function recordBaseName(draft: DeploymentDraft): string {
  const safe = (text: string) => text.normalize('NFD').replace(/[̀-ͯ]/g, '').replace(/[^A-Za-z0-9._-]+/g, '-').replace(/^[-.]+|[-.]+$/g, '');
  const named = safe(draft.name);
  if (named) return named;
  // "10051_CG1", "10051_TC" → "10051": labels in a study usually start with the family or site ID.
  const labels = draft.devices.map((device) => device.label.trim()).filter(Boolean);
  if (labels.length) {
    let prefix = labels[0]!;
    for (const label of labels.slice(1)) while (!label.startsWith(prefix)) prefix = prefix.slice(0, -1);
    const shared = safe(prefix.replace(/[_\s-]+$/, ''));
    if (shared.length >= 2) return shared;
  }
  return 'tottag-deployment';
}

/** Moves a v1 draft (Unix times and daily hours on the tag's clock) to what is typed today. */
export function migrateV1Draft(stored: {
  startTime: number | null; endTime: number | null; timezone: string; useDailyTimes: boolean;
  dailyStartTime: number; dailyEndTime: number; devices: Array<{ eui: number[]; label: string }>;
}): DeploymentDraft {
  const zone = isValidTimezone(stored.timezone) ? stored.timezone : localTimezone();
  const start = typeof stored.startTime === 'number' ? stored.startTime : Number.NaN;
  const end = typeof stored.endTime === 'number' ? stored.endTime : Number.NaN;
  const anchor = Number.isFinite(start) ? start : Date.now() / 1000;
  const local = localDailyWindow({ start: stored.dailyStartTime, end: stored.dailyEndTime }, zone, anchor);
  return {
    name: '',
    timezone: zone,
    start: unixToWallClock(start, zone),
    end: unixToWallClock(end, zone),
    useDailyTimes: stored.useDailyTimes,
    dailyStart: stored.useDailyTimes ? timeOfDayInput(local.start) : '07:00',
    dailyEnd: stored.useDailyTimes ? timeOfDayInput(local.end) : '22:00',
    clockChangePolicy: 'start',
    devices: stored.devices.map((device) => ({ eui: Uint8Array.from(device.eui), label: device.label })),
  };
}
