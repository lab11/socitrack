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

const hexByte = (value: number): string => value.toString(16).padStart(2, '0').toUpperCase();

/**
 * `EE:FF`: the last two parts of the address, which is how people tell tags apart.
 *
 * Two parts rather than one, because the second-lowest byte is not always the same across a lab's
 * tags, and one part alone would make two different tags look identical on screen.
 */
export function shortAddress(eui: Uint8Array | readonly number[]): string {
  return `${hexByte(eui[1] ?? 0)}:${hexByte(eui[0] ?? 0)}`;
}

/** `TotTag EE:FF`, the name a specific tag goes by everywhere a person reads it. */
export function tagName(eui: Uint8Array | readonly number[]): string {
  return `TotTag ${shortAddress(eui)}`;
}

/** A label as it goes into a log's file name: only what a file name cannot hold is dropped, as the desktop tool's file_label() does. */
export function fileLabel(label: string): string {
  return label.replace(/[\\/:*?"<>|\u0000-\u001F\u007F]/g, '').replace(/^[ .]+|[ .]+$/g, '').slice(0, EUI_NAME_MAX_LEN);
}

/** The one-byte ID tags know each other by on the air and in the log, as two hex digits. */
export function shortUidHex(eui: Uint8Array | readonly number[]): string {
  return hexByte(eui[0] ?? 0);
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

export interface ValidationOptions {
  /** Unix seconds. When given, a deployment that has already ended is an error and one already under way is noted. */
  readonly now?: number;
}

/**
 * Everything wrong with a configuration, worst first.
 *
 * Errors block writing to a tag; warnings do not. The split is by consequence, not by confidence:
 * a short-UID collision is an error because the resulting data cannot be repaired afterwards, while
 * an empty label is a warning because it only makes the analysis harder to read.
 *
 * Messages are written for the person running the study. A tag is named the way it is everywhere
 * else on screen — TotTag and the last two parts of its address — and never by its full address.
 */
export function validateDeployment(config: DeploymentConfig, options: ValidationOptions = {}): Problem[] {
  const problems: Problem[] = [];
  const { devices } = config;
  const nameOf = (index: number): string => devices[index]!.label.trim() || tagName(devices[index]!.eui);

  // --- devices ---
  if (devices.length === 0) {
    problems.push({ severity: 'error', code: 'no-devices', devices: [], message: 'Add at least one tag.' });
  }
  if (devices.length > MAX_NUM_RANGING_DEVICES) {
    problems.push({
      severity: 'error',
      code: 'too-many-devices',
      devices: devices.map((_, index) => index).slice(MAX_NUM_RANGING_DEVICES),
      message: `A deployment can include at most ${MAX_NUM_RANGING_DEVICES} tags, and this one has ${devices.length}. Remove ${devices.length - MAX_NUM_RANGING_DEVICES === 1 ? 'this one' : 'these'}.`,
    });
  }

  // The one that cannot be fixed after the fact.
  for (const conflict of shortUidConflicts(devices)) {
    problems.push({
      severity: 'error',
      code: 'short-uid-conflict',
      devices: conflict.devices,
      // Shown against every device involved, so it has to stay short enough to repeat without
      // burying the row it belongs to. The full reasoning lives in shortUid()'s documentation.
      message:
        `${joinList(conflict.devices.map(nameOf))} ${conflict.devices.length === 2 ? 'both' : 'all'} end in ${hexByte(conflict.shortUid)}. ` +
        'Tags tell each other apart by that last part alone, so their data could not be separated ' +
        'afterwards. Swap one for a tag ending differently.',
    });
  }

  // A low byte of 0 is the protocol's "no device": schedule_phase_add_device() takes 0 for an empty slot,
  // and a device_eui of 0 means "not heard", so a tag ending in 00 can never join a network.
  devices.forEach((device, index) => {
    if (device.eui.length === EUI_LEN && shortUid(device.eui) === 0) {
      problems.push({
        severity: 'error',
        code: 'short-uid-zero',
        devices: [index],
        message: `${tagName(device.eui)} ends in 00, which tags use to mean "no tag", so it could never join the network. ` +
          'Swap it for a tag ending differently.',
      });
    }
  });

  devices.forEach((device, index) => {
    if (device.eui.length !== EUI_LEN) {
      problems.push({
        severity: 'error',
        code: 'bad-eui',
        devices: [index],
        message: `Tag ${index + 1} has a ${device.eui.length}-part address where a TotTag has ${EUI_LEN}. Remove it and add it again.`,
      });
    }
    if (labelBytes(device.label) > EUI_NAME_MAX_LEN) {
      problems.push({
        severity: 'error',
        code: 'label-too-long',
        devices: [index],
        message: `"${device.label}" is too long. A label can be up to ${EUI_NAME_MAX_LEN} characters, and letters with accents count as two.`,
      });
    }
    if (device.label.trim() === '') {
      problems.push({
        severity: 'warning',
        code: 'label-missing',
        devices: [index],
        message: `${tagName(device.eui)} has no label, so its data will only be identified by the tag's ID.`,
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
        message: `${indices.length} tags are labeled "${label}". Each tag needs its own label to tell their data apart.`,
      });
    }
  }

  // --- window ---
  const startKnown = Number.isFinite(config.startTime);
  const endKnown = Number.isFinite(config.endTime);
  if (!startKnown) {
    problems.push({ severity: 'error', code: 'missing-start', devices: [], message: 'Enter a complete start date and time.' });
  }
  if (!endKnown) {
    problems.push({ severity: 'error', code: 'missing-end', devices: [], message: 'Enter a complete end date and time.' });
  }
  const duration = config.endTime - config.startTime;
  if (startKnown && endKnown && duration <= 0) {
    problems.push({
      severity: 'error',
      code: 'end-before-start',
      devices: [],
      message: 'The deployment ends before it starts.',
    });
  } else if (startKnown && endKnown && duration > MAX_DEPLOYMENT_SECONDS) {
    problems.push({
      severity: 'error',
      code: 'too-long',
      devices: [],
      message:
        `A deployment can run for at most ${MAX_DEPLOYMENT_DAYS} days, and this one is ` +
        `${(duration / 86400).toFixed(1)}. Tags cannot keep their timestamps straight for longer than that.`,
    });
  }
  if (options.now !== undefined && endKnown && config.endTime <= options.now) {
    problems.push({
      severity: 'error',
      code: 'already-over',
      devices: [],
      message: 'The end time has already passed, so the tags would never record. Choose dates in the future.',
    });
  } else if (options.now !== undefined && startKnown && endKnown && duration > 0 && config.startTime <= options.now) {
    problems.push({
      severity: 'warning',
      code: 'already-started',
      devices: [],
      message: 'The start time has already passed, so each tag starts recording as soon as it comes off its charger.',
    });
  }

  if (config.useDailyTimes) {
    for (const [name, value] of [['start', config.dailyStartTime], ['end', config.dailyEndTime]] as const) {
      if (!Number.isInteger(value) || value < 0 || value >= 86400) {
        problems.push({
          severity: 'error',
          code: 'bad-daily-time',
          devices: [],
          message: `Enter a complete daily ${name} time.`,
        });
      }
    }
    if (Number.isInteger(config.dailyStartTime) && config.dailyStartTime === config.dailyEndTime) {
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
