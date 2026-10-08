// Configuring a deployment: the settings, what makes them valid, and the bytes that go to a device.
//
// Pure. Takes plain values, returns plain values and byte arrays. Everything that talks to a tag
// lives in the app's transport adapters and calls into here for the payload.

import {
  BLE_MAINTENANCE_NEW_EXPERIMENT, EUI_LEN, EUI_NAME_MAX_LEN, EXPERIMENT_DETAILS_LAYOUT,
  MAX_DEPLOYMENT_DAYS, MAX_DEPLOYMENT_SECONDS, MAX_NUM_RANGING_DEVICES,
} from './constants.ts';

/** One tag in a deployment. */
export interface DeploymentDevice {
  /** Full 6-byte EUI, least-significant byte first — the order the firmware stores and compares. */
  readonly eui: Uint8Array;
  /** Free text, at most EUI_NAME_MAX_LEN bytes of UTF-8. */
  readonly label: string;
}

export interface DeploymentConfig {
  /** Unix seconds, UTC. */
  readonly startTime: number;
  readonly endTime: number;
  /** IANA zone the window was entered in. Provenance for people; never sent to a device. */
  readonly timezone: string;
  readonly useDailyTimes: boolean;
  /** Seconds past UTC midnight. The firmware compares these against its own UTC time-of-day. */
  readonly dailyStartTime: number;
  readonly dailyEndTime: number;
  readonly devices: readonly DeploymentDevice[];
}

export type ProblemSeverity = 'error' | 'warning';

export interface Problem {
  readonly severity: ProblemSeverity;
  readonly code: string;
  /** Which device indices this concerns, for highlighting rows. */
  readonly devices: readonly number[];
  readonly message: string;
}

// --- The short UID ------------------------------------------------------------------------------

/**
 * The byte every device is known by on the air and in the log.
 *
 * The ranging protocol and the log format both identify a peer by ONE byte — the low byte of its
 * EUI. `compute_ranges()` writes `state[dev].device_eui` into a RANGES record as a single byte, the
 * BLE scan list stores `discovered_devices[i][0]`, and `computation_phase_configure_filters()` keys
 * its per-peer filters on `details->uids[i][0]`. Nothing anywhere carries more than that byte at
 * runtime.
 *
 * So two tags in one deployment whose EUIs share a low byte are indistinguishable to each other and
 * in the data. See `shortUidConflicts()`.
 */
export function shortUid(eui: Uint8Array): number {
  return eui[0] ?? 0;
}

/** `AA:BB:CC:DD:EE:FF`, most-significant byte first, the way a BLE address is conventionally shown. */
export function formatEui(eui: Uint8Array): string {
  return Array.from(eui).reverse().map((b) => b.toString(16).padStart(2, '0').toUpperCase()).join(':');
}

/**
 * Groups of device indices that share a low EUI byte.
 *
 * Returns one entry per colliding value, each listing every device involved — not pairs, because
 * three tags can collide and reporting that as three pairs makes it look like three problems.
 */
export function shortUidConflicts(devices: readonly DeploymentDevice[]): Array<{ shortUid: number; devices: number[] }> {
  const byShortUid = new Map<number, number[]>();
  devices.forEach((device, index) => {
    const key = shortUid(device.eui);
    const existing = byShortUid.get(key);
    if (existing) existing.push(index);
    else byShortUid.set(key, [index]);
  });
  return [...byShortUid]
    .filter(([, indices]) => indices.length > 1)
    .map(([value, indices]) => ({ shortUid: value, devices: indices }))
    .sort((a, b) => a.shortUid - b.shortUid);
}

// --- Validation ---------------------------------------------------------------------------------

const labelBytes = (label: string): number => new TextEncoder().encode(label).length;

/** "a and b", "a, b and c". Intl.ListFormat is not used: the package stays dependency- and locale-free. */
const joinList = (items: readonly string[]): string =>
  items.length <= 1 ? (items[0] ?? '') : `${items.slice(0, -1).join(', ')} and ${items[items.length - 1]}`;

/**
 * Everything wrong with a configuration, worst first.
 *
 * Errors block writing to a device; warnings do not. The split is by consequence, not by confidence:
 * a short-UID collision is an error because the resulting data cannot be repaired afterwards, while
 * an empty label is a warning because it only makes the analysis harder to read.
 */
export function validateDeployment(config: DeploymentConfig): Problem[] {
  const problems: Problem[] = [];
  const { devices } = config;

  // --- devices ---
  if (devices.length === 0) {
    problems.push({ severity: 'error', code: 'no-devices', devices: [], message: 'Add at least one TotTag.' });
  }
  if (devices.length > MAX_NUM_RANGING_DEVICES) {
    problems.push({
      severity: 'error',
      code: 'too-many-devices',
      devices: devices.map((_, index) => index).slice(MAX_NUM_RANGING_DEVICES),
      message: `A deployment holds at most ${MAX_NUM_RANGING_DEVICES} TotTags; this one has ${devices.length}.`,
    });
  }

  // The one that cannot be fixed after the fact.
  for (const conflict of shortUidConflicts(devices)) {
    const named = conflict.devices.map((index) => devices[index]!.label || formatEui(devices[index]!.eui));
    problems.push({
      severity: 'error',
      code: 'short-uid-conflict',
      devices: conflict.devices,
      // Shown against every device involved, so it has to stay short enough to repeat without
      // burying the row it belongs to. The full reasoning lives in shortUid()'s documentation.
      message:
        `${joinList(named)} ${conflict.devices.length === 2 ? 'both' : 'all'} end in ${conflict.shortUid.toString(16).padStart(2, '0').toUpperCase()}. ` +
        'Tags identify each other by that last byte alone, so their data could not be told apart ' +
        'afterwards. Swap one for a tag ending differently.',
    });
  }

  devices.forEach((device, index) => {
    if (device.eui.length !== EUI_LEN) {
      problems.push({
        severity: 'error',
        code: 'bad-eui',
        devices: [index],
        message: `Device ${index + 1} has a ${device.eui.length}-byte address; it must be ${EUI_LEN} bytes.`,
      });
    }
    if (labelBytes(device.label) > EUI_NAME_MAX_LEN) {
      problems.push({
        severity: 'error',
        code: 'label-too-long',
        devices: [index],
        message: `"${device.label}" is longer than ${EUI_NAME_MAX_LEN} bytes; it would be cut off on the device.`,
      });
    }
    if (device.label.trim() === '') {
      problems.push({
        severity: 'warning',
        code: 'label-missing',
        devices: [index],
        message: `${formatEui(device.eui)} has no label, so it will appear in the data by its address.`,
      });
    }
  });

  const seenLabels = new Map<string, number[]>();
  devices.forEach((device, index) => {
    const key = device.label.trim();
    if (!key) return;
    const existing = seenLabels.get(key);
    if (existing) existing.push(index);
    else seenLabels.set(key, [index]);
  });
  for (const [label, indices] of seenLabels) {
    if (indices.length > 1) {
      problems.push({
        severity: 'error',
        code: 'duplicate-label',
        devices: indices,
        message: `${indices.length} TotTags are labelled "${label}". Labels have to be unique to tell them apart.`,
      });
    }
  }

  // --- window ---
  const duration = config.endTime - config.startTime;
  if (duration <= 0) {
    problems.push({
      severity: 'error',
      code: 'end-before-start',
      devices: [],
      message: 'The deployment ends before it starts.',
    });
  } else if (duration > MAX_DEPLOYMENT_SECONDS) {
    problems.push({
      severity: 'error',
      code: 'too-long',
      devices: [],
      message:
        `A deployment can run at most ${MAX_DEPLOYMENT_DAYS} days; this one is ` +
        `${(duration / 86400).toFixed(1)}. Timestamps are milliseconds since the start in a 32-bit counter, ` +
        'which stops being able to represent them after that.',
    });
  }

  if (config.useDailyTimes) {
    for (const [name, value] of [['start', config.dailyStartTime], ['end', config.dailyEndTime]] as const) {
      if (!Number.isInteger(value) || value < 0 || value >= 86400) {
        problems.push({
          severity: 'error',
          code: 'bad-daily-time',
          devices: [],
          message: `The daily ${name} time is not a valid time of day.`,
        });
      }
    }
    if (config.dailyStartTime === config.dailyEndTime) {
      problems.push({
        severity: 'error',
        code: 'empty-daily-window',
        devices: [],
        message: 'The daily start and end times are the same, so the tags would never record.',
      });
    }
  }

  return problems.sort((a, b) => (a.severity === b.severity ? 0 : a.severity === 'error' ? -1 : 1));
}

export const hasErrors = (problems: readonly Problem[]): boolean =>
  problems.some((problem) => problem.severity === 'error');

// --- Encoding -----------------------------------------------------------------------------------

/**
 * The 239-byte `experiment_details_t`, laid out from the extracted struct rather than from literals.
 *
 * Offsets come from EXPERIMENT_DETAILS_LAYOUT, so a field moving in the firmware moves here too
 * instead of silently writing the wrong bytes.
 */
export function encodeExperimentDetails(config: DeploymentConfig): Uint8Array {
  const layout = EXPERIMENT_DETAILS_LAYOUT;
  const offsetOf = (name: string): number => {
    const field = layout.fields.find((candidate) => candidate.name === name);
    if (!field) throw new Error(`experiment_details_t has no field '${name}'`);
    return field.offset;
  };

  const out = new Uint8Array(layout.size);
  const view = new DataView(out.buffer);
  view.setUint32(offsetOf('experiment_start_time'), config.startTime >>> 0, true);
  view.setUint32(offsetOf('experiment_end_time'), config.endTime >>> 0, true);
  // Seconds-of-day, normalised. The Python tool packed a raw local-minus-offset value here, which
  // goes negative anywhere east of UTC and made struct.pack raise outright; the firmware compares
  // against rtc_get_time_of_day(), which is always 0..86399, and handles a wrapped window itself.
  const daily = (value: number): number => (config.useDailyTimes ? ((value % 86400) + 86400) % 86400 : 0);
  view.setUint32(offsetOf('daily_start_time'), daily(config.dailyStartTime), true);
  view.setUint32(offsetOf('daily_end_time'), daily(config.dailyEndTime), true);
  out[offsetOf('use_daily_times')] = config.useDailyTimes ? 1 : 0;
  out[offsetOf('num_devices')] = config.devices.length;

  const uidsBase = offsetOf('uids');
  const labelsBase = offsetOf('uid_name_mappings');
  const encoder = new TextEncoder();
  config.devices.forEach((device, index) => {
    out.set(device.eui.subarray(0, EUI_LEN), uidsBase + index * EUI_LEN);
    // Truncated on a byte boundary, then NUL-padded — the firmware stores a fixed-width char array
    // and does not NUL-terminate, so anything past the width is simply not carried.
    const label = encoder.encode(device.label).subarray(0, EUI_NAME_MAX_LEN);
    out.set(label, labelsBase + index * EUI_NAME_MAX_LEN);
  });
  out[offsetOf('is_terminated')] = 0;
  return out;
}

/** The BLE/USB payload: the opcode byte followed by the struct. */
export function encodeNewExperimentCommand(config: DeploymentConfig): Uint8Array {
  const details = encodeExperimentDetails(config);
  const out = new Uint8Array(1 + details.length);
  out[0] = BLE_MAINTENANCE_NEW_EXPERIMENT;
  out.set(details, 1);
  return out;
}
