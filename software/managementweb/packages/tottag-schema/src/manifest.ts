// The record of what was actually deployed.
//
// The question this answers is asked months later, usually when something in the data looks wrong:
// "what exactly did we put on these tags?" Until now the only answer was the tags themselves, which
// by then have been reconfigured, and a researcher's memory.
//
// Two properties matter more than completeness:
//
//   1. It records what the DEVICE CONFIRMED, not what the app sent. Every field here is read back
//      off the tag after writing and decoded by the same parser that reads a log, so a device that
//      silently accepted something different is visible rather than assumed away.
//   2. It is readable without this app. JSON with spelled-out fields and a human summary, not a
//      binary blob — the failure mode being guarded against is a format nobody can open in 2030.

import { formatEui, type DeploymentConfig } from './deployment.ts';
import type { ExperimentDetails } from './log.ts';

export const MANIFEST_KIND = 'tottag.deployment-manifest';
export const MANIFEST_VERSION = 1;

export type WriteOutcome = 'confirmed' | 'mismatch' | 'unconfirmed' | 'failed';

export interface DeviceManifestEntry {
  readonly eui: string;
  readonly shortUid: string;
  readonly label: string;
  readonly outcome: WriteOutcome;
  /** What went wrong, or how the read-back differed. Null when confirmed. */
  readonly detail: string | null;
  /** Firmware and hardware strings read from the device, when it reported them. */
  readonly firmware?: string | undefined;
  readonly hardware?: string | undefined;
}

export interface DeploymentManifest {
  readonly kind: typeof MANIFEST_KIND;
  readonly manifestVersion: number;
  readonly writtenAt: string;
  readonly deployment: {
    readonly startTime: number;
    readonly endTime: number;
    readonly startTimeIso: string;
    readonly endTimeIso: string;
    readonly timezone: string;
    readonly durationDays: number;
    readonly useDailyTimes: boolean;
    readonly dailyStartUtc: string | null;
    readonly dailyEndUtc: string | null;
  };
  readonly devices: readonly DeviceManifestEntry[];
  /** Hex of the exact 239 bytes written, so the manifest can be replayed or compared byte for byte. */
  readonly experimentDetailsHex: string;
  readonly summary: string;
}

const iso = (unixSeconds: number): string => new Date(unixSeconds * 1000).toISOString();

const timeOfDay = (seconds: number): string => {
  const h = Math.floor(seconds / 3600);
  const m = Math.floor((seconds % 3600) / 60);
  const s = seconds % 60;
  return `${String(h).padStart(2, '0')}:${String(m).padStart(2, '0')}:${String(s).padStart(2, '0')}`;
};

const hex = (bytes: Uint8Array): string =>
  Array.from(bytes).map((b) => b.toString(16).padStart(2, '0')).join('');

export interface DeviceWriteResult {
  readonly eui: Uint8Array;
  readonly label: string;
  readonly outcome: WriteOutcome;
  readonly detail?: string | null;
  readonly firmware?: string | undefined;
  readonly hardware?: string | undefined;
}

export function buildManifest(
  config: DeploymentConfig,
  detailsBytes: Uint8Array,
  results: readonly DeviceWriteResult[],
  writtenAt: Date,
): DeploymentManifest {
  const confirmed = results.filter((r) => r.outcome === 'confirmed').length;
  const failed = results.filter((r) => r.outcome === 'failed' || r.outcome === 'mismatch').length;
  const unconfirmed = results.filter((r) => r.outcome === 'unconfirmed').length;

  const parts = [`${confirmed} of ${results.length} TotTag(s) confirmed the deployment.`];
  if (failed) parts.push(`${failed} did not take it.`);
  if (unconfirmed) parts.push(`${unconfirmed} were written but could not be read back to check.`);
  parts.push(
    `Runs ${iso(config.startTime)} to ${iso(config.endTime)} UTC ` +
      `(${((config.endTime - config.startTime) / 86400).toFixed(2)} days), entered in ${config.timezone}.`,
  );

  return {
    kind: MANIFEST_KIND,
    manifestVersion: MANIFEST_VERSION,
    writtenAt: writtenAt.toISOString(),
    deployment: {
      startTime: config.startTime,
      endTime: config.endTime,
      startTimeIso: iso(config.startTime),
      endTimeIso: iso(config.endTime),
      timezone: config.timezone,
      durationDays: Number(((config.endTime - config.startTime) / 86400).toFixed(4)),
      useDailyTimes: config.useDailyTimes,
      // Spelled out in UTC because that is what the device compares against. Rendering it in the
      // entry timezone would be friendlier and would also be the thing that makes an off-by-an-hour
      // argument unresolvable a year later.
      dailyStartUtc: config.useDailyTimes ? timeOfDay(config.dailyStartTime) : null,
      dailyEndUtc: config.useDailyTimes ? timeOfDay(config.dailyEndTime) : null,
    },
    devices: results.map((result) => ({
      eui: formatEui(result.eui),
      shortUid: `0x${(result.eui[0] ?? 0).toString(16).padStart(2, '0').toUpperCase()}`,
      label: result.label,
      outcome: result.outcome,
      detail: result.detail ?? null,
      firmware: result.firmware,
      hardware: result.hardware,
    })),
    experimentDetailsHex: hex(detailsBytes),
    summary: parts.join(' '),
  };
}

/**
 * Compare what a device reports back against what was sent.
 *
 * Only the fields the device actually stores are compared. The timezone is not among them — it is
 * provenance kept in the manifest for people, and expecting it back would report a mismatch on every
 * device.
 */
export function diffReadback(config: DeploymentConfig, readback: ExperimentDetails): string[] {
  const differences: string[] = [];
  const check = (name: string, expected: number | boolean, actual: number | boolean) => {
    if (expected !== actual) differences.push(`${name}: sent ${String(expected)}, device has ${String(actual)}`);
  };
  check('start time', config.startTime, readback.experimentStartTime);
  check('end time', config.endTime, readback.experimentEndTime);
  check('daily times enabled', config.useDailyTimes, readback.useDailyTimes);
  if (config.useDailyTimes) {
    check('daily start', ((config.dailyStartTime % 86400) + 86400) % 86400, readback.dailyStartTime);
    check('daily end', ((config.dailyEndTime % 86400) + 86400) % 86400, readback.dailyEndTime);
  }
  check('device count', config.devices.length, readback.numDevices);

  for (let index = 0; index < Math.min(config.devices.length, readback.numDevices); index += 1) {
    const sent = config.devices[index]!;
    const got = readback.uids[index];
    if (!got || formatEui(sent.eui) !== formatEui(got)) {
      differences.push(`device ${index + 1} address: sent ${formatEui(sent.eui)}, device has ${got ? formatEui(got) : 'nothing'}`);
    }
    // The device stores a fixed-width field, so compare against what would survive the trip.
    const expectedLabel = new TextDecoder()
      .decode(new TextEncoder().encode(sent.label).subarray(0, 16))
      .replace(/\0+$/, '');
    if (expectedLabel !== (readback.labels[index] ?? '')) {
      differences.push(`device ${index + 1} label: sent "${expectedLabel}", device has "${readback.labels[index] ?? ''}"`);
    }
  }
  if (readback.isTerminated) differences.push('the device reports the deployment as already terminated');
  return differences;
}

/** A plain-text companion, for pasting into a lab notebook. */
export function manifestToText(manifest: DeploymentManifest): string {
  const lines = [
    'TotTag deployment manifest',
    '='.repeat(60),
    `Written:      ${manifest.writtenAt}`,
    `Starts:       ${manifest.deployment.startTimeIso}  (${manifest.deployment.startTime})`,
    `Ends:         ${manifest.deployment.endTimeIso}  (${manifest.deployment.endTime})`,
    `Duration:     ${manifest.deployment.durationDays} days`,
    `Entered in:   ${manifest.deployment.timezone}`,
    manifest.deployment.useDailyTimes
      ? `Daily window: ${manifest.deployment.dailyStartUtc} to ${manifest.deployment.dailyEndUtc} UTC`
      : 'Daily window: none — records for the whole span',
    '',
    'TotTags',
    '-'.repeat(60),
  ];
  for (const device of manifest.devices) {
    lines.push(
      `  ${device.eui}  short ${device.shortUid}  ${device.label || '<unlabelled>'}  [${device.outcome}]` +
        (device.detail ? `\n      ${device.detail}` : '') +
        (device.firmware ? `\n      firmware ${device.firmware}` : ''),
    );
  }
  lines.push('', manifest.summary, '', `experiment_details_t: ${manifest.experimentDetailsHex}`);
  return lines.join('\n');
}
