// The record of what was actually deployed.
//
// The question this answers is asked months later, usually when something in the data looks wrong:
// "what exactly did we put on these tags?" Until now the only answer was the tags themselves, which
// by then have been reconfigured, and a researcher's memory.
//
// Three properties matter more than completeness:
//
//   1. It records what the TAG CONFIRMED, not what the app sent. Every field here is read back off
//      the tag after writing and decoded by the same parser that reads a log, so a tag that silently
//      accepted something different is visible rather than assumed away.
//   2. It is readable without this app. JSON with spelled-out fields and a plain-text companion, not
//      a binary blob — the failure mode being guarded against is a format nobody can open in 2030.
//   3. Times are given as the people running the study think of them: on the local clock, with the
//      time zone named. The exact bytes each tag was given are kept alongside, so an argument about
//      an hour a year later can still be settled from this file alone.

import { formatEui, shortUidHex, tagName, type DeploymentConfig } from './deployment.ts';
import type { ExperimentDetails } from './log.ts';
import {
  clockChanges, formatDateTime, formatDay, formatSpan, formatTimeOfDay, isValidTimezone, localDailyWindow,
  timeOfDayInput, unixToWallClock,
} from './timezone.ts';

export const MANIFEST_KIND = 'tottag.deployment-manifest';
/** 2: local times with the zone named, a deployment name, and outcomes for tags that were not written. */
export const MANIFEST_VERSION = 2;

/**
 * What happened when a tag was written to.
 *
 * `skipped` is a tag deliberately left alone, because it still held a deployment whose data nobody
 * had said was safe to lose.
 */
export type WriteOutcome = 'confirmed' | 'mismatch' | 'unconfirmed' | 'failed' | 'skipped';

export interface DeviceManifestEntry {
  /** "TotTag 00:0A", as the tag is named on screen. */
  readonly name: string;
  readonly eui: string;
  readonly shortUid: string;
  readonly label: string;
  readonly outcome: WriteOutcome;
  /** What went wrong, or how the read-back differed. Null when confirmed. */
  readonly detail: string | null;
  /** Battery when it was written to, in mV, where it was read. */
  readonly batteryMv?: number | undefined;
  /** Firmware and hardware strings read from the tag, when it reported them. */
  readonly firmware?: string | undefined;
  readonly hardware?: string | undefined;
}

export interface DailyHours {
  /** "HH:MM" on the local clock. */
  readonly start: string;
  readonly end: string;
}

export interface DeploymentManifest {
  readonly kind: typeof MANIFEST_KIND;
  readonly manifestVersion: number;
  /** The instant it was written, as an ISO timestamp. */
  readonly writtenAt: string;
  /** What the person setting it up called it, if anything. */
  readonly name: string | null;
  readonly deployment: {
    /** IANA zone every local time in this record is on. */
    readonly timezone: string;
    /** "YYYY-MM-DDTHH:MM" on the local clock. */
    readonly start: string;
    readonly end: string;
    /** Unix seconds: the same instants, unambiguous. */
    readonly startTime: number;
    readonly endTime: number;
    readonly durationDays: number;
    /** Null when the tags record around the clock. */
    readonly dailyHours: DailyHours | null;
    /** When the clocks change during the deployment, the hours the tags record from then on. */
    readonly afterClockChange: ReadonlyArray<{ readonly from: string; readonly dailyHours: DailyHours | null }>;
  };
  readonly devices: readonly DeviceManifestEntry[];
  /** Hex of the exact 239 bytes written, so the record can be replayed or compared byte for byte. */
  readonly experimentDetailsHex: string;
  readonly summary: string;
}

const hex = (bytes: Uint8Array): string =>
  Array.from(bytes).map((b) => b.toString(16).padStart(2, '0')).join('');

export interface DeviceWriteResult {
  readonly eui: Uint8Array;
  readonly label: string;
  readonly outcome: WriteOutcome;
  readonly detail?: string | null;
  readonly batteryMv?: number | null | undefined;
  readonly firmware?: string | undefined;
  readonly hardware?: string | undefined;
}

/** The local hours a tag with this configuration records at, as of a given instant. */
function hoursAt(config: DeploymentConfig, atUnix: number): DailyHours | null {
  if (!config.useDailyTimes) return null;
  const local = localDailyWindow({ start: config.dailyStartTime, end: config.dailyEndTime }, config.timezone, atUnix);
  return { start: timeOfDayInput(local.start), end: timeOfDayInput(local.end) };
}

export function buildManifest(
  config: DeploymentConfig,
  detailsBytes: Uint8Array,
  results: readonly DeviceWriteResult[],
  writtenAt: Date,
  name: string | null = null,
): DeploymentManifest {
  const confirmed = results.filter((r) => r.outcome === 'confirmed').length;
  const failed = results.filter((r) => r.outcome === 'failed' || r.outcome === 'mismatch').length;
  const unconfirmed = results.filter((r) => r.outcome === 'unconfirmed').length;
  const skipped = results.filter((r) => r.outcome === 'skipped').length;

  const tags = (count: number) => `${count} tag${count === 1 ? '' : 's'}`;
  const parts = [`${confirmed} of ${tags(results.length)} confirmed the deployment.`];
  if (failed) parts.push(`${tags(failed)} did not take it.`);
  if (unconfirmed) parts.push(`${tags(unconfirmed)} ${unconfirmed === 1 ? 'was' : 'were'} written but could not be read back to check.`);
  if (skipped) parts.push(`${tags(skipped)} ${skipped === 1 ? 'was' : 'were'} left alone because ${skipped === 1 ? 'it' : 'they'} still held an earlier deployment.`);
  // Fixed locale, so the same deployment always produces the same file.
  parts.push(
    `Runs ${formatDateTime(config.startTime, config.timezone, 'en-US')} to ${formatDateTime(config.endTime, config.timezone, 'en-US')} ` +
      `(${config.timezone}).`,
  );

  const changes = clockChanges(config.timezone, config.startTime, config.endTime);
  return {
    kind: MANIFEST_KIND,
    manifestVersion: MANIFEST_VERSION,
    writtenAt: writtenAt.toISOString(),
    name: name?.trim() || null,
    deployment: {
      timezone: config.timezone,
      start: unixToWallClock(config.startTime, config.timezone),
      end: unixToWallClock(config.endTime, config.timezone),
      startTime: config.startTime,
      endTime: config.endTime,
      durationDays: Number(((config.endTime - config.startTime) / 86400).toFixed(4)),
      dailyHours: hoursAt(config, config.startTime),
      afterClockChange: changes.map((change) => ({
        from: unixToWallClock(change.at, config.timezone),
        dailyHours: hoursAt(config, change.at),
      })),
    },
    devices: results.map((result) => ({
      name: tagName(result.eui),
      eui: formatEui(result.eui),
      shortUid: `0x${shortUidHex(result.eui)}`,
      label: result.label,
      outcome: result.outcome,
      detail: result.detail ?? null,
      batteryMv: result.batteryMv ?? undefined,
      firmware: result.firmware,
      hardware: result.hardware,
    })),
    experimentDetailsHex: hex(detailsBytes),
    summary: parts.join(' '),
  };
}

/**
 * Compare what a tag reports back against what was sent, in sentences a person can act on.
 *
 * Only the fields the tag actually stores are compared. The time zone is not among them — it is kept
 * in the record for people, and expecting it back would report a mismatch on every tag.
 */
export function diffReadback(config: DeploymentConfig, readback: ExperimentDetails): string[] {
  const differences: string[] = [];
  const check = (what: string, expected: number | boolean, actual: number | boolean) => {
    if (expected !== actual) differences.push(`It stored a different ${what} from the one sent.`);
  };
  check('start time', config.startTime, readback.experimentStartTime);
  check('end time', config.endTime, readback.experimentEndTime);
  check('choice of daily hours', config.useDailyTimes, readback.useDailyTimes);
  if (config.useDailyTimes) {
    check('daily start time', ((config.dailyStartTime % 86400) + 86400) % 86400, readback.dailyStartTime);
    check('daily end time', ((config.dailyEndTime % 86400) + 86400) % 86400, readback.dailyEndTime);
  }
  if (config.devices.length !== readback.numDevices) {
    differences.push(`It stored ${readback.numDevices} tag(s) in the deployment where ${config.devices.length} were sent.`);
  }

  for (let index = 0; index < Math.min(config.devices.length, readback.numDevices); index += 1) {
    const sent = config.devices[index]!;
    const got = readback.uids[index];
    if (!got || formatEui(sent.eui) !== formatEui(got)) {
      differences.push(`Tag ${index + 1} in its list is ${got ? tagName(got) : 'missing'} where ${tagName(sent.eui)} was sent.`);
    }
    // The tag stores a fixed-width field, so compare against what would survive the trip.
    const expectedLabel = new TextDecoder()
      .decode(new TextEncoder().encode(sent.label).subarray(0, 16))
      .replace(/\0+$/, '');
    if (expectedLabel !== (readback.labels[index] ?? '')) {
      differences.push(`It stored the label "${readback.labels[index] ?? ''}" where "${expectedLabel}" was sent.`);
    }
  }
  if (readback.isTerminated) differences.push('It reports the deployment as already canceled.');
  return differences;
}

const OUTCOME_WORDS: Record<WriteOutcome, string> = {
  confirmed: 'written and confirmed',
  mismatch: 'NOT CONFIRMED: stored something different',
  unconfirmed: 'written, but could not be checked',
  failed: 'NOT WRITTEN',
  skipped: 'NOT WRITTEN: left alone',
};

/** A plain-text companion, for pasting into a lab notebook. Times on the deployment's own clock. */
export function manifestToText(manifest: DeploymentManifest, locale?: string): string {
  const { deployment } = manifest;
  const zone = deployment.timezone;
  const hours = (daily: DailyHours | null) => {
    if (!daily) return 'all day, every day';
    const [sh = 0, sm = 0] = daily.start.split(':').map(Number);
    const [eh = 0, em = 0] = daily.end.split(':').map(Number);
    return `${formatTimeOfDay(sh * 3600 + sm * 60, locale)} to ${formatTimeOfDay(eh * 3600 + em * 60, locale)} each day`;
  };
  const writtenAt = Date.parse(manifest.writtenAt) / 1000;
  const lines = [
    manifest.name ? `TotTag deployment record: ${manifest.name}` : 'TotTag deployment record',
    '='.repeat(60),
    `Written:    ${formatDateTime(writtenAt, zone, locale)}`,
    `Starts:     ${formatDateTime(deployment.startTime, zone, locale)}`,
    `Ends:       ${formatDateTime(deployment.endTime, zone, locale)}`,
    `Length:     ${formatSpan(deployment.endTime - deployment.startTime)}`,
    `Time zone:  ${zone}`,
    `Records:    ${hours(deployment.dailyHours)}`,
  ];
  for (const change of deployment.afterClockChange) {
    const at = Date.parse(`${change.from}:00Z`) / 1000;
    lines.push(`            The clocks change on ${formatDay(at, 'UTC', locale)}; from then on, ${hours(change.dailyHours)}.`);
  }
  lines.push('', 'Tags', '-'.repeat(60));
  for (const device of manifest.devices) {
    lines.push(
      `  ${device.name}  ${(device.label || '(no label)').padEnd(16)}  ${OUTCOME_WORDS[device.outcome]}` +
        (device.detail ? `\n      ${device.detail}` : '') +
        (device.batteryMv ? `\n      battery ${(device.batteryMv / 1000).toFixed(2)} V` : '') +
        (device.firmware ? `\n      firmware ${device.firmware}` : ''),
    );
  }
  lines.push('', manifest.summary.replace(/ Runs .*$/, ''));
  return lines.join('\n');
}

// --- Reading a record back ----------------------------------------------------------------------------
//
// The record is the only place the study's time zone survives: it is not on the tags and not in their
// logs. So when logs come back to be reviewed, the record that set them up is what puts every time on
// the study's own clock. Both tools write records, and the desktop tool may write only a few of the
// fields above, so reading one asks for the least that can place a log in time and ignores the rest.

/** What a deployment record says about where and when, as far as matching logs to it needs. */
export interface StudyRecord {
  /** What the person who set it up called it, if anything. */
  readonly name: string | null;
  /** IANA zone the study ran in. */
  readonly timezone: string;
  /** Unix seconds the deployment started, exactly as the tags were told. */
  readonly startTime: number;
  readonly endTime: number | null;
  /** Every tag the record lists. `eui` is 12 lowercase hex digits, most-significant byte first. */
  readonly devices: ReadonlyArray<{ readonly eui: string; readonly label: string | null }>;
  /** The exact experiment details written to every tag, as lowercase hex, when the record kept them. */
  readonly detailsHex: string | null;
  /** When the record was written, Unix seconds, when it says. Where several records match, the newest wins. */
  readonly writtenAt: number | null;
}

export type ParsedManifest =
  | { readonly ok: true; readonly record: StudyRecord }
  | { readonly ok: false; readonly reason: string };

/** An address in any of the ways it gets written ("C0:98:E5:42:01:05", "c098e5420105"), or null. */
export function normalizeEuiText(value: unknown): string | null {
  if (typeof value !== 'string') return null;
  const digits = value.toLowerCase().replace(/[^0-9a-f]/g, '');
  return digits.length === 12 && /^[0-9a-f :.-]+$/i.test(value.trim()) ? digits : null;
}

const finite = (value: unknown): number | null => {
  const number = typeof value === 'string' && value.trim() !== '' ? Number(value) : value;
  return typeof number === 'number' && Number.isFinite(number) ? number : null;
};

/**
 * Read a deployment record leniently.
 *
 * Required: the record's `kind`, the deployment's time zone and start time, and an address for each tag.
 * Everything else is optional, and fields this version does not know are ignored, so a record written
 * by either tool, or by a later version of either, still places its logs in time.
 */
export function parseManifest(text: string): ParsedManifest {
  let value: unknown;
  try {
    value = JSON.parse(text.replace(/^﻿/, ''));
  } catch {
    return { ok: false, reason: 'This file is not a deployment record. A record is the .json file saved when the tags were set up.' };
  }
  const root = (value && typeof value === 'object' ? value : {}) as Record<string, unknown>;
  if (root.kind !== MANIFEST_KIND) {
    return { ok: false, reason: 'This file is not a deployment record. A record is the .json file saved when the tags were set up.' };
  }
  const deployment = (root.deployment && typeof root.deployment === 'object' ? root.deployment : {}) as Record<string, unknown>;
  const timezone = typeof deployment.timezone === 'string' ? deployment.timezone.trim() : '';
  if (!timezone) return { ok: false, reason: 'This deployment record does not say which time zone the study was in.' };
  if (!isValidTimezone(timezone)) {
    return { ok: false, reason: `This deployment record names the time zone "${timezone}", which this browser does not recognize.` };
  }
  const startTime = finite(deployment.startTime);
  if (startTime === null) return { ok: false, reason: 'This deployment record does not say when the deployment started.' };
  if (!Array.isArray(root.devices)) return { ok: false, reason: 'This deployment record does not list its tags.' };

  const devices: Array<{ eui: string; label: string | null }> = [];
  for (const entry of root.devices) {
    const device = (entry && typeof entry === 'object' ? entry : {}) as Record<string, unknown>;
    const eui = normalizeEuiText(device.eui);
    if (!eui) continue;
    const label = typeof device.label === 'string' && device.label.trim() ? device.label.trim() : null;
    devices.push({ eui, label });
  }
  if (!devices.length) return { ok: false, reason: 'This deployment record does not list the address of any tag.' };

  const rawHex = typeof root.experimentDetailsHex === 'string' ? root.experimentDetailsHex.replace(/\s+/g, '').toLowerCase() : '';
  const name = typeof root.name === 'string' && root.name.trim() ? root.name.trim() : null;
  const writtenAt = typeof root.writtenAt === 'string' ? Date.parse(root.writtenAt) / 1000 : Number.NaN;
  return {
    ok: true,
    record: {
      name,
      timezone,
      startTime,
      endTime: finite(deployment.endTime),
      devices,
      detailsHex: /^(?:[0-9a-f]{2})+$/.test(rawHex) ? rawHex : null,
      writtenAt: Number.isFinite(writtenAt) ? writtenAt : null,
    },
  };
}

/** What a log says about which deployment it belongs to. */
export interface LogIdentity {
  /** The experiment details carried in the log's header, byte for byte. */
  readonly details: Uint8Array | null;
  /** Unix seconds the deployment started, from those details. */
  readonly experimentStartTime: number | null;
  /** The tag that wrote the log: its full address (least-significant byte first) or just its last byte. */
  readonly tag: Uint8Array | number | null;
}

export interface ManifestMatch {
  readonly record: StudyRecord;
  /** Its position in the list given, so a caller can name the file it came from. */
  readonly index: number;
  /** `details`: the log carries exactly the bytes the record says were written. */
  readonly by: 'details' | 'start-and-tag';
}

/** Of the records at these positions, the newest; one that does not say when it was written counts as oldest. */
function newest(records: readonly StudyRecord[], indices: readonly number[]): number | null {
  let best: number | null = null;
  for (const index of indices) {
    const written = records[index]!.writtenAt ?? Number.NEGATIVE_INFINITY;
    if (best === null || written >= (records[best]!.writtenAt ?? Number.NEGATIVE_INFINITY)) best = index;
  }
  return best;
}

/**
 * The record a log belongs to, or null.
 *
 * A log matches a record when its experiment details are exactly the bytes the record says were
 * written. A record without those bytes, or a tag whose details were changed afterwards, still
 * matches when the deployment started at the same second and the tag that wrote the log is one the
 * record lists. Where several records match the same way — the same tags set up twice, say — the newest
 * wins. The desktop tool applies the same rule.
 */
export function matchManifest(log: LogIdentity, records: readonly StudyRecord[]): ManifestMatch | null {
  const detailsHex = log.details && log.details.length ? hex(log.details) : null;
  if (detailsHex) {
    const index = newest(records, records.flatMap((record, i) => (record.detailsHex === detailsHex ? [i] : [])));
    if (index !== null) return { record: records[index]!, index, by: 'details' };
  }
  if (log.experimentStartTime === null || log.tag === null) return null;
  const isTag = typeof log.tag === 'number'
    ? (eui: string) => Number.parseInt(eui.slice(10), 16) === log.tag
    : (() => {
        const wanted = hex(Uint8Array.from(log.tag).reverse());
        return (eui: string) => eui === wanted;
      })();
  const index = newest(records, records.flatMap((record, i) =>
    (record.startTime === log.experimentStartTime && record.devices.some((device) => isTag(device.eui)) ? [i] : [])));
  return index !== null ? { record: records[index]!, index, by: 'start-and-tag' } : null;
}
