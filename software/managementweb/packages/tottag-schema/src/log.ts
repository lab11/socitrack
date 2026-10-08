// Reading an offload stream.
//
// A stream is either legacy v1 — a bare concatenation of records with no framing at all — or v2,
// which carries page structure through to the host so that a loss is localisable. Detection is
// unambiguous: v1 begins with a record type byte in 1..8 and v2 begins with the ASCII 'TTS1'.
//
// Two things distinguish this reader from the Python one it is modelled on, and both come from
// having had real deployment files to test against (Storage_Redesign.md §15.8):
//
//   1. **Records are returned in write order, not merged by timestamp.** The Python parser keys
//      `log_data[timestamp]`, which merges a range, a motion flag and a voltage at the same instant
//      into one row — convenient, and correct for those — but silently drops the older of two records
//      of the SAME type at the same millisecond. On the real corpus that costs 6 to 36 records per
//      file. Write order loses nothing, and `mergeByTimestamp()` is offered separately for consumers
//      that want the old shape and can state that they accept the loss.
//
//   2. **A page that fails is reported, not skipped.** Every hole, CRC failure, short decode and
//      backward time bound comes back in the report with the position and sequence number, because
//      the whole point of the v2 format is that a loss has a known size and location.
//
// The v1 path exists to read archived files and is deliberately a faithful port of the old
// byte-at-a-time resynchroniser, including its 500 ms grid heuristic. That heuristic must never be
// applied to v2: current firmware stores 10 ms resolution and it would reject every record.

import {
  BATTERY_EVENT, COMPRESSED_RANGE_DATUM_LENGTH, EUI_LEN, EUI_NAME_MAX_LEN, MAX_NUM_RANGING_DEVICES,
  MAX_VALID_RANGE_MM, NANDLOG_FRAMING_LENGTH_BYTES, NANDLOG_MAX_PAGE_SIZE_BYTES,
  NANDLOG_NO_TIMESTAMP, NANDLOG_TIMESTAMP_TOLERANCE_MS, RECORD_HEADER_BYTES,
  RESET_DIAGNOSTIC, RESET_DIAGNOSTIC_MASK, RESET_DIAGNOSTIC_SHIFT, RESET_STATUS_BITS,
  RESET_STATUS_MASK, STORAGE_DIAGNOSTIC_NUM_POOLS, STORAGE_TYPE, TIMESTAMP_QUANTUM_MS,
  V2_FORMAT_VERSION_FRAMED, V2_FORMAT_VERSION_UNFRAMED, V2_STREAM_MAGIC, WATCHDOG_TASK,
  WATCHDOG_TASK_NAMES, DIAGNOSTICS_LAYOUT, DIAGNOSTICS_STACK_NAMES,
  STORAGE_DIAGNOSTIC_FLAG_TEMPCO_AVAILABLE, STORAGE_DIAGNOSTIC_FLAG_TEMPCO_APPLIED,
  STORAGE_DIAGNOSTIC_FLAG_FIRMWARE_MODIFIED, STORAGE_DIAGNOSTIC_STACK_UNMONITORED,
  STORAGE_DIAGNOSTIC_FLAG_DIAGNOSTIC_BUILD, STORAGE_DIAGNOSTIC_FLAG_TEMPCO_DISABLED,
  RADIO_ABORT_LAYOUT, STORAGE_RADIO_ABORT_PHASE_RANGING, STORAGE_RADIO_ABORT_PHASE_STATUS,
  STORAGE_RADIO_ABORT_UNMEASURED, STORAGE_RADIO_ABORT_NO_EVENT_TIME, STORAGE_RADIO_ABORT_TRIGGER_TX_DONE,
  STORAGE_RADIO_ABORT_TRIGGER_RX_FRAME, STORAGE_RADIO_ABORT_TRIGGER_RX_TIMEOUT, STORAGE_RADIO_ABORT_TRIGGER_RX_ERROR, SCHEDULE_CATCH_LAYOUT, ROUND_START_LAYOUT, SESSION_END_LAYOUT, RADIO_TIMING_LAYOUT,
  STORAGE_SCHEDULE_CATCH_NONE, STORAGE_SCHEDULE_CATCH_UNMEASURED,
  STORAGE_ROUND_START_FLAG_SECOND_COPY_FAILED, STORAGE_ROUND_START_FLAG_COMPUTED, STORAGE_ROUND_START_FLAG_ABANDONED,
  STORAGE_ROUND_START_FLAG_JOIN_HEARD, STORAGE_ROUND_START_FLAG_JOIN_RELAYED,
  STORAGE_SESSION_END_STOPPED, STORAGE_SESSION_END_SEARCH_TIMEOUT, STORAGE_SESSION_END_COLLISION, STORAGE_SESSION_END_SILENT,
  SCHEDULE_ROLE, SCHEDULER_PHASE, PACKET_TYPE,
} from './constants.ts';
import { crc32 } from './crc32.ts';
import { guess } from './guesses.ts';

// --- Types ----------------------------------------------------------------------------------------

export type LogFormat = 1 | 2;

/** Milliseconds since the experiment start, on whichever clock the record's writer used. */
export type ExperimentMs = number;

export interface RecordBase {
  /** Record type byte, one of STORAGE_TYPE. */
  readonly type: number;
  /** Experiment-relative milliseconds, as stored. */
  readonly ms: ExperimentMs;
  /** Index of the page this record came from, counted along the stream. -1 for v1. */
  readonly page: number;
  /** Sequence number of that page within the epoch. -1 for v1. */
  readonly seq: number;
}

export type LogRecord =
  | (RecordBase & { readonly kind: 'voltage'; readonly millivolts: number })
  | (RecordBase & { readonly kind: 'charging'; readonly event: number; readonly label: string })
  | (RecordBase & { readonly kind: 'motion'; readonly inMotion: boolean })
  | (RecordBase & { readonly kind: 'ranges'; readonly ranges: ReadonlyMap<number, number> })
  | (RecordBase & { readonly kind: 'imu'; readonly accel: readonly [number, number, number] })
  | (RecordBase & { readonly kind: 'bleScan'; readonly peers: readonly number[] })
  | (RecordBase & {
      readonly kind: 'reset';
      readonly status: number;
      readonly causes: readonly string[];
      readonly diagnostic: number;
      readonly diagnosticLabel: string | null;
      readonly isWatchdog: boolean;
    })
  | (RecordBase & { readonly kind: 'diagnostics' } & DiagnosticsFields)
  | (RecordBase & {
      readonly kind: 'radioAbort';
      /** A ranging-phase abort abandons the round; a status-phase one only ends that exchange early. */
      readonly phase: 'ranging' | 'status' | number;
      readonly slot: number;
      readonly scheduleSize: number;
      /** How far past the arm deadline the attempt came, µs. */
      readonly lateUs: number;
      /** Time already spent in the radio interrupt, µs, or null if this build could not measure it. */
      readonly isrElapsedUs: number | null;
      /** Radio events that interrupt had serviced so far. */
      readonly isrEvents: number;
      /** Since the last 10 s temperature refresh, ms, or null if there has been none. */
      readonly sinceTemperatureMs: number | null;
      /** The radio event the interrupt was handling, or null if unknown. */
      readonly trigger: 'txDone' | 'rxFrame' | 'rxTimeout' | 'rxError' | number | null;
      /** When the interrupt started, µs after the round's reference, or null if unmeasured. */
      readonly isrEntryUs: number | null;
      /**
       * From the triggering frame's radio timestamp to the interrupt starting, µs, or null for an event with no
       * timestamp. The frame's air time after its timestamp is in it too, so only a change from the usual matters.
       */
      readonly eventToIsrUs: number | null;
      /**
       * How long the processor had slept when this interrupt woke it, µs, 0 if the interrupt arrived as it was going
       * to sleep, or null if it was awake.
       */
      readonly asleepUs: number | null;
      /** From the processor waking to this interrupt starting, µs, or null if it was awake. */
      readonly wakeToIsrUs: number | null;
    })
  | (RecordBase & {
      readonly kind: 'scheduleCatch';
      /** Sequence number of the first copy decoded: 0-1 sent by the master, 2-4 relayed; null if none arrived. */
      readonly firstCopy: number | null;
      /** Rounds that went by before a copy was decoded, or null if the schedule timestamps went backwards. */
      readonly roundsMissed: number | null;
      /** Receiver on this long before the expected round's first copy, µs; negative if after it. */
      readonly leadUs: number | null;
      /** Wake-up timer firing to the ranging task running, µs. */
      readonly timerToTaskUs: number | null;
      /** Radio wake-up, µs, or null if the radio had to be reset. */
      readonly wakeUs: number | null;
      /** Frames heard but not decodable before the schedule. */
      readonly rxErrors: number;
      /** Decodable frames before the schedule that were not one. */
      readonly otherFrames: number;
      /** Receiver on to the first undecodable frame, µs, or null if none. */
      readonly firstErrorUs: number | null;
      /** Carrier offset of the decoded copy, ppm, as the DW3000 reports it. */
      readonly carrierOffsetPpm: number | null;
      /** What the device had learned to add to its wake-up margin for this wake-up, µs. */
      readonly wakeCorrectionUs: number;
      /** Wake-up timer's compare match to its interrupt running, µs. */
      readonly timerLatencyUs: number | null;
    })
  | (RecordBase & {
      readonly kind: 'roundStart';
      /** Wake-up timer firing to the ranging task running, µs. */
      readonly timerToTaskUs: number;
      /** Radio wake-up, µs: 0 if it was already awake, null if it had to be reset. */
      readonly wakeUs: number | null;
      /** Wake-up timer firing to the first schedule copy being sent, µs. */
      readonly timerToTransmitUs: number;
      readonly secondCopyFailed: boolean;
      readonly computed: boolean;
      readonly abandoned: boolean;
      readonly joinHeard: boolean;
      readonly joinRelayed: boolean;
      readonly scheduleSize: number;
      readonly devicesRanged: number;
      /** Wake-up timer's compare match to its interrupt running, µs. */
      readonly timerLatencyUs: number | null;
    })
  | (RecordBase & {
      readonly kind: 'sessionEnd';
      readonly reason: 'unknown' | 'stopped' | 'search timeout' | 'collision' | 'heard nobody';
      /** The device's role when the run ended, by schedule_role_t name, or the raw value. */
      readonly role: string;
      readonly scheduleSize: number;
      /** The frame that ended a run as a collision: the phase it arrived in, its type, the byte after its header, and how far into the round. */
      readonly collision: { readonly phase: string; readonly packet: string; readonly source: number; readonly atUs: number } | null;
      readonly sessionMs: number;
      readonly roundsRanged: number;
      readonly schedulesHeard: number;
      readonly joinRequestsSent: number;
      readonly joinRequestsHeard: number;
      readonly listenErrors: number;
      readonly stalls: number;
    })
  | (RecordBase & {
      readonly kind: 'radioTiming';
      /** Delayed receives armed in time straight after a received frame, this minute. */
      readonly arms: number;
      /** Of those, ones whose interrupt had to wake the processor first, and ones that arrived as it was going to sleep. */
      readonly afterSleep: number;
      readonly duringSleepEntry: number;
      /** Least time to spare at any of them, µs, or null if none. */
      readonly slackMinUs: number | null;
      readonly slackUnder25Us: number;
      /** Fastest and slowest from the frame's radio timestamp to the interrupt starting, µs. */
      readonly eventToIsrMinUs: number | null;
      readonly eventToIsrMaxUs: number;
      /** How many fell in each band: below STORAGE_RADIO_TIMING_FIRST_US, then STORAGE_RADIO_TIMING_STEP_US wide. */
      readonly eventToIsrCounts: readonly number[];
      /** Longest from the processor waking to the radio interrupt starting, µs. */
      readonly wakeToIsrMaxUs: number;
      /** Antenna in use for single-antenna exchanges when written, from 0, and how often that choice moved this minute. */
      readonly antenna: number;
      readonly antennaChanges: number;
    })
  | (RecordBase & {
      readonly kind: 'anchor';
      /** The device's own un-offset clock at the same instant, in ms since experiment start. */
      readonly localMs: number;
      /** network - local, in ms. Exact: both come from a single RTC read. */
      readonly offsetMs: number;
    });

export interface PageSummary {
  readonly position: number;
  readonly seq: number;
  readonly firstTimestamp: number | null;
  readonly lastTimestamp: number | null;
  readonly payloadLength: number;
  readonly recordCount: number;
  readonly decodedCount: number;
  readonly crcValid: boolean | null;
  readonly repaired: boolean;
}

/** A STORAGE_TYPE_DIAGNOSTICS record. Counters are cumulative since boot and saturating. */
export interface DiagnosticsFields {
  /** Watchdog pets refused because some task was late. */
  readonly watchdogDeclines: number;
  /** Distinct episodes of lateness, by task name. Only non-zero entries are present. */
  readonly watchdogLate: ReadonlyMap<string, number>;
  /** Charger interrupts discarded as chatter by the de-bounce. */
  readonly chargerSuppressedEdges: number;
  /** BLE buffer allocations that returned NULL. */
  readonly wsfAllocFailures: number;
  readonly wsfLargestFailedLength: number;
  /** Peak simultaneous allocations per WSF pool, and each pool's capacity. */
  readonly wsfPoolPeak: readonly number[];
  readonly wsfPoolSize: readonly number[];
  /**
   * Times this device ran a network as master and heard nothing at all, cumulative since boot.
   * Non-zero means it struggled to reach its own network; MASTER_INELIGIBLE_AFTER_CYCLES or more
   * means it was demoted and stopped offering itself for election.
   */
  readonly masterCycleFailures: number;
  /** Leading eight hex digits of the git commit the firmware was built from. */
  readonly firmwareRevision: string;
  /** Built with uncommitted firmware changes, so the revision alone does not identify the build. */
  readonly firmwareModified: boolean;
  /** A diagnostic build: late radio arms are logged and radio interrupts timed. */
  readonly diagnosticBuild: boolean;
  /** Built with TempCo switched off, whatever the chip supports. */
  readonly tempcoDisabled: boolean;
  readonly tempcoAvailable: boolean;
  /** The last temperature reading lowered the regulator trims. */
  readonly tempcoApplied: boolean;
  /** Chip temperature, °C, or null before the first reading. */
  readonly temperatureC: number | null;
  /** Ranging slots that produced a decoded packet, and those that timed out or errored. */
  readonly radioRxOk: number;
  readonly radioRxFailed: number;
  /** Delayed transmissions and receives programmed after their slot had passed. A late receive aborts the round. */
  readonly radioTxLate: number;
  readonly radioRxArmLate: number;
  /** Radio interrupts longer than RADIO_ISR_BUDGET_US, and the longest once warmed up. */
  readonly radioIsrOverBudget: number;
  readonly radioIsrWarmMaxUs: number;
  /** Times the radio interrupt line stayed asserted and the radio was silenced. */
  readonly radioIrqStuck: number;
  /** Worst radio wake-up, against RADIO_WAKEUP_SAFETY_DELAY_US, and wake-ups that needed a radio reset. */
  readonly radioWakeMaxUs: number;
  readonly radioWakeFailures: number;
  /** Records discarded because the storage queue was full: data missing with no other trace. */
  readonly recordsDropped: number;
  /** Least free stack ever seen, in 32-bit words, by task name. Null for a task not running in this mode. */
  readonly stackFreeWords: ReadonlyMap<string, number | null>;
  /** Bluetooth controller restarts by the self-check. */
  readonly bleResets: number;
  /** Retired flash blocks, factory-marked and grown. */
  readonly nandBadBlocks: number;
}

export interface ParseReport {
  readonly format: LogFormat;
  /** Stream format version: 1 unframed, 2 framed. Null for v1 streams. */
  readonly formatVersion: number | null;
  readonly totalPages: number | null;
  readonly pagesRead: number;
  /** Pages the device could not read, as [position, seq]. */
  readonly holes: ReadonlyArray<readonly [number, number]>;
  /** Pages whose payload CRC did not match, as [position, seq]. */
  readonly crcFailures: ReadonlyArray<readonly [number, number]>;
  /** Pages that decoded fewer records than their header advertised. */
  readonly shortPages: ReadonlyArray<{ position: number; seq: number; decoded: number; expected: number }>;
  /** Structurally valid records whose contents failed a plausibility check. */
  readonly rejectedRecords: ReadonlyArray<{ position: number; seq: number; count: number }>;
  readonly repaired: ReadonlyArray<readonly [number, number]>;
  /** Page bounds running backwards: the device's clock base moved here. */
  readonly timeDiscontinuities: ReadonlyArray<{ position: number; seq: number; previousLast: number; thisFirst: number }>;
  readonly lastSeq: number | null;
  readonly truncated: boolean;
  readonly details: Uint8Array | null;
  readonly pages: readonly PageSummary[];
}

export interface ParseResult {
  readonly records: readonly LogRecord[];
  readonly report: ParseReport;
  readonly experimentStartTime: number;
}

export interface ExperimentDetails {
  readonly experimentStartTime: number;
  readonly experimentEndTime: number;
  readonly dailyStartTime: number;
  readonly dailyEndTime: number;
  readonly useDailyTimes: boolean;
  readonly numDevices: number;
  readonly uids: ReadonlyArray<Uint8Array>;
  readonly labels: readonly string[];
  readonly isTerminated: boolean;
}

// --- Format detection -------------------------------------------------------------------------------

const ASCII = new TextDecoder('latin1');

/**
 * Return 2 if the stream carries the v2 magic, otherwise 1.
 *
 * Safe because byte 0 of a v1 stream is always a record type in 1..8, and 'T' is 0x54.
 */
export function detectFormat(data: Uint8Array): LogFormat {
  return data.length >= 4 && ASCII.decode(data.subarray(0, 4)) === V2_STREAM_MAGIC ? 2 : 1;
}

// --- Record grammar ---------------------------------------------------------------------------------

/**
 * Structural length of the record starting at `offset`, or null if it cannot be determined.
 *
 * This is the table that a reader of an *unframed* payload cannot do without, and the reason
 * NANDLOG_RECORD_FRAMING matters: a type this function does not recognise stops the walk, and
 * everything after it in the page is lost. Types 7 and 8 are emitted at every boot, so a reader
 * missing either cannot get past page 0 — which is precisely how this package went stale.
 */
export function recordLength(data: Uint8Array, offset: number): number | null {
  const type = data[offset];
  let length: number | null;
  switch (type) {
    case STORAGE_TYPE.STORAGE_TYPE_VOLTAGE:
      length = 9;
      break;
    case STORAGE_TYPE.STORAGE_TYPE_CHARGING_EVENT:
    case STORAGE_TYPE.STORAGE_TYPE_MOTION:
      length = 6;
      break;
    case STORAGE_TYPE.STORAGE_TYPE_RANGES:
      length = offset + 6 <= data.length ? 6 + data[offset + 5]! * COMPRESSED_RANGE_DATUM_LENGTH : null;
      break;
    case STORAGE_TYPE.STORAGE_TYPE_IMU:
      // The IMU length byte counts itself, so the record is 5 + length, not 6 + length.
      length = offset + 6 <= data.length ? 5 + data[offset + 5]! : null;
      break;
    case STORAGE_TYPE.STORAGE_TYPE_BLE_SCAN:
      length = offset + 6 <= data.length ? 6 + data[offset + 5]! : null;
      break;
    case STORAGE_TYPE.STORAGE_TYPE_RESET_REASON:
      length = 7;
      break;
    case STORAGE_TYPE.STORAGE_TYPE_TIME_ANCHOR:
      length = 9;
      break;
    case STORAGE_TYPE.STORAGE_TYPE_DIAGNOSTICS:
      length = RECORD_HEADER_BYTES + DIAGNOSTICS_LAYOUT.size;
      break;
    case STORAGE_TYPE.STORAGE_TYPE_RADIO_ABORT:
      length = RECORD_HEADER_BYTES + RADIO_ABORT_LAYOUT.size;
      break;
    case STORAGE_TYPE.STORAGE_TYPE_SCHEDULE_CATCH:
      length = RECORD_HEADER_BYTES + SCHEDULE_CATCH_LAYOUT.size;
      break;
    case STORAGE_TYPE.STORAGE_TYPE_ROUND_START:
      length = RECORD_HEADER_BYTES + ROUND_START_LAYOUT.size;
      break;
    case STORAGE_TYPE.STORAGE_TYPE_SESSION_END:
      length = RECORD_HEADER_BYTES + SESSION_END_LAYOUT.size;
      break;
    case STORAGE_TYPE.STORAGE_TYPE_RADIO_TIMING:
      length = RECORD_HEADER_BYTES + RADIO_TIMING_LAYOUT.size;
      break;
    default:
      return null;
  }
  return length !== null && offset + length <= data.length ? length : null;
}

/** Decode a raw reset-status word into its hardware causes and the firmware's own verdict. */
export function decodeResetReason(status: number): {
  causes: string[];
  diagnostic: number;
  diagnosticLabel: string | null;
  isWatchdog: boolean;
} {
  const causes = RESET_STATUS_BITS.filter(([bit]) => status & RESET_STATUS_MASK & bit).map(([, name]) => name);
  const diagnostic = (status >>> RESET_DIAGNOSTIC_SHIFT) & RESET_DIAGNOSTIC_MASK;
  return {
    causes: causes.length ? causes : [`Unknown (0x${status.toString(16).padStart(4, '0').toUpperCase()})`],
    diagnostic,
    diagnosticLabel: describeDiagnostic(diagnostic),
    isWatchdog: (status & RESET_STATUS_MASK & 0x040) !== 0,
  };
}

const TASK_NAMES = Object.entries(WATCHDOG_TASK)
  .filter(([name]) => name !== 'WATCHDOG_NUM_TASKS')
  .sort((a, b) => a[1] - b[1])
  .map(([name]) => name.replace('WATCHDOG_TASK_', ''));

/**
 * Human-readable form of the four diagnostic bits.
 *
 * The per-task stall codes are derived from `watchdog_task_t` rather than transcribed, because the
 * firmware guarantees the two enums stay contiguous and the extractor re-checks it. Transcribing
 * them would mean a task reordering renames every stall in every archived log with nothing failing.
 */
export function describeDiagnostic(diagnostic: number): string | null {
  const D = RESET_DIAGNOSTIC;
  if (diagnostic === D.RESET_DIAGNOSTIC_NONE) return null;
  if (diagnostic >= D.RESET_DIAGNOSTIC_STALL_TIME_ALIGNED && diagnostic < D.RESET_DIAGNOSTIC_STALL_MULTIPLE) {
    return `stalled: ${TASK_NAMES[diagnostic - D.RESET_DIAGNOSTIC_STALL_TIME_ALIGNED]}Task`;
  }
  switch (diagnostic) {
    case D.RESET_DIAGNOSTIC_STALL_MULTIPLE: return 'stalled: several tasks (scheduler or tick stopped)';
    case D.RESET_DIAGNOSTIC_HARD_FAULT: return 'hard fault';
    case D.RESET_DIAGNOSTIC_STACK_OVERFLOW: return 'stack overflow';
    case D.RESET_DIAGNOSTIC_ASSERT: return 'assertion failed';
    case D.RESET_DIAGNOSTIC_MALLOC_FAILED: return 'allocation failed';
    case D.RESET_DIAGNOSTIC_STORAGE_FATAL: return 'storage fault';
    case D.RESET_DIAGNOSTIC_STORAGE_UNWRITABLE: return 'storage unwritable';
    case D.RESET_DIAGNOSTIC_CLOCK_STOPPED: return '32 kHz clock stopped';
    case D.RESET_DIAGNOSTIC_NOTHING_RECORDED:
      return 'nothing recorded a cause (breadcrumb survived, so no handler ran)';
    default: return `unknown diagnostic ${diagnostic}`;
  }
}

const BATTERY_LABELS = new Map<number, string>([
  [BATTERY_EVENT.BATTERY_PLUGGED, 'Plugged'],
  [BATTERY_EVENT.BATTERY_UNPLUGGED, 'Unplugged'],
  [BATTERY_EVENT.BATTERY_CHARGING, 'Charging'],
  [BATTERY_EVENT.BATTERY_NOT_CHARGING, 'Not Charging'],
  [BATTERY_EVENT.BATTERY_CRITICAL_VOLTAGE, 'Critical Voltage'],
]);

// --- Payload walking ----------------------------------------------------------------------------------

interface WalkOptions {
  readonly page: number;
  readonly seq: number;
  /** v1 only: slide forward a byte at a time when a record does not decode. */
  readonly resynchronize: boolean;
  /** v2 framed only: each record is prefixed with its own uint16 length. */
  readonly framed: boolean;
}

/**
 * Strip per-record length prefixes from a framed payload.
 *
 * Returns the payload in the same layout an unframed page uses, plus a map from each record's offset
 * in it to the offset of the next. Those boundaries are the whole point of framing: a record this
 * reader does not recognise can be stepped over exactly rather than guessed at. Returns null if the
 * prefixes do not describe the payload they were given, which means damage rather than unfamiliarity.
 */
function stripFraming(payload: Uint8Array): { data: Uint8Array; boundaries: Map<number, number> } | null {
  const out: number[] = [];
  const boundaries = new Map<number, number>();
  const view = new DataView(payload.buffer, payload.byteOffset, payload.byteLength);
  let offset = 0;
  while (offset + NANDLOG_FRAMING_LENGTH_BYTES <= payload.length) {
    const dataLength = view.getUint16(offset, true);
    offset += NANDLOG_FRAMING_LENGTH_BYTES;
    if (offset + RECORD_HEADER_BYTES + dataLength > payload.length) return null;
    const start = out.length;
    for (let i = 0; i < RECORD_HEADER_BYTES + dataLength; i += 1) out.push(payload[offset + i]!);
    boundaries.set(start, out.length);
    offset += RECORD_HEADER_BYTES + dataLength;
  }
  return offset === payload.length ? { data: Uint8Array.from(out), boundaries } : null;
}

const DIAGNOSTICS_FIELD = Object.fromEntries(DIAGNOSTICS_LAYOUT.fields.map((field) => [field.name, field.offset]));

/** A diagnostics payload, every field read at its offset in the firmware's own struct. */
function decodeDiagnostics(view: DataView, at: number): DiagnosticsFields {
  const u8 = (name: string, index = 0) => view.getUint8(at + DIAGNOSTICS_FIELD[name]! + index);
  const u16 = (name: string, index = 0) => view.getUint16(at + DIAGNOSTICS_FIELD[name]! + 2 * index, true);
  const u32 = (name: string, index = 0) => view.getUint32(at + DIAGNOSTICS_FIELD[name]! + 4 * index, true);
  const late = new Map<string, number>();
  WATCHDOG_TASK_NAMES.forEach((name, task) => {
    const episodes = u8('watchdog_late_episodes', task);
    if (episodes) late.set(name, episodes);
  });
  const peak: number[] = [];
  const size: number[] = [];
  for (let pool = 0; pool < STORAGE_DIAGNOSTIC_NUM_POOLS; pool += 1) {
    peak.push(u8('wsf_pool_high_water', pool));
    size.push(u8('wsf_pool_capacity', pool));
  }
  const stacks = new Map<string, number | null>();
  DIAGNOSTICS_STACK_NAMES.forEach((name, index) => {
    const words = u16('stack_free_words', index);
    stacks.set(name, words === STORAGE_DIAGNOSTIC_STACK_UNMONITORED ? null : words);
  });
  const flags = u8('status_flags');
  const temperature = view.getInt8(at + DIAGNOSTICS_FIELD.temperature_c!);
  return {
    watchdogDeclines: u16('watchdog_declines'),
    watchdogLate: late,
    chargerSuppressedEdges: u16('charger_suppressed_edges'),
    wsfAllocFailures: u16('wsf_alloc_failures'),
    wsfLargestFailedLength: u16('wsf_largest_failed_length'),
    wsfPoolPeak: peak,
    wsfPoolSize: size,
    masterCycleFailures: u8('master_cycle_failures'),
    firmwareRevision: u32('firmware_revision').toString(16).padStart(8, '0'),
    firmwareModified: (flags & STORAGE_DIAGNOSTIC_FLAG_FIRMWARE_MODIFIED) !== 0,
    diagnosticBuild: (flags & STORAGE_DIAGNOSTIC_FLAG_DIAGNOSTIC_BUILD) !== 0,
    tempcoDisabled: (flags & STORAGE_DIAGNOSTIC_FLAG_TEMPCO_DISABLED) !== 0,
    tempcoAvailable: (flags & STORAGE_DIAGNOSTIC_FLAG_TEMPCO_AVAILABLE) !== 0,
    tempcoApplied: (flags & STORAGE_DIAGNOSTIC_FLAG_TEMPCO_APPLIED) !== 0,
    temperatureC: temperature === -128 ? null : temperature,
    radioRxOk: u32('radio_rx_ok'),
    radioRxFailed: u32('radio_rx_failed'),
    radioTxLate: u16('radio_tx_late'),
    radioRxArmLate: u16('radio_rx_arm_late'),
    radioIsrOverBudget: u16('radio_isr_over_budget'),
    radioIsrWarmMaxUs: u16('radio_isr_warm_max_us'),
    radioIrqStuck: u16('radio_irq_stuck'),
    radioWakeMaxUs: u16('radio_wake_max_us'),
    radioWakeFailures: u16('radio_wake_failures'),
    recordsDropped: u16('storage_records_dropped'),
    stackFreeWords: stacks,
    bleResets: u8('ble_resets'),
    nandBadBlocks: u16('nand_bad_blocks'),
  };
}

function walkPayload(
  payload: Uint8Array,
  options: WalkOptions,
  sink: LogRecord[],
): { decoded: number; rejected: number } {
  let data = payload;
  let boundaries: Map<number, number> | null = null;
  if (options.framed) {
    const stripped = stripFraming(payload);
    if (!stripped) return { decoded: 0, rejected: 0 };
    data = stripped.data;
    boundaries = stripped.boundaries;
  }

  const view = new DataView(data.buffer, data.byteOffset, data.byteLength);
  const { page, seq } = options;
  let i = 0;
  let decoded = 0;
  let rejected = 0;

  while (i + RECORD_HEADER_BYTES <= data.length) {
    const type = data[i]!;
    const ms = view.getUint32(i + 1, true);
    // The 500 ms grid is a v1 resynchronisation heuristic and nothing more. Current firmware stores
    // 10 ms resolution, so applying it to a v2 page would reject essentially every record.
    const onGrid = options.resynchronize ? ms % TIMESTAMP_QUANTUM_MS === 0 : true;
    let consumed = 0;

    if (onGrid && type >= 1 && type < STORAGE_TYPE.STORAGE_NUM_TYPES) {
      const base = { type, ms, page, seq };
      switch (type) {
        case STORAGE_TYPE.STORAGE_TYPE_VOLTAGE: {
          if (i + 9 > data.length) break;
          const millivolts = view.getUint32(i + 5, true);
          if (millivolts > 0 && millivolts < 4500) {
            sink.push({ ...base, kind: 'voltage', millivolts });
            consumed = 9;
          }
          break;
        }
        case STORAGE_TYPE.STORAGE_TYPE_CHARGING_EVENT: {
          if (i + 6 > data.length) break;
          const event = data[i + 5]!;
          const label = BATTERY_LABELS.get(event);
          if (label !== undefined) {
            sink.push({ ...base, kind: 'charging', event, label });
            consumed = 6;
          }
          break;
        }
        case STORAGE_TYPE.STORAGE_TYPE_MOTION: {
          if (i + 6 > data.length) break;
          const value = data[i + 5]!;
          if (value === 0 || value === 1) {
            sink.push({ ...base, kind: 'motion', inMotion: value === 1 });
            consumed = 6;
          }
          break;
        }
        case STORAGE_TYPE.STORAGE_TYPE_RANGES: {
          if (i + 6 > data.length) break;
          const count = data[i + 5]!;
          if (count < MAX_NUM_RANGING_DEVICES && i + 6 + count * COMPRESSED_RANGE_DATUM_LENGTH <= data.length) {
            const ranges = new Map<number, number>();
            for (let j = 0; j < count; j += 1) {
              const at = i + 6 + j * COMPRESSED_RANGE_DATUM_LENGTH;
              const millimetres = view.getUint16(at + 1, true);
              // The firmware's own bound, not the Python tool's 16000, which silently discarded
              // every range between 16 m and 32 m that the device considered valid.
              if (millimetres < MAX_VALID_RANGE_MM) ranges.set(data[at]!, millimetres);
            }
            sink.push({ ...base, kind: 'ranges', ranges });
            consumed = 6 + count * COMPRESSED_RANGE_DATUM_LENGTH;
          }
          break;
        }
        case STORAGE_TYPE.STORAGE_TYPE_IMU: {
          if (i + 6 > data.length) break;
          const imuLength = data[i + 5]!;
          if (imuLength === 7 && i + 12 <= data.length) {
            sink.push({
              ...base,
              kind: 'imu',
              accel: [view.getInt16(i + 6, true), view.getInt16(i + 8, true), view.getInt16(i + 10, true)],
            });
            consumed = 5 + imuLength;
          }
          break;
        }
        case STORAGE_TYPE.STORAGE_TYPE_BLE_SCAN: {
          if (i + 6 > data.length) break;
          const count = data[i + 5]!;
          if (count < MAX_NUM_RANGING_DEVICES && i + 6 + count <= data.length) {
            sink.push({ ...base, kind: 'bleScan', peers: Array.from(data.subarray(i + 6, i + 6 + count)) });
            consumed = 6 + count;
          }
          break;
        }
        case STORAGE_TYPE.STORAGE_TYPE_RESET_REASON: {
          if (i + 7 > data.length) break;
          const status = view.getUint16(i + 5, true);
          const decodedReset = decodeResetReason(status);
          sink.push({ ...base, kind: 'reset', status, ...decodedReset });
          consumed = 7;
          break;
        }
        case STORAGE_TYPE.STORAGE_TYPE_DIAGNOSTICS: {
          // Near-misses, not faults: conditions the firmware recovered from and that nothing else in
          // the log would show. Every counter is cumulative since boot and saturating.
          if (i + RECORD_HEADER_BYTES + DIAGNOSTICS_LAYOUT.size > data.length) break;
          sink.push({ ...base, kind: 'diagnostics', ...decodeDiagnostics(view, i + RECORD_HEADER_BYTES) });
          consumed = RECORD_HEADER_BYTES + DIAGNOSTICS_LAYOUT.size;
          break;
        }
        case STORAGE_TYPE.STORAGE_TYPE_RADIO_ABORT: {
          // Written only by a diagnostic build, once for each radio receive that missed its slot
          if (i + RECORD_HEADER_BYTES + RADIO_ABORT_LAYOUT.size > data.length) break;
          const at = i + RECORD_HEADER_BYTES;
          const field = (name: string) => at + RADIO_ABORT_LAYOUT.fields.find((f) => f.name === name)!.offset;
          const phase = view.getUint8(field('phase'));
          const isrElapsed = view.getUint16(field('isr_elapsed_us'), true);
          const since = view.getUint16(field('since_temperature_ms'), true);
          const trigger = view.getUint8(field('trigger'));
          const isrEntry = view.getUint16(field('isr_entry_us'), true);
          const eventToIsr = view.getInt16(field('event_to_isr_us'), true);
          const asleep = view.getUint16(field('asleep_us'), true);
          const wakeToIsr = view.getUint16(field('wake_to_isr_us'), true);
          sink.push({
            ...base,
            kind: 'radioAbort',
            phase: phase === STORAGE_RADIO_ABORT_PHASE_RANGING ? 'ranging' : phase === STORAGE_RADIO_ABORT_PHASE_STATUS ? 'status' : phase,
            slot: view.getUint8(field('slot')),
            scheduleSize: view.getUint8(field('schedule_size')),
            lateUs: view.getInt16(field('late_us'), true),
            isrElapsedUs: isrElapsed === STORAGE_RADIO_ABORT_UNMEASURED ? null : isrElapsed,
            isrEvents: view.getUint8(field('isr_events')),
            sinceTemperatureMs: since === STORAGE_RADIO_ABORT_UNMEASURED ? null : since,
            trigger: trigger === STORAGE_RADIO_ABORT_TRIGGER_TX_DONE ? 'txDone'
              : trigger === STORAGE_RADIO_ABORT_TRIGGER_RX_FRAME ? 'rxFrame'
              : trigger === STORAGE_RADIO_ABORT_TRIGGER_RX_TIMEOUT ? 'rxTimeout'
              : trigger === STORAGE_RADIO_ABORT_TRIGGER_RX_ERROR ? 'rxError'
              : trigger === 0 ? null : trigger,
            isrEntryUs: isrEntry === STORAGE_RADIO_ABORT_UNMEASURED ? null : isrEntry,
            eventToIsrUs: eventToIsr === STORAGE_RADIO_ABORT_NO_EVENT_TIME ? null : eventToIsr,
            asleepUs: asleep === STORAGE_RADIO_ABORT_UNMEASURED ? null : asleep,
            wakeToIsrUs: wakeToIsr === STORAGE_RADIO_ABORT_UNMEASURED ? null : wakeToIsr,
          });
          consumed = RECORD_HEADER_BYTES + RADIO_ABORT_LAYOUT.size;
          break;
        }
        case STORAGE_TYPE.STORAGE_TYPE_SCHEDULE_CATCH: {
          // Written only by a diagnostic build, once for each participant wake-up from its timer
          if (i + RECORD_HEADER_BYTES + SCHEDULE_CATCH_LAYOUT.size > data.length) break;
          const at = i + RECORD_HEADER_BYTES;
          const field = (name: string) => at + SCHEDULE_CATCH_LAYOUT.fields.find((f) => f.name === name)!.offset;
          const firstCopy = view.getUint8(field('first_copy'));
          const heard = firstCopy !== STORAGE_SCHEDULE_CATCH_NONE;
          const roundsMissed = view.getUint8(field('rounds_missed'));
          const measured = (value: number) => (value === STORAGE_SCHEDULE_CATCH_UNMEASURED ? null : value);
          sink.push({
            ...base,
            kind: 'scheduleCatch',
            firstCopy: heard ? firstCopy : null,
            roundsMissed: roundsMissed === STORAGE_SCHEDULE_CATCH_NONE ? null : roundsMissed,
            leadUs: heard ? view.getInt32(field('lead_us'), true) : null,
            timerToTaskUs: measured(view.getUint16(field('timer_to_task_us'), true)),
            wakeUs: measured(view.getUint16(field('wake_us'), true)),
            rxErrors: view.getUint8(field('rx_errors')),
            otherFrames: view.getUint8(field('other_frames')),
            firstErrorUs: measured(view.getUint16(field('first_error_us'), true)),
            carrierOffsetPpm: heard ? view.getInt16(field('carrier_offset_cppm'), true) / 100 : null,
            wakeCorrectionUs: view.getInt16(field('wake_correction_us'), true),
            timerLatencyUs: measured(view.getUint16(field('timer_latency_us'), true)),
          });
          consumed = RECORD_HEADER_BYTES + SCHEDULE_CATCH_LAYOUT.size;
          break;
        }
        case STORAGE_TYPE.STORAGE_TYPE_ROUND_START: {
          // Written only by a diagnostic build, by the master as each round ends
          if (i + RECORD_HEADER_BYTES + ROUND_START_LAYOUT.size > data.length) break;
          const at = i + RECORD_HEADER_BYTES;
          const field = (name: string) => at + ROUND_START_LAYOUT.fields.find((f) => f.name === name)!.offset;
          const flags = view.getUint8(field('flags'));
          const wake = view.getUint16(field('wake_us'), true);
          const latency = view.getUint16(field('timer_latency_us'), true);
          sink.push({
            ...base,
            kind: 'roundStart',
            timerToTaskUs: view.getUint16(field('timer_to_task_us'), true),
            wakeUs: wake === STORAGE_SCHEDULE_CATCH_UNMEASURED ? null : wake,
            timerToTransmitUs: view.getUint16(field('timer_to_transmit_us'), true),
            secondCopyFailed: (flags & STORAGE_ROUND_START_FLAG_SECOND_COPY_FAILED) !== 0,
            computed: (flags & STORAGE_ROUND_START_FLAG_COMPUTED) !== 0,
            abandoned: (flags & STORAGE_ROUND_START_FLAG_ABANDONED) !== 0,
            joinHeard: (flags & STORAGE_ROUND_START_FLAG_JOIN_HEARD) !== 0,
            joinRelayed: (flags & STORAGE_ROUND_START_FLAG_JOIN_RELAYED) !== 0,
            scheduleSize: view.getUint8(field('schedule_size')),
            devicesRanged: view.getUint8(field('devices_ranged')),
            timerLatencyUs: latency === STORAGE_SCHEDULE_CATCH_UNMEASURED ? null : latency,
          });
          consumed = RECORD_HEADER_BYTES + ROUND_START_LAYOUT.size;
          break;
        }
        case STORAGE_TYPE.STORAGE_TYPE_SESSION_END: {
          // Written only by a diagnostic build, each time the ranging scheduler stops
          if (i + RECORD_HEADER_BYTES + SESSION_END_LAYOUT.size > data.length) break;
          const at = i + RECORD_HEADER_BYTES;
          const field = (name: string) => at + SESSION_END_LAYOUT.fields.find((f) => f.name === name)!.offset;
          const reason = view.getUint8(field('reason'));
          const nameOf = (members: Record<string, number>, value: number) =>
            Object.entries(members).find(([, v]) => v === value)?.[0] ?? String(value);
          sink.push({
            ...base,
            kind: 'sessionEnd',
            reason: reason === STORAGE_SESSION_END_STOPPED ? 'stopped'
              : reason === STORAGE_SESSION_END_SEARCH_TIMEOUT ? 'search timeout'
              : reason === STORAGE_SESSION_END_COLLISION ? 'collision'
              : reason === STORAGE_SESSION_END_SILENT ? 'heard nobody' : 'unknown',
            role: nameOf(SCHEDULE_ROLE, view.getUint8(field('role'))),
            scheduleSize: view.getUint8(field('schedule_size')),
            collision: reason === STORAGE_SESSION_END_COLLISION ? {
              phase: nameOf(SCHEDULER_PHASE, view.getUint8(field('collision_phase'))),
              packet: nameOf(PACKET_TYPE, view.getUint8(field('collision_type'))),
              source: view.getUint8(field('collision_source')),
              atUs: view.getUint16(field('collision_at_us'), true),
            } : null,
            sessionMs: view.getUint32(field('session_ms'), true),
            roundsRanged: view.getUint16(field('rounds_ranged'), true),
            schedulesHeard: view.getUint16(field('schedules_heard'), true),
            joinRequestsSent: view.getUint16(field('join_requests_sent'), true),
            joinRequestsHeard: view.getUint16(field('join_requests_heard'), true),
            listenErrors: view.getUint16(field('listen_errors'), true),
            stalls: view.getUint8(field('stalls')),
          });
          consumed = RECORD_HEADER_BYTES + SESSION_END_LAYOUT.size;
          break;
        }
        case STORAGE_TYPE.STORAGE_TYPE_RADIO_TIMING: {
          // Written only by a diagnostic build, once a minute
          if (i + RECORD_HEADER_BYTES + RADIO_TIMING_LAYOUT.size > data.length) break;
          const at = i + RECORD_HEADER_BYTES;
          const layout = (name: string) => RADIO_TIMING_LAYOUT.fields.find((f) => f.name === name)!;
          const u16 = (name: string) => view.getUint16(at + layout(name).offset, true);
          const bands = layout('event_to_isr_counts');
          const slackMin = view.getInt16(at + layout('slack_min_us').offset, true);
          const eventToIsrMin = u16('event_to_isr_min_us');
          sink.push({
            ...base,
            kind: 'radioTiming',
            arms: u16('arms'),
            afterSleep: u16('after_sleep'),
            duringSleepEntry: u16('during_sleep_entry'),
            slackMinUs: slackMin === 0x7fff ? null : slackMin,
            slackUnder25Us: u16('slack_under_25_us'),
            eventToIsrMinUs: eventToIsrMin === 0xffff ? null : eventToIsrMin,
            eventToIsrMaxUs: u16('event_to_isr_max_us'),
            eventToIsrCounts: Array.from({ length: bands.size / bands.elementSize }, (_, band) => view.getUint16(at + bands.offset + band * 2, true)),
            wakeToIsrMaxUs: u16('wake_to_isr_max_us'),
            antenna: view.getUint8(at + layout('antenna').offset),
            antennaChanges: view.getUint8(at + layout('antenna_changes').offset),
          });
          consumed = RECORD_HEADER_BYTES + RADIO_TIMING_LAYOUT.size;
          break;
        }
        case STORAGE_TYPE.STORAGE_TYPE_TIME_ANCHOR: {
          if (i + 9 > data.length) break;
          const localMs = view.getUint32(i + 5, true);
          sink.push({ ...base, kind: 'anchor', localMs, offsetMs: ms - localMs });
          consumed = 9;
          break;
        }
        default:
          break;
      }
    }

    if (boundaries) {
      // The device said where this record ends. Take its word over anything inferred here, in both
      // directions: a record that decoded is still advanced past by its declared length, so the two
      // can never drift apart.
      const next = boundaries.get(i);
      if (next === undefined) break;
      if (consumed) decoded += 1;
      else rejected += 1;
      i = next;
    } else if (consumed) {
      i += consumed;
      decoded += 1;
    } else if (options.resynchronize) {
      i += 1;
    } else {
      // Record-aligned payload: step over this record using its own structural length rather than
      // abandoning the page. Only an unrecognisable type byte is unrecoverable.
      const step = recordLength(data, i);
      if (step === null) break;
      i += step;
      rejected += 1;
    }
  }

  return { decoded, rejected };
}

// --- Experiment details ----------------------------------------------------------------------------

/** Decode the 239-byte `experiment_details_t` blob carried in a v2 stream header. */
export function parseExperimentDetails(details: Uint8Array): ExperimentDetails {
  const view = new DataView(details.buffer, details.byteOffset, details.byteLength);
  const uidsAt = 18;
  const labelsAt = uidsAt + MAX_NUM_RANGING_DEVICES * EUI_LEN;
  const uids: Uint8Array[] = [];
  const labels: string[] = [];
  for (let i = 0; i < MAX_NUM_RANGING_DEVICES; i += 1) {
    uids.push(details.subarray(uidsAt + i * EUI_LEN, uidsAt + (i + 1) * EUI_LEN));
    // NUL padded, not NUL terminated, so trim rather than reading to a terminator.
    const raw = details.subarray(labelsAt + i * EUI_NAME_MAX_LEN, labelsAt + (i + 1) * EUI_NAME_MAX_LEN);
    labels.push(ASCII.decode(raw).replace(/\0+$/, ''));
  }
  const numDevices = details[17]!;
  return {
    experimentStartTime: view.getUint32(0, true),
    experimentEndTime: view.getUint32(4, true),
    dailyStartTime: view.getUint32(8, true),
    dailyEndTime: view.getUint32(12, true),
    useDailyTimes: details[16] !== 0,
    numDevices,
    uids: uids.slice(0, numDevices),
    labels: labels.slice(0, numDevices),
    isTerminated: details[details.length - 1] !== 0,
  };
}

// --- Parsers -------------------------------------------------------------------------------------------

const EMPTY_REPORT = {
  holes: [] as ReadonlyArray<readonly [number, number]>,
  crcFailures: [] as ReadonlyArray<readonly [number, number]>,
  shortPages: [] as ParseReport['shortPages'],
  rejectedRecords: [] as ParseReport['rejectedRecords'],
  repaired: [] as ReadonlyArray<readonly [number, number]>,
  timeDiscontinuities: [] as ParseReport['timeDiscontinuities'],
};

/**
 * Parse a legacy unframed record stream.
 *
 * `experimentStartTime` is required: a v1 file carries no header, so the only place it survives is
 * the filename. This is a faithful port of the Python grammar, byte-at-a-time resynchronisation and
 * all, because its job is to reproduce results from archived files rather than to improve on them.
 */
export function parseV1(data: Uint8Array, experimentStartTime: number): ParseResult {
  const records: LogRecord[] = [];
  walkPayload(data, { page: -1, seq: -1, resynchronize: true, framed: false }, records);
  return {
    records,
    experimentStartTime,
    report: {
      format: 1,
      formatVersion: null,
      totalPages: null,
      pagesRead: 0,
      lastSeq: null,
      truncated: false,
      details: null,
      pages: [],
      ...EMPTY_REPORT,
    },
  };
}

/**
 * Parse a page-framed stream.
 *
 * `repairs` is an optional `{seq: payload}` map from earlier retransmission rounds; a page that
 * arrived unreadable or corrupt is replaced by its repaired copy, so the report describes what is
 * *still* missing rather than what the original transfer lost.
 */
export function parseV2(
  data: Uint8Array,
  experimentStartTimeOverride?: number,
  repairs: ReadonlyMap<number, Uint8Array> = new Map(),
): ParseResult {
  if (data.length < 16 || detectFormat(data) !== 2) throw new Error('not a v2 stream');
  const view = new DataView(data.buffer, data.byteOffset, data.byteLength);
  const formatVersion = view.getUint16(4, true);
  const detailsLength = view.getUint16(6, true);
  const totalPages = view.getUint32(8, true);
  if (formatVersion !== V2_FORMAT_VERSION_UNFRAMED && formatVersion !== V2_FORMAT_VERSION_FRAMED) {
    throw new Error(`unsupported v2 stream format version ${formatVersion}`);
  }
  const framed = formatVersion === V2_FORMAT_VERSION_FRAMED;

  let offset = 16;
  const details = data.subarray(offset, offset + detailsLength);
  offset += detailsLength;
  if (details.length < 4 && experimentStartTimeOverride === undefined) {
    throw new Error('stream header is missing experiment details and no start time was supplied');
  }
  const experimentStartTime =
    experimentStartTimeOverride ?? new DataView(details.buffer, details.byteOffset, 4).getUint32(0, true);

  const records: LogRecord[] = [];
  const pages: PageSummary[] = [];
  const holes: Array<readonly [number, number]> = [];
  const crcFailures: Array<readonly [number, number]> = [];
  const shortPages: Array<{ position: number; seq: number; decoded: number; expected: number }> = [];
  const rejectedRecords: Array<{ position: number; seq: number; count: number }> = [];
  const repairedPages: Array<readonly [number, number]> = [];
  const timeDiscontinuities: Array<{ position: number; seq: number; previousLast: number; thisFirst: number }> = [];
  const seenSeqs = new Set<number>();

  let previousLast: number | null = null;
  let position = 0;
  let truncated = false;
  let lastSeq: number | null = null;

  // Stop after the declared number of pages rather than when the bytes run out. A device that keeps
  // logging during a transfer can send a few bytes past its own declared total, because
  // total_payload_bytes is sampled before the last page is read, and reading that tail as another
  // page frame would invent a corrupt page and request a retransmission for it.
  while (position < totalPages && offset + 20 <= data.length) {
    const seq = view.getUint32(offset, true);
    const firstRaw = view.getUint32(offset + 4, true);
    const lastRaw = view.getUint32(offset + 8, true);
    const payloadLength = view.getUint16(offset + 12, true);
    const recordCount = view.getUint16(offset + 14, true);
    const payloadCrc = view.getUint32(offset + 16, true);
    offset += 20;
    position += 1;

    let payload: Uint8Array | null = null;
    if (payloadLength) {
      if (offset + payloadLength > data.length) {
        truncated = true;
        position -= 1;
        break;
      }
      payload = data.subarray(offset, offset + payloadLength);
      offset += payloadLength;
    }
    lastSeq = seq;
    seenSeqs.add(seq);

    const firstTimestamp = firstRaw === NANDLOG_NO_TIMESTAMP ? null : firstRaw;
    const lastTimestamp = lastRaw === NANDLOG_NO_TIMESTAMP ? null : lastRaw;

    // Page bounds that run backwards mean the device's clock base moved mid-log. The device selects
    // a time range by scanning these same bounds, so where they are not ordered a date-limited
    // download may be short — and nothing else in the stream would reveal it, since what the device
    // sends is internally consistent and reports no gaps. A page starting less than the tolerance
    // early is not that: a range stamped at its round's start can be written after a record stamped
    // later, and keeps its own time.
    if (firstTimestamp !== null && previousLast !== null && previousLast - firstTimestamp > NANDLOG_TIMESTAMP_TOLERANCE_MS) {
      timeDiscontinuities.push({ position: position - 1, seq, previousLast, thisFirst: firstTimestamp });
    }
    if (lastTimestamp !== null) previousLast = lastTimestamp;

    // Verify independently of the device, which also catches corruption introduced in transit.
    let crcValid: boolean | null = payload === null ? null : crc32(payload) === payloadCrc;
    let repaired = false;
    if (crcValid !== true) {
      const repair = repairs.get(seq);
      if (repair) {
        payload = repair;
        crcValid = true;
        repaired = true;
        repairedPages.push([position - 1, seq]);
      }
    }

    let decodedCount = 0;
    if (payload === null) {
      holes.push([position - 1, seq]);
    } else if (crcValid === false) {
      crcFailures.push([position - 1, seq]);
    } else {
      const walk = walkPayload(payload, { page: position - 1, seq, resynchronize: false, framed }, records);
      decodedCount = walk.decoded;
      if (walk.rejected) rejectedRecords.push({ position: position - 1, seq, count: walk.rejected });
      // A repaired frame's own record_count came from the frame that failed, so it proves nothing.
      if (!repaired && recordCount && walk.decoded < recordCount) {
        shortPages.push({ position: position - 1, seq, decoded: walk.decoded, expected: recordCount });
      }
    }

    pages.push({
      position: position - 1,
      seq,
      firstTimestamp,
      lastTimestamp,
      payloadLength,
      recordCount,
      decodedCount,
      crcValid,
      repaired,
    });
  }

  // A page lost to a truncated transfer has no frame to substitute into, so repaired copies of pages
  // the stream never carried are decoded here instead. They must count towards the total, or the
  // caller keeps asking for pages it already holds.
  let pagesRead = position;
  for (const seq of [...repairs.keys()].filter((s) => !seenSeqs.has(s)).sort((a, b) => a - b)) {
    const walk = walkPayload(repairs.get(seq)!, { page: -1, seq, resynchronize: false, framed }, records);
    pagesRead += 1;
    repairedPages.push([-1, seq]);
    if (lastSeq === null || seq > lastSeq) lastSeq = seq;
    if (walk.rejected) rejectedRecords.push({ position: -1, seq, count: walk.rejected });
  }

  return {
    records,
    experimentStartTime,
    report: {
      format: 2,
      formatVersion,
      totalPages,
      pagesRead,
      holes,
      crcFailures,
      shortPages,
      rejectedRecords,
      repaired: repairedPages,
      timeDiscontinuities,
      lastSeq,
      truncated: truncated || pagesRead < totalPages,
      details,
      pages,
    },
  };
}

/**
 * Parse either format, dispatching on the stream magic.
 *
 * `repairs` is a `{seq: payload}` map gathered from retransmission rounds, as `extractPages` returns.
 * It is accepted here rather than only on `parseV2` so that the repair loop a caller writes is the
 * same shape as `tottag.py`'s, which takes repairs on its top-level `parse` too. A v1 stream predates
 * page sequence numbers and so cannot be repaired; repairs are ignored on that path.
 */
export function parse(
  data: Uint8Array,
  experimentStartTime?: number,
  repairs: ReadonlyMap<number, Uint8Array> = new Map(),
): ParseResult {
  if (detectFormat(data) === 2) return parseV2(data, experimentStartTime, repairs);
  if (experimentStartTime === undefined) {
    throw new Error('a v1 stream carries no header, so experimentStartTime must be supplied');
  }
  return parseV1(data, experimentStartTime);
}

/**
 * The sequence numbers a stream declaring `totalPages` should carry, as a half-open `[from, to)`.
 *
 * Sequence numbers run contiguously within an epoch, so the whole set follows from one page whose
 * number can be believed and where it sat. Counting back from that page's POSITION rather than
 * starting at its number is what still finds a missing first page.
 *
 * A damaged frame ahead of it can shift that position either way — a junk header inserts a phantom
 * page, a bad payload length swallows a real one — so the estimate is clamped by the pages actually
 * `held`: the range must reach down to the lowest of them and up to the highest.
 */
function expectedSeqRange(
  firstVerified: { readonly position: number; readonly seq: number },
  held: Iterable<number>,
  totalPages: number,
): readonly [number, number] {
  let lowest = Infinity;
  let highest = -Infinity;
  for (const seq of held) {
    if (seq < lowest) lowest = seq;
    if (seq > highest) highest = seq;
  }
  const base = Math.max(Math.min(firstVerified.seq - firstVerified.position, lowest), highest - totalPages + 1);
  return [Math.max(base, 0), base + totalPages];
}

/**
 * Sequence numbers worth asking the device to resend.
 *
 * Every page the device declared that is not held intact: holes, CRC failures, and pages a truncated
 * or damaged transfer never delivered at all. A page whose CRC passed arrived intact, so a page that
 * merely stopped decoding early has a record-level problem that a second copy of the same bytes would
 * not fix.
 *
 * A transfer in which no page arrived yields an empty list, because there is no anchor to count from
 * — a stream does not necessarily begin at sequence zero, since a wrapped log or a time-bounded
 * download starts partway through the epoch. That case is a failed transfer rather than a partial
 * one, and the caller should repeat the whole download instead of naming pages.
 */
export function missingSeqs(report: ParseReport): number[] {
  // Only a page whose payload verified can be believed about its own sequence number: one that failed
  // CRC carries that number in the same damaged bytes as the payload. A repaired page verified when its
  // replacement was collected, including one the stream never carried at all.
  const held = new Set<number>();
  for (const page of report.pages) if (page.crcValid === true) held.add(page.seq);
  for (const [position, seq] of report.repaired) if (position < 0) held.add(seq);
  const first = report.pages.find((page) => page.crcValid === true);
  if (!first || !report.totalPages) return [];

  // Ask for what the device declared less what is held, rather than naming each bad page by its
  // position: a phantom page written into the stream by a damaged transfer would otherwise shift every
  // name after it, sending the repair loop after pages it already holds while the real gaps go unasked.
  const [from, to] = expectedSeqRange(first, held, report.totalPages);
  const missing: number[] = [];
  for (let seq = from; seq < to; seq += 1) if (!held.has(seq)) missing.push(seq);
  return missing;
}

/**
 * Extract `{seq: frame}` for every CRC-valid page in a retransmission response, where a frame is the
 * whole 20-byte page header followed by its payload — exactly the bytes a stream carries.
 *
 * Keeping the header is what makes a repaired page substitutable. The device sends its own timestamps,
 * record count, payload length and CRC with each page, all mutually consistent and already verified
 * here, so splicing a frame into a stream needs no field rewritten and cannot leave a header
 * disagreeing with the payload under it.
 */
export function extractPageFrames(data: Uint8Array): Map<number, Uint8Array> {
  const frames = new Map<number, Uint8Array>();
  if (data.length < 16 || detectFormat(data) !== 2) return frames;
  const view = new DataView(data.buffer, data.byteOffset, data.byteLength);
  let offset = 16 + view.getUint16(6, true);
  while (offset + 20 <= data.length) {
    const start = offset;
    const seq = view.getUint32(offset, true);
    const payloadLength = view.getUint16(offset + 12, true);
    const payloadCrc = view.getUint32(offset + 16, true);
    offset += 20;
    if (payloadLength === 0) continue;                       // still unreadable on the device
    if (offset + payloadLength > data.length) break;          // the repair stream was itself truncated
    const payload = data.subarray(offset, offset + payloadLength);
    offset += payloadLength;
    if (crc32(payload) === payloadCrc) frames.set(seq, data.subarray(start, offset));
  }
  return frames;
}

/** Extract `{seq: payload}` for every CRC-valid page in a retransmission response. */
export function extractPages(data: Uint8Array): Map<number, Uint8Array> {
  const pages = new Map<number, Uint8Array>();
  for (const [seq, frame] of extractPageFrames(data)) pages.set(seq, frame.subarray(20));
  return pages;
}

/**
 * Splice repaired pages into a stream, returning one that holds what was recovered.
 *
 * This is what makes a repair survive. Asking the device to resend a page and then writing a file that
 * still lacks it means the recovery lives only in whatever the tool happened to display, and re-reading
 * the saved log reports holes that were already fixed. The merged stream is the artefact of record.
 *
 * The merged stream is rebuilt from the pages that can be believed, in sequence order: every page that
 * arrived intact keeps its original bytes, a repair fills each page that did not, and a page the device
 * reported unreadable keeps its empty frame if nothing has replaced it, so a re-read still names it. A
 * frame whose CRC failed is dropped — its payload is unusable and its header, sequence number included,
 * sits in the same untrustworthy bytes — which is also what removes a phantom page a damaged transfer
 * wrote into the stream. With nothing new to merge the input is returned unchanged, so a clean download
 * is byte-for-byte what the tag sent.
 *
 * The stream header keeps the page total the DEVICE declared. It is the only record of how many pages
 * the log should hold, so a merge that still lacks some leaves a file that reads as incomplete and says
 * which pages are missing, rather than one that has quietly shrunk to fit what arrived.
 */
export function mergeRepairs(stream: Uint8Array, repairs: ReadonlyMap<number, Uint8Array>): Uint8Array {
  if (repairs.size === 0 || stream.length < 16 || detectFormat(stream) !== 2) return stream;
  const view = new DataView(stream.buffer, stream.byteOffset, stream.byteLength);
  const headerLength = 16 + view.getUint16(6, true);
  const totalPages = view.getUint32(8, true);
  const totalPayload = view.getUint32(12, true);

  const intact = new Map<number, Uint8Array>();
  const unreadable = new Map<number, Uint8Array>();
  let firstVerified: { position: number; seq: number } | null = null;
  let offset = headerLength;
  let position = 0;
  while (position < totalPages && offset + 20 <= stream.length) {
    const start = offset;
    const seq = view.getUint32(offset, true);
    const payloadLength = view.getUint16(offset + 12, true);
    const payloadCrc = view.getUint32(offset + 16, true);
    offset += 20;
    // A payload running past the end is the transfer stopping mid-page. One claiming more than a page
    // can physically hold is damaged outright, and stepping by it would desynchronise every page after.
    if (payloadLength && (offset + payloadLength > stream.length || payloadLength > NANDLOG_MAX_PAGE_SIZE_BYTES)) break;
    offset += payloadLength;
    position += 1;
    const frame = stream.subarray(start, offset);
    if (!payloadLength) {
      if (!unreadable.has(seq)) unreadable.set(seq, frame);
    } else if (crc32(frame.subarray(20)) === payloadCrc) {
      if (!intact.has(seq)) intact.set(seq, frame);
      firstVerified ??= { position: position - 1, seq };
    }
  }

  const frames = new Map(intact);
  for (const [seq, frame] of repairs) if (!intact.has(seq)) frames.set(seq, frame);
  if (frames.size === intact.size) return stream;

  // Only an empty frame whose number falls inside the declared range can be the device's own marker;
  // one outside it is a header read out of damaged bytes.
  if (firstVerified) {
    const [from, to] = expectedSeqRange(firstVerified, frames.keys(), totalPages);
    for (const [seq, frame] of unreadable) {
      if (seq >= from && seq < to && !frames.has(seq)) frames.set(seq, frame);
    }
  }

  const ordered = [...frames.keys()].sort((a, b) => a - b);
  const bodyLength = ordered.reduce((total, seq) => total + frames.get(seq)!.length, 0);
  const merged = new Uint8Array(headerLength + bodyLength);
  merged.set(stream.subarray(0, headerLength), 0);
  let at = headerLength;
  for (const seq of ordered) {
    merged.set(frames.get(seq)!, at);
    at += frames.get(seq)!.length;
  }

  // Grow the totals only if more real pages are held than the device declared — never shrink them.
  const mergedView = new DataView(merged.buffer, merged.byteOffset, merged.byteLength);
  mergedView.setUint32(8, Math.max(totalPages, frames.size), true);
  mergedView.setUint32(12, Math.max(totalPayload, bodyLength - frames.size * 20), true);
  return merged;
}

// --- Optional legacy shape ----------------------------------------------------------------------------

/**
 * Merge records into one row per timestamp, the shape the Python tool's `.pkl` files have.
 *
 * **This loses data, and only in one specific way.** A row is keyed by record kind, so a range, a
 * motion flag, a BLE scan and a voltage at the same instant coexist happily; what cannot coexist is
 * two records of the *same* kind at the same millisecond, where the later overwrites the earlier.
 * On the four-device 3.9-day corpus that is 6 to 36 records per file, all of them anchors and
 * charging events. It used to be far worse: at the old 500 ms timestamp quantum, two ranging rounds
 * could share a bucket, which cost 6 to 31 real measurements per device per night.
 *
 * Offered because downstream consumers of the `.pkl` expect this shape, not because it is right.
 * Prefer the record list.
 */
export function mergeByTimestamp(records: readonly LogRecord[]): Array<Record<string, unknown>> {
  const rows = new Map<number, Record<string, unknown>>();
  for (const record of records) {
    let row = rows.get(record.ms);
    if (!row) {
      row = { ms: record.ms };
      rows.set(record.ms, row);
    }
    switch (record.kind) {
      case 'voltage': row.v = record.millivolts; break;
      case 'charging': row.c = record.label; break;
      case 'motion': row.m = record.inMotion; break;
      case 'ranges': row.r = Object.fromEntries(record.ranges); break;
      case 'imu': row.i = record.accel; break;
      case 'bleScan': row.b = record.peers; break;
      case 'reset': row.rst = record.causes; break;
      case 'anchor': row.offset = record.offsetMs; break;
      case 'diagnostics': row.diag = record; break;
    }
  }
  return [...rows.values()].sort((a, b) => (a.ms as number) - (b.ms as number));
}

/**
 * Rough seconds to download `byteCount` over BLE, for a figure shown BEFORE a transfer starts.
 *
 * Once a transfer is running, use `ThroughputMeter` instead: it measures the link in front of it,
 * which an old laptop and a new one do not share. This exists only for the "this will take a while"
 * hint on the button, where there is nothing to measure yet, and is deliberately described as a
 * guess wherever it is shown.
 */
export function estimateBleDownloadSeconds(byteCount: number): number {
  return byteCount / guess('ble-download-throughput', 62980);
}
