// The boundary between "somewhere a log comes from" and everything that reasons about one.
//
// Nothing behind this interface knows about files, Bluetooth, or serial ports. That is the whole
// point: the schema package is already pure, and this keeps the app's own logic pure too, so that
// adding the Web Serial and Web Bluetooth transports later is a new adapter rather than a rewrite
// of whatever consumed the file one.
//
// A source yields bytes and a name. It does not parse, validate, or interpret — those belong to
// @tottag/schema, which takes a Uint8Array and has no idea where it came from.

/** One log's worth of bytes, with enough provenance to tell the user which one it is. */
export interface LoadedLog {
  /** Display name. For a file this is the filename; for a tag it will be the label or EUI. */
  readonly name: string;
  /** Raw stream bytes, exactly as produced by the device. */
  readonly bytes: Uint8Array;
  /** Where it came from, for the UI to label and for provenance in an export. */
  readonly origin: 'file' | 'serial' | 'bluetooth';
  /** Last-modified time where the source knows one. Absent for live transfers. */
  readonly modifiedAt?: Date | undefined;
  /**
   * Low EUI byte of the device this came from, where the transport knew it.
   *
   * The only thing a live transfer knows that the bytes do not: the details block lists every device
   * in the deployment, and nothing in it says which one is holding the log. Absent for a file, whose
   * name already settled the question.
   */
  readonly deviceUid?: number | undefined;
}

/** A failure to load one log. Reported per-log so that one bad file does not lose a whole batch. */
export interface LoadFailure {
  readonly name: string;
  readonly reason: string;
}

/**
 * A deployment record (the .json saved when the tags were set up), as text.
 *
 * It travels with logs because it is opened with them: dropped in the same gesture, or found in the
 * same folder. It is the only place the study's time zone is written down.
 */
export interface LoadedRecordFile {
  readonly name: string;
  readonly text: string;
}

export interface LoadResult {
  readonly loaded: readonly LoadedLog[];
  readonly failed: readonly LoadFailure[];
  /** Deployment records opened alongside the logs. */
  readonly records?: readonly LoadedRecordFile[] | undefined;
  /**
   * Read from a whole folder rather than chosen file by file. A folder of downloads can hold .json files
   * that are not deployment records, and those are passed over quietly rather than reported.
   */
  readonly fromFolder?: boolean | undefined;
}

/**
 * Something that can produce logs.
 *
 * `isAvailable` exists because two of the three implementations will not exist in Safari or
 * Firefox. The UI needs to say so plainly up front rather than presenting a control that fails
 * when pressed.
 */
export interface LogSource {
  readonly id: 'file' | 'serial' | 'bluetooth';
  readonly label: string;
  isAvailable(): boolean;
  /** Why it is unavailable, phrased for a non-technical reader. Null when it is available. */
  unavailableReason(): string | null;
  load(): Promise<LoadResult>;
}
