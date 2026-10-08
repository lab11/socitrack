// Firmware facts, read from the committed snapshot in tools/constants.snapshot.json.
//
// Nothing in this file may be hand-edited to a different value than the snapshot holds. The snapshot
// is regenerated from firmware/src with `npm run drift:update`, and `drift.test.ts` fails if the
// firmware has moved since. `constants.test.ts` fails if this file exposes a firmware-derived value
// the snapshot does not contain.
//
// Values that are NOT firmware facts — anything estimated, chosen, or specified only in prose —
// belong in guesses.ts with a registry entry, not here.

import snapshot from '../tools/constants.snapshot.json' with { type: 'json' };
import { guess } from './guesses.ts';

export type BoardRevision = 'M' | 'N' | 'O' | 'P';

/** Board revisions in ascending order, matching the firmware's REVISION_ID comparisons. */
export const BOARD_REVISIONS = ['M', 'N', 'O', 'P'] as const;

export const REVISION_IDS = snapshot.revisionIds;

// --- Scalar constants ---------------------------------------------------------------------------

export const {
  /** Bytes in a device EUI, and in a BLE MAC address. */
  EUI_LEN,
  /** Max bytes in a per-device label. NUL padded, not NUL terminated. */
  EUI_NAME_MAX_LEN,
  /** Max devices in one deployment. */
  MAX_NUM_RANGING_DEVICES,
  /** Bytes per (peer, range) pair inside a RANGES record body: u8 uid + i16 millimetres. */
  COMPRESSED_RANGE_DATUM_LENGTH,
  /** Max RANGES record body: one count byte plus MAX_NUM_RANGING_DEVICES data. */
  MAX_COMPRESSED_RANGE_DATA_LENGTH,
  /** Max IMU record body, excluding the self-inclusive length byte. */
  MAX_IMU_DATA_LENGTH,

  /**
   * Period of the TimeAlignedTask loop, in seconds, and therefore of VOLTAGE and periodic
   * TIME_ANCHOR records.
   *
   * A loop iteration measures **299.38 s** of real time rather than 300: the FreeRTOS port computes
   * `ulTimerCountsForOneTick = 32768/100 = 327`, truncated from 327.68, so the tick runs 0.208%
   * fast. Anything checking record cadence must expect 299.38, not 300, or it will report a 0.2%
   * shortfall on a perfectly healthy device.
   */
  BATTERY_CHECK_INTERVAL_S,

  /**
   * Ranging round period, in microseconds. 500000 gives 2 Hz rounds.
   *
   * This is **not** the RANGES record rate. `handle_range_computation_phase()` stores a record only
   * when the round produced at least one range, so the observed record rate measures how many rounds
   * succeeded. A device logging 1 record/s is not ranging at 1 Hz; it is ranging at 2 Hz and failing
   * half its rounds.
   */
  SCHEDULING_INTERVAL_US,

  /** Seconds of silence before a peer is considered gone. */
  DEVICE_TIMEOUT_SECONDS,

  /**
   * Consecutive rounds yielding neither a detected device nor a range before the scheduler gives up
   * and raises APP_NOTIFY_NETWORK_LOST. Six rounds, so 3 seconds.
   *
   * This is the hinge of the scan/range oscillation: a link good enough for BLE discovery but
   * marginal for UWB crosses it repeatedly, and each crossing costs a full BLE scan stop/start.
   */
  MAX_EMPTY_ROUNDS_BEFORE_STATE_CHANGE,

  /**
   * Upper bound in seconds on how long a record may sit unflushed in the RAM page cache.
   *
   * This is the exact size of the data loss a hard reset costs, which makes it the yardstick for
   * reading a gap: silence shorter than this across a reboot is the expected cache loss, and silence
   * longer than this means the device genuinely stopped executing.
   */
  STORAGE_FLUSH_TIMEOUT_S,

  /**
   * An offset move at least this large (ms) writes an audit TIME_ANCHOR before the new base is
   * adopted. A host reporting backward page bounds should use the same threshold to separate a
   * routine clock adjustment from a re-basing worth warning about.
   */
  TIME_BASE_CHANGE_THRESHOLD_MS,
  /**
   * Minimum spacing between accepted charger edges.
   *
   * A state change arriving inside the window is deferred and counted into
   * `charger_suppressed_edges`, not dropped. This is the floor on how fast genuine CHARGING_EVENT
   * records can legitimately arrive, which is what makes a storm diagnosable rather than merely
   * surprising.
   */
  BATTERY_EVENT_DEBOUNCE_MS,
  /**
   * A ranging round with no phase transition for this long restarts the schedule phase.
   *
   * Bounds how long a wedged radio can suppress RANGES records before recovering unaided — the
   * thing that separates a radio stall from a device outage when reading a gap.
   */
  RANGING_ROUND_STALL_TIMEOUT_MS,

  /**
   * Nominal seconds per watchdog tick at AM_HAL_WDT_1_16HZ.
   *
   * Nominal only. The LFRC has no specified minimum or maximum and measures ~20.94 s per tick on
   * revP, 24% slow, so every window derived from this understates the real one. Use
   * WATCHDOG_MEASURED_TICK_S for anything quantitative.
   */
  WATCHDOG_TICK_S,
  /** Ticks from the last successful pet to the pre-reset interrupt. */
  WATCHDOG_INTERRUPT_TICKS,
  /** Ticks from the last successful pet to the hardware reset. */
  WATCHDOG_RESET_TICKS,
  /** How often each monitored task checks in, in milliseconds. */
  WATCHDOG_CHECKIN_INTERVAL_MS,
  /** A task later than this (ms) is stalled: the pet is declined and the task is named. */
  WATCHDOG_CHECKIN_DEADLINE_MS,
  /** Milliseconds after arming during which the watchdog is petted unconditionally. */
  WATCHDOG_STARTUP_GRACE_MS,

  /**
   * Lower bound applied to a raw two-way-TOF sample before it enters the median.
   *
   * This never reaches a log file: compute_ranges clamps the resulting median to >= 0, so a stored
   * range is always non-negative even though it is encoded as int16.
   */
  MIN_VALID_RANGE_MM,
  /**
   * Upper bound on a stored range.
   *
   * The Python tool used 16000 here, silently discarding every range between 16 m and 32 m that the
   * firmware considered valid.
   */
  MAX_VALID_RANGE_MM,

  /** RAM sizing budget for one page. A fitted part with a larger page is a compile error. */
  NANDLOG_MAX_PAGE_SIZE_BYTES,
  /** RAM sizing budget for one spare area. */
  NANDLOG_MAX_SPARE_SIZE_BYTES,
  /** Blocks kept erased ahead of the write head. */
  NANDLOG_ERASE_AHEAD_BLOCKS,
  /** Program or erase attempts on one block before it is retired as bad. */
  NANDLOG_BLOCK_ERRORS_BEFORE_REMOVAL,
  /** Blocks one page write may relocate through before the log declares the part unwritable. */
  NANDLOG_PAGE_PLACEMENT_ATTEMPTS,
  /** Largest caller-defined metadata blob. experiment_details_t must fit inside this. */
  NANDLOG_MAX_EPOCH_DETAILS_BYTES,
  /**
   * A backward timestamp step no larger than this (ms) stays in the page, keeping its own timestamp,
   * instead of committing the page. Page bounds are the earliest and latest record, so neighbouring
   * pages may overlap by up to this much, which readers do not report as a time discontinuity.
   *
   * 250 ms is chosen to separate writer disagreement — a ranging record carrying the master's
   * broadcast time next to one carrying the device's own clock read, which differ by 10-40 ms —
   * from a genuinely moved time base.
   */
  NANDLOG_TIMESTAMP_TOLERANCE_MS,
  /**
   * Whether each record carries its own length.
   *
   * **1 as of the diagnostics record.** Pages carry NANDLOG_PAGE_MAGIC_FRAMED ('TTP2'), every record
   * is prefixed with a uint16 length, and a reader can step over a type it has never heard of
   * instead of losing the rest of the page. It was turned on precisely because adding
   * STORAGE_TYPE_DIAGNOSTICS is the change framing exists to make safe: under the unframed format
   * every reader had to learn a new type in the same commit, and this package's failure to do that
   * for types 7 and 8 is what left it silently unable to decode past page 0.
   */
  NANDLOG_RECORD_FRAMING,
  /** Bytes in the per-record length prefix, when framing is enabled. */
  NANDLOG_FRAMING_LENGTH_BYTES,
  /** WSF buffer pools reported in a diagnostics record. Fixed at the five ble_task.c declares. */
  STORAGE_DIAGNOSTIC_NUM_POOLS,
  /** Stack headroom entries in a diagnostics record: each watchdog task in order, then the timer service. */
  STORAGE_DIAGNOSTIC_NUM_STACKS,
  /** Diagnostics status bit: this chip's factory trims support TempCo. */
  STORAGE_DIAGNOSTIC_FLAG_TEMPCO_AVAILABLE,
  /** Diagnostics status bit: the last temperature reading adjusted the voltage trims. */
  STORAGE_DIAGNOSTIC_FLAG_TEMPCO_APPLIED,
  /** Diagnostics status bit: built with uncommitted firmware changes, so the revision alone does not identify it. */
  STORAGE_DIAGNOSTIC_FLAG_FIRMWARE_MODIFIED,
  /** Stack headroom value for a task that is not running in the current mode. */
  STORAGE_DIAGNOSTIC_STACK_UNMONITORED,
  /** Diagnostics status bit: a diagnostic build, which logs every late radio arm and times radio interrupts. */
  STORAGE_DIAGNOSTIC_FLAG_DIAGNOSTIC_BUILD,
  /** Diagnostics status bit: built with TempCo switched off, whatever the chip supports. */
  STORAGE_DIAGNOSTIC_FLAG_TEMPCO_DISABLED,
  /** Radio abort phase: a ranging-slot receive, whose failure abandons the round. */
  STORAGE_RADIO_ABORT_PHASE_RANGING,
  /** Radio abort phase: a status-slot receive, whose failure only ends the status exchange early. */
  STORAGE_RADIO_ABORT_PHASE_STATUS,
  /** Radio abort value for a field the build could not measure. */
  STORAGE_RADIO_ABORT_UNMEASURED,
  /** Radio abort trigger: not inside a radio interrupt, or before it had handled an event. */
  STORAGE_RADIO_ABORT_TRIGGER_UNKNOWN,
  /** Radio abort trigger: a frame this device sent. */
  STORAGE_RADIO_ABORT_TRIGGER_TX_DONE,
  /** Radio abort trigger: a frame this device received. */
  STORAGE_RADIO_ABORT_TRIGGER_RX_FRAME,
  /** Radio abort trigger: a receive window that closed empty. */
  STORAGE_RADIO_ABORT_TRIGGER_RX_TIMEOUT,
  /** Radio abort trigger: a frame that could not be decoded. */
  STORAGE_RADIO_ABORT_TRIGGER_RX_ERROR,
  /** Radio abort event-to-interrupt time when the event has no radio timestamp or the interrupt start is unknown. */
  STORAGE_RADIO_ABORT_NO_EVENT_TIME,
  /** Schedule catch first_copy when the network was lost before any copy was decoded. */
  STORAGE_SCHEDULE_CATCH_NONE,
  /** Schedule catch rounds_missed when the schedule timestamps went backwards. */
  STORAGE_SCHEDULE_CATCH_UNKNOWN,
  /** Schedule catch and round start value for a time not measured; as wake_us, the radio had to be reset. */
  STORAGE_SCHEDULE_CATCH_UNMEASURED,
  /** Round start bit: the master's second schedule copy could not be armed in time. */
  STORAGE_ROUND_START_FLAG_SECOND_COPY_FAILED,
  /** Round start bit: the round reached the computation phase. */
  STORAGE_ROUND_START_FLAG_COMPUTED,
  /** Round start bit: the round ended early on a radio or ranging error. */
  STORAGE_ROUND_START_FLAG_ABANDONED,
  /** Round start bit: the master's join window heard a request directly. */
  STORAGE_ROUND_START_FLAG_JOIN_HEARD,
  /** Round start bit: a join request reached the master relayed through the status exchange. */
  STORAGE_ROUND_START_FLAG_JOIN_RELAYED,
  /** Session end reason: nothing recorded why. */
  STORAGE_SESSION_END_UNKNOWN,
  /** Session end reason: the application stopped the scheduler, as it does on finding a higher-ID master. */
  STORAGE_SESSION_END_STOPPED,
  /** Session end reason: no round completed within the network search time. */
  STORAGE_SESSION_END_SEARCH_TIMEOUT,
  /** Session end reason: a frame of an unexpected type arrived mid-round. */
  STORAGE_SESSION_END_COLLISION,
  /** Session end reason: as master, heard nobody for several rounds running. */
  STORAGE_SESSION_END_SILENT,
  /** Radio timing record: bands in its frame-to-interrupt histogram. */
  STORAGE_RADIO_TIMING_BANDS,
  /** Radio timing record: upper edge of the first frame-to-interrupt band, µs. */
  STORAGE_RADIO_TIMING_FIRST_US,
  /** Radio timing record: width of each band after the first, µs. */
  STORAGE_RADIO_TIMING_STEP_US,
  /** Radio timing record: how often a diagnostic build writes one, ms. */
  STORAGE_RADIO_TIMING_INTERVAL_MS,
  /** Milliseconds the chip may hold BUSY before the driver declares it dead and resets. */
  NANDLOG_BUSY_TIMEOUT_MS,
  /** Most pages one retransmission round may request. */
  NANDLOG_MAX_RETRANSMIT_PAGES,

  /** Sentinel in first_timestamp/last_timestamp for a page holding no timestamped record. */
  NANDLOG_NO_TIMESTAMP,

  /** USB vendor ID, for the Web Serial device filter. */
  USB_VID,
  /** USB product ID, for the Web Serial device filter. */
  USB_PID,
  /** Requested ATT MTU. Log chunks are sent as MTU-3 byte notifications. */
  BLE_DESIRED_MTU,
  /** In 1.25 ms units. Bulk download is one notification per connection interval. */
  BLE_MIN_CONNECTION_INTERVAL_1_25_MS,
  /** In 1.25 ms units. */
  BLE_MAX_CONNECTION_INTERVAL_1_25_MS,
  MAX_NUM_CONNECTIONS,
  /** In 1.25 ms units. Requested once an offload starts, so this sets download throughput. */
  BLE_FAST_CONNECTION_INTERVAL_1_25_MS,
  /** Connection events the peripheral may skip when idle. Irrelevant mid-offload. */
  BLE_CONNECTION_SLAVE_LATENCY,
  /**
   * In 10 ms units. A link with no successful exchange for this long drops.
   *
   * The transport must keep any host-side stall under this, and treat a drop as resumable via
   * DOWNLOAD_LOG_CONTINUE rather than as a failed transfer.
   */
  BLE_SUPERVISION_TIMEOUT_10_MS,
  BLE_MAINTENANCE_NEW_EXPERIMENT,
  BLE_MAINTENANCE_DELETE_EXPERIMENT,
  BLE_MAINTENANCE_DOWNLOAD_LOG,
  BLE_MAINTENANCE_SET_LOG_DOWNLOAD_DATES,
  BLE_MAINTENANCE_DOWNLOAD_LOG_CONTINUE,
  BLE_MAINTENANCE_PACKET_COMPLETE,
  /** Count byte then that many u32 sequence numbers, accumulated across writes. */
  BLE_MAINTENANCE_RETRANSMIT_PAGES,
  /** Start a radio test: u32 start and end Unix seconds, a count byte, then that many 6-byte EUIs. */
  BLE_MAINTENANCE_START_RADIO_TEST,
  /** End a radio test early; the device restarts into whatever it would otherwise be doing. */
  BLE_MAINTENANCE_STOP_RADIO_TEST,
  /** Bytes in a START_RADIO_TEST write before its EUIs. */
  BLE_MAINTENANCE_RADIO_TEST_HEADER_LEN,
  /** Longest radio test a device will accept, seconds. */
  RADIO_TEST_MAX_SECONDS,
  /** How long a device that lost its test's device list across the restart waits to be sent it again, seconds. */
  RADIO_TEST_LIST_WAIT_S,
  /** Antennas each device cycles through in a ranging round. */
  NUM_XMIT_ANTENNAS,
  /** Layout version opening the live radio statistics. */
  BLE_RADIO_STATS_VERSION,
  /** Live radio statistics bit: this boot is a radio test. */
  BLE_RADIO_STATS_FLAG_TEST_RUNNING,
  /** Live radio statistics bit: a radio test waiting to be sent its device list again. */
  BLE_RADIO_STATS_FLAG_TEST_WAITING,
  /**
   * Sequence numbers the firmware reads from ONE retransmit write.
   *
   * It applies `MIN(count, this)` and silently ignores the excess, so a host that sends more loses
   * the remainder with no error returned. Chunk at exactly this size.
   */
  BLE_MAINTENANCE_MAX_SEQS_PER_WRITE,
} = snapshot.defines;

/**
 * Measured seconds per watchdog tick on revP, against a nominal WATCHDOG_TICK_S of 16.
 *
 * Two independent measurements agreed to 1.6% (20.94 s and 20.60 s) and both put the LFRC ~24% slow.
 * This is a measurement of one board family, not a specification — the datasheet gives the LFRC no
 * tolerance at all — so it is exported for interpretation, never for configuration.
 */
export const WATCHDOG_MEASURED_TICK_S = guess('watchdog-tick-rate', 20.94);

/**
 * Seconds a stall can persist before the hardware resets the device.
 *
 * The single most useful number for reading a gap in a log: silence longer than
 * STORAGE_FLUSH_TIMEOUT_S but comparable to this is a hang the watchdog caught.
 */
export const WATCHDOG_RESET_WINDOW_S = WATCHDOG_RESET_TICKS * WATCHDOG_MEASURED_TICK_S;

/**
 * Real seconds in one TimeAlignedTask iteration, accounting for the truncated tick divisor.
 *
 * Measured across 1112-1134 anchor intervals per device on four devices: 299.369, 299.382, 299.376,
 * 299.380 s. Cadence checks must use this rather than BATTERY_CHECK_INTERVAL_S.
 */
export const TIME_ALIGNED_INTERVAL_S = 299.38;

// --- Per-chip flash geometry ----------------------------------------------------------------------

/** SPI NAND parts the log supports. `nandlog` identifies the fitted part at runtime by JEDEC ID. */
export type ChipKey = 'W25N01GWZEIG' | 'AS5F18G04SND';

/**
 * Flash geometry, which belongs to the fitted part rather than to the board revision.
 *
 * `PAYLOAD_BYTES_PER_PAGE` is the page size minus the 32-byte nandlog page header, derived in the
 * extractor from the measured struct so it cannot drift away from the header it describes.
 * `LOG_CAPACITY_BYTES` further excludes the bad-block reserve and the 8-block metadata ring.
 *
 * A .ttg file records neither the revision nor the part, so anything selected from here needs the
 * part supplied from outside the file — but a stream's own page sizes identify it in practice, since
 * the two parts differ by a factor of two.
 */
export const PER_CHIP = snapshot.perChip;

// --- Enumerations ---------------------------------------------------------------------------------

/**
 * The type byte that opens every log record.
 *
 * Types 7 (RESET_REASON) and 8 (TIME_ANCHOR) were added after the first version of this package was
 * written, and both are emitted at every boot — so a reader that does not know them cannot get past
 * page 0 of any current file. STORAGE_NUM_TYPES bounds the validator's range check.
 */
export const STORAGE_TYPE = snapshot.enums.storage_data_type_t.members;

/** MOTION record body. Just the two motion states; the former charger codes were removed. */
export const MOTION_CODE = snapshot.enums.motion_code_t.members;

/**
 * CHARGING_EVENT record body.
 *
 * 1..4 match the Python tool's BATTERY_CODES mapping. 5 (BATTERY_CRITICAL_VOLTAGE) is delivered only
 * by the VCOMP brownout path, which is gated off on every current board.
 */
export const BATTERY_EVENT = snapshot.enums.battery_event_t.members;

/** Advertised ranging role, the third byte of the BLE manufacturer data, and the role in a SESSION_END record. */
export const SCHEDULE_ROLE = snapshot.enums.schedule_role_t.members;

/** Ranging protocol phase, which reaches the log only as where a SESSION_END record's colliding frame arrived. */
export const SCHEDULER_PHASE = snapshot.enums.scheduler_phase_t.members;

/** Message type byte opening every ranging-protocol frame, as logged for a SESSION_END record's colliding frame. */
export const PACKET_TYPE = snapshot.enums.packet_t.members;

/** USB CDC command bytes. Several alias the BLE_MAINTENANCE_* opcodes. */
export const USB_COMMAND = snapshot.enums.usb_command_t.members;

/** Bitmask of enabled IMU outputs. Only IMU_ACCELEROMETER reaches storage in the default build. */
export const IMU_DATA_TYPE = snapshot.enums.imu_data_type_t.members;

/**
 * The five tasks that must all check in for the watchdog to be petted.
 *
 * Ordering is load-bearing: the per-task stall codes in RESET_DIAGNOSTIC are this enum offset by
 * RESET_DIAGNOSTIC_STALL_TIME_ALIGNED. The firmware asserts that statically and the extractor
 * re-checks it, because a reordering would silently rename every stall in every archived log.
 */
export const WATCHDOG_TASK = snapshot.enums.watchdog_task_t.members;

/**
 * Task names in `watchdog_task_t` order, mirroring `watchdog_task_names[]` in system.c.
 *
 * The spelling is transcribed rather than derived: mechanically title-casing the enum members gives
 * `RANGINGTask` and `BLE` cannot be recovered from `Ble`, so a derivation would be uglier and no safer.
 * What matters is the ORDER, because a diagnostics record's late-episode array is indexed by it and a
 * reordering would silently rename every entry in every archived log — so `constants.test.ts` pins the
 * length of this list against WATCHDOG_NUM_TASKS, which makes adding a task a build failure here rather
 * than a mislabelled counter in a log nobody re-reads.
 */
export const WATCHDOG_TASK_NAMES: readonly string[] =
  ['TimeAlignedTask', 'StorageTask', 'AppTask', 'BLETask', 'RangingTask'];

/**
 * The firmware's own verdict on what stopped it, carried in the top four bits of a RESET_REASON
 * record's status word.
 *
 * RESET_DIAGNOSTIC_NOTHING_RECORDED is re-armed at every boot rather than cleared, so reading it
 * back after a reset proves two things at once: the scratch register survived, and no handler wrote
 * to it. On a watchdog reset that combination is itself the diagnosis — nothing declined a pet,
 * which means no monitored task was running to decline one.
 */
export const RESET_DIAGNOSTIC = snapshot.enums.reset_diagnostic_t.members;

// --- Structures -----------------------------------------------------------------------------------

/**
 * Layout of experiment_details_t: the packed struct exchanged over BLE and USB to configure a
 * deployment, stored verbatim in the log metadata page, and echoed in the offload stream header.
 * 239 bytes.
 */
export const EXPERIMENT_DETAILS_LAYOUT = snapshot.structs.experiment_details_t;

/** On-flash page header, 32 bytes. Its size is what makes payload capacity 4064 on a 4096-byte part. */
export const PAGE_HEADER_LAYOUT = snapshot.structs.nandlog_page_header_t;

/** Metadata ring entry header, 32 bytes, followed by details_length bytes of experiment details. */
export const META_HEADER_LAYOUT = snapshot.structs.nandlog_meta_header_t;

/**
 * Payload of a STORAGE_TYPE_DIAGNOSTICS record, 69 bytes.
 *
 * Counters for conditions the firmware recovered from, written once per TimeAlignedTask loop, then
 * the firmware revision, TempCo state and chip temperature, radio health, dropped records, stack
 * headroom and recoveries. Receive counts per antenna are read live over Bluetooth, not logged. Every counter was previously reachable only from a console or a debugger,
 * which made it useless in a deployment. Cumulative since boot and saturating, so a reboot partitions
 * them and a pegged value still reads as bad rather than wrapping to zero.
 */
export const DIAGNOSTICS_LAYOUT = snapshot.structs.storage_diagnostics_t;

/**
 * Payload of a STORAGE_TYPE_RADIO_ABORT record, 19 bytes, written only by a diagnostic build: one radio
 * receive that could not be armed before its slot, with how late it was, what the radio interrupt had been
 * doing, which radio event it was handling, when it started relative to that event, and whether it had to wake
 * the processor first.
 */
export const RADIO_ABORT_LAYOUT = snapshot.structs.storage_radio_abort_t;

/**
 * Payload of a STORAGE_TYPE_SCHEDULE_CATCH record, written only by a diagnostic build: one participant
 * wake-up from its timer, from the receiver opening to the first schedule copy decoded.
 */
export const SCHEDULE_CATCH_LAYOUT = snapshot.structs.storage_schedule_catch_t;

/**
 * Payload of a STORAGE_TYPE_ROUND_START record, written only by a diagnostic build as the round ends: how
 * long the master took from its wake-up timer to the first schedule copy, and how that round went.
 */
export const ROUND_START_LAYOUT = snapshot.structs.storage_round_start_t;

/**
 * Payload of a STORAGE_TYPE_SESSION_END record, written only by a diagnostic build: why one run of the
 * ranging scheduler ended, and what it saw while it lasted.
 */
export const SESSION_END_LAYOUT = snapshot.structs.storage_session_end_t;

/**
 * Payload of a STORAGE_TYPE_RADIO_TIMING record, written once a minute only by a diagnostic build: how close
 * that minute's receives came to their deadlines.
 */
export const RADIO_TIMING_LAYOUT = snapshot.structs.storage_radio_timing_t;

/**
 * Value of BLE_LIVE_STATS_RADIO_CHAR: a device's radio counters since it booted, read live over Bluetooth, with its
 * role, schedule size and any radio test's state. Versioned by its first byte.
 */
export const RADIO_STATS_LAYOUT = snapshot.structs.ble_radio_stats_t;

/** Display names for the stack headroom entries, in record order. */
export const DIAGNOSTICS_STACK_NAMES: readonly string[] = [...WATCHDOG_TASK_NAMES, 'TimerService'];

/** First 16 bytes of an offload stream. */
export const STREAM_HEADER_LAYOUT = snapshot.structs.nandlog_stream_header_t;

/** Per-page frame in an offload stream, 20 bytes, followed by payload_length payload bytes. */
export const WIRE_PAGE_LAYOUT = snapshot.structs.nandlog_wire_page_t;

// --- Bluetooth ------------------------------------------------------------------------------------

/**
 * 128-bit BLE UUIDs in canonical text form.
 *
 * `implemented: false` marks a UUID that the firmware defines but does not back with a
 * characteristic. Reading BLE_LIVE_STATS_ADDRESS_CHAR returns an ATT error, and writes to
 * BLE_MODE_SWITCH_CHAR are silently discarded unless the firmware was built with
 * _REMOTE_MODE_SWITCH_ENABLED, which no shipping build sets.
 */
export const BLE_UUID = snapshot.uuids;

/**
 * GATT System ID (0x2A23), from the standard Device Information Service.
 *
 * This is where a browser must get device identity from. Web Bluetooth cannot see MAC addresses —
 * BluetoothDevice.id is an opaque per-origin string — so the Python tool's approach of splitting
 * the BLE address does not port. bluetooth_init packs the EUI as
 * [uid[0], uid[1], uid[2], 0xFE, 0xFF, uid[3], uid[4], uid[5]].
 */
export const GATT_SYSTEM_ID_UUID = '00002a23-0000-1000-8000-00805f9b34fb';
export const GATT_FIRMWARE_REVISION_UUID = '00002a26-0000-1000-8000-00805f9b34fb';
export const GATT_HARDWARE_REVISION_UUID = '00002a27-0000-1000-8000-00805f9b34fb';

/** Byte offsets of the EUI within the 8-byte System ID value. */
export const SYSTEM_ID_EUI_OFFSETS = [0, 1, 2, 5, 6, 7] as const;

// --- IMU scale factors ----------------------------------------------------------------------------

/**
 * Fixed-point scale factors from the BNO08x driver used on revN/O/P.
 *
 * SCALE_Q8 is the one that reaches storage. revM uses a BNO055 with UNIT_SEL=0 instead, whose
 * accelerometer LSB is 1/100 m/s^2 rather than 1/256 — a factor of 2.56 that no log file records.
 */
export const SCALE_FACTOR = snapshot.scaleFactors;

/** revM accelerometer LSB in m/s^2. From the BNO055 UNIT_SEL=0 setting, not a Q-format constant. */
export const BNO055_ACCEL_SCALE = 0.01;

// --- v1 log format ---------------------------------------------------------------------------------

/** Page header of a v1 data page: 'D', 'A', then a uint16 payload length. */
export const V1_PAGE_MAGIC = 'DA';
export const V1_PAGE_HEADER_BYTES = 4;

/** Metadata page magic in the v1 layout. */
export const V1_METADATA_MAGIC = 'META';

/** Bytes preceding every record body: one type byte plus a uint32 timestamp. */
export const RECORD_HEADER_BYTES = 5;

/**
 * Granularity that v1 log timestamps were floored to.
 *
 * **v1 only.** The `500 * (t / 500)` snap was removed from all ten write sites; current firmware
 * stores the RTC's own 10 ms resolution. The Python parser used `timestamp % 500 == 0` as its
 * primary resynchronisation signal, which would reject every record written today — that check is
 * now conditional on the v1 path.
 */
export const TIMESTAMP_QUANTUM_MS = 500;

/** Real timestamp granularity in current firmware: the RTC counts `1000*seconds + 10*hundredths`. */
export const RTC_RESOLUTION_MS = 10;

/**
 * Experiment-relative timestamps are uint32 milliseconds, so they wrap after this many seconds.
 *
 * 4294967.295 s is 49.7 days. This is the raw type ceiling; the usable ceiling is lower, because
 * the firmware adds a sub-second term on top of it — see MAX_EXPERIMENT_ELAPSED_SECONDS.
 */
export const TIMESTAMP_WRAP_SECONDS = 0xffffffff / 1000;

/**
 * Hundredths of a second reported by the Apollo4 RTC, and the milliseconds each is worth.
 *
 * `rtc_get_timestamp_diff_ms()` returns `1000 * (now - start) + 10 * hundredths`, so the sub-second
 * term contributes up to 990 ms on top of the whole-second product. Ignoring it is how you arrive
 * at a limit that is one second too generous and wraps in the field rather than in a test.
 */
const RTC_MAX_HUNDREDTHS = 99;
const RTC_MS_PER_HUNDREDTH = 10;

/**
 * The largest elapsed experiment time, in seconds, for which every stored timestamp is
 * representable — including the sub-second term, and staying strictly below NANDLOG_NO_TIMESTAMP,
 * which a real timestamp reaching it would be misread as.
 *
 * Measured 2026-09-11 at 4294966 s (49.71025 days) by compiling the exact firmware arithmetic, and
 * asserted against that figure in constants.test.ts. Derived here rather than hardcoded so that a
 * change to the sentinel moves it automatically.
 */
export const MAX_EXPERIMENT_ELAPSED_SECONDS = Math.floor(
  (NANDLOG_NO_TIMESTAMP - 1 - RTC_MAX_HUNDREDTHS * RTC_MS_PER_HUNDREDTH) / 1000,
);

/**
 * The deployment-duration limit to offer a user, in whole days.
 *
 * 49 days rather than 49.71: a duration control denominated in days should not have a fractional
 * maximum nobody can express. This REPLACES the 21-day limit the Python GUI enforced, which was
 * believed to be this constraint and was not — it left 28.71 days of headroom, and no check for it
 * existed anywhere in the firmware.
 */
export const MAX_DEPLOYMENT_DAYS = Math.floor(MAX_EXPERIMENT_ELAPSED_SECONDS / 86400);
export const MAX_DEPLOYMENT_SECONDS = MAX_DEPLOYMENT_DAYS * 86400;

// --- v2 log format ------------------------------------------------------------------------------

const asciiFromLittleEndian = (value: number): string =>
  String.fromCharCode(value & 0xff, (value >>> 8) & 0xff, (value >>> 16) & 0xff, (value >>> 24) & 0xff);

/** `'TTS1'`. The first four bytes of an offload stream, and the v1/v2 discriminator. */
export const V2_STREAM_MAGIC = asciiFromLittleEndian(snapshot.defines.NANDLOG_STREAM_MAGIC);
/** `'TTP1'`. A data page whose records are opaque to the log. */
export const V2_DATA_PAGE_MAGIC = asciiFromLittleEndian(snapshot.defines.NANDLOG_PAGE_MAGIC);
/** `'TTP2'`. A data page whose records each carry their own length. */
export const V2_DATA_PAGE_MAGIC_FRAMED = asciiFromLittleEndian(snapshot.defines.NANDLOG_PAGE_MAGIC_FRAMED);
/** `'TTM1'`. A metadata page in the epoch ring. */
export const V2_METADATA_PAGE_MAGIC = asciiFromLittleEndian(snapshot.defines.NANDLOG_META_MAGIC);

/** Stream format version: 1 when records are unframed, 2 when each carries its own length. */
export const V2_FORMAT_VERSION_UNFRAMED = 1;
export const V2_FORMAT_VERSION_FRAMED = 2;

/** The version this firmware build emits, from NANDLOG_RECORD_FRAMING. */
export const V2_FORMAT_VERSION_THIS_BUILD = snapshot.defines.NANDLOG_RECORD_FRAMING
  ? V2_FORMAT_VERSION_FRAMED
  : V2_FORMAT_VERSION_UNFRAMED;

/** Blocks reserved for the metadata ring: 8 blocks = 512 experiments before it wraps. */
export const V2_METADATA_RING_BLOCKS = 8;

/**
 * Attempts allowed for ONE page before the host gives up on it. Retries are counted per page rather
 * than per round: a round that recovers most of what it asked for is a bad but serviceable link and
 * deserves another go, whereas a page asked for repeatedly that never arrives is the only thing that
 * actually indicates a problem.
 */
export const V2_RETRANSMIT_PAGE_ATTEMPTS = 6;

/**
 * Loop guard only, not the give-up policy -- V2_RETRANSMIT_PAGE_ATTEMPTS decides that. A repair round
 * can carry at most NANDLOG_MAX_RETRANSMIT_PAGES pages, so a badly holed transfer legitimately needs
 * several rounds just to name everything it is missing.
 */
export const V2_RETRANSMIT_RETRY_ROUNDS = 40;

// --- Reset status bits ----------------------------------------------------------------------------

/**
 * Raw `am_hal_reset_status_e` bits latched by RSTGEN, carried in the low 12 bits of a RESET_REASON
 * record. Several can be set at once, which is why the firmware stores the raw word rather than
 * choosing a winner.
 */
export const RESET_STATUS_BITS: ReadonlyArray<readonly [number, string]> = [
  [0x001, 'External'],
  [0x002, 'Power-On'],
  [0x004, 'Brown-Out'],
  [0x008, 'SW Power-On'],
  [0x010, 'SW Power-On Init'],
  [0x020, 'Debugger'],
  [0x040, 'Watchdog'],
  [0x080, 'Unregulated Supply Brownout'],
  [0x100, 'Core Regulator Brownout'],
  [0x200, 'Memory Regulator Brownout'],
  [0x400, 'High-Power Memory Regulator Brownout'],
  [0x800, 'Low-Power Core Regulator Brownout'],
];

export const RESET_STATUS_MASK = 0xfff;
export const RESET_WATCHDOG_BIT = 0x040;

/** The firmware's diagnostic occupies the four bits above the hardware status word. */
export const RESET_DIAGNOSTIC_SHIFT = 12;
export const RESET_DIAGNOSTIC_MASK = 0xf;
