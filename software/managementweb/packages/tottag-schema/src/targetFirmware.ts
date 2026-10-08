// What the firmware we are WRITING FOR can do, as distinct from what wrote a log we are reading.
//
// These are two different questions with two different safe answers, and conflating them is the
// mistake this module exists to prevent — A3EM keeps `cardFirmwareProfile` and
// `targetFirmwareProfile` apart for the same reason.
//
//   READING  (`detectFirmwareEra`)  — infer from evidence in the file, and default to ASSUMING LESS.
//                                     Reading an old log in a current tool is ordinary, and claiming
//                                     a capability the writer did not have produces confident
//                                     falsehoods about somebody's deployment.
//
//   WRITING  (this module)          — default to CURRENT firmware. Every tag being configured today
//                                     is about to run current firmware, and defaulting to the older
//                                     behaviour only produces warnings about situations that cannot
//                                     arise, which is how people learn to ignore warnings.
//
// A revision inference is tempting to use for both, and must not be: the tag in your hand right now
// has nothing to do with the tag that wrote the file you happen to have open.

/** What the firmware being written to is known to support. */
export interface TargetFirmware {
  /** Accepts BLE_MAINTENANCE_RETRANSMIT_PAGES, so a damaged download can be repaired. */
  readonly supportsRetransmission: boolean;
  /** Writes framed records, so a reader can step over a record type it does not know. */
  readonly writesFramedRecords: boolean;
  /** De-bounces charger edges and reports the suppressed count. */
  readonly debouncesChargerEdges: boolean;
  /** Advertises `TotTag-XX`, so a host chooser can tell tags apart. */
  readonly advertisesShortUidInName: boolean;
  /** Human-readable name for the UI and for a manifest. */
  readonly label: string;
}

/**
 * The firmware in this repository. The default for everything that writes to a tag.
 *
 * Deliberately not detected. Detection belongs to reading; a tag about to be configured is going to
 * run whatever is flashed on it, and the honest way to handle an older one is for the WRITE to fail
 * and say so, not for the host to quietly downgrade what it asks for.
 */
export const CURRENT_TARGET: TargetFirmware = {
  supportsRetransmission: true,
  writesFramedRecords: true,
  debouncesChargerEdges: true,
  advertisesShortUidInName: true,
  label: 'current firmware',
};

/**
 * Firmware predating the diagnostics record and record framing.
 *
 * Provided so that a caller who KNOWS it is talking to an old tag can say so explicitly. Never
 * selected automatically — see the note at the top of this file.
 */
export const LEGACY_TARGET: TargetFirmware = {
  supportsRetransmission: false,
  writesFramedRecords: false,
  debouncesChargerEdges: false,
  advertisesShortUidInName: false,
  label: 'firmware older than the diagnostics record',
};

/**
 * Capabilities to assume when writing.
 *
 * Takes no log and no inference on purpose: passing a `FirmwareEra` here would be exactly the
 * conflation the module exists to prevent, and the type system will not let you.
 */
export function targetFirmware(known?: TargetFirmware): TargetFirmware {
  return known ?? CURRENT_TARGET;
}
