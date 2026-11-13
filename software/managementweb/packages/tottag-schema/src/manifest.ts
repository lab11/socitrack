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
  clockChanges, formatDateTime, formatDay, formatSpan, formatTimeOfDay, localDailyWindow, timeOfDayInput,
  unixToWallClock,
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
  if (readback.isTerminated) differences.push('It reports the deployment as already cancelled.');
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
