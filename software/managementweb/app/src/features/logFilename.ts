// Naming a log that came off a tag over the air. Pure: no browser APIs, no I/O.
//
// A downloaded log has no filename of its own — the tag serves a byte stream, not a file — so the
// app has to invent one. It invents the SAME one `tottag.py` would have written, because the two
// dashboards are routinely pointed at the same folder and a researcher comparing them should find
// one file per device per deployment, not two under different names.
//
// The Python convention is `{label}_{experiment start}.ttg`, where the label is the deployment's own
// label for the device and the timestamp is when the EXPERIMENT started, not when the download
// happened. Both come out of the log itself, which is what makes the name reproducible: downloading
// the same tag twice overwrites one file rather than accumulating a second one under a new clock
// reading. The tag's identity contributes only the one thing the log cannot say on its own — which
// of the devices listed in the details block is the one that wrote it.

import { EUI_LEN, EUI_NAME_MAX_LEN, MAX_NUM_RANGING_DEVICES, parseExperimentDetails } from '@tottag/schema';

/** Shortest details block `parseExperimentDetails` can read without running off the end. */
const DETAILS_MIN_BYTES = 18 + MAX_NUM_RANGING_DEVICES * (EUI_LEN + EUI_NAME_MAX_LEN) + 1;

export interface LogNameInputs {
  /** The details block from the parse report, when the stream carried one. */
  readonly details: Uint8Array | null;
  /** Experiment start in whole Unix seconds, as the parser recovered it. */
  readonly experimentStartTime: number;
  /** Low EUI byte of the device this came from, where the transport knew it. */
  readonly deviceUid: number | undefined;
  /** Used whole when nothing better can be derived. Already ends in `.ttg`. */
  readonly fallback: string;
}

/**
 * A filename is a path fragment the moment it reaches a save dialog, and the label it is built from
 * was typed by whoever configured the deployment and stored on the device. Keep it to the character
 * set the existing labels use and nothing can be smuggled through it.
 */
function sanitise(label: string): string {
  return label.replace(/[^A-Za-z0-9._-]/g, '').replace(/^\.+/, '').slice(0, EUI_NAME_MAX_LEN);
}

/** This device's label, as the deployment recorded it. Null when the log cannot say. */
export function labelForDevice(details: Uint8Array | null, deviceUid: number | undefined): string | null {
  if (!details || details.byteLength < DETAILS_MIN_BYTES || deviceUid === undefined) return null;
  let parsed;
  try {
    parsed = parseExperimentDetails(details);
  } catch {
    return null;
  }
  const index = parsed.uids.findIndex((uid) => uid[0] === deviceUid);
  if (index < 0) return null;
  // `tottag.py` falls back to the bare UID for an unlabelled device, and so does this.
  return sanitise(parsed.labels[index] || String(deviceUid)) || null;
}

export function canonicalLogName(inputs: LogNameInputs): string {
  const { experimentStartTime } = inputs;
  if (!Number.isInteger(experimentStartTime) || experimentStartTime <= 0) return inputs.fallback;
  const label = labelForDevice(inputs.details, inputs.deviceUid);
  return label ? `${label}_${experimentStartTime}.ttg` : inputs.fallback;
}
