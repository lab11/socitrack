// Declaration of every fact this package takes from the firmware, and where that fact lives.
//
// This file is the contract between `managementweb` and `firmware/src`. Adding a constant to the
// schema package without adding it here is caught by `constants.test.ts`; the firmware changing a
// value listed here is caught by `drift.test.ts`; the firmware *renaming or deleting* something
// listed here is caught by the extractor failing to find it, which is a hard error rather than a
// silently smaller result set.
//
// The last of those three is the one that matters most. A checker that quietly stops checking is
// worse than no checker, because it reports success.
//
// ---------------------------------------------------------------------------------------------
// 2026-09-01: retargeted after the `nandlog` extraction (Storage_Redesign.md §14).
//
// The entire `MEMORY_*` family and the `peripherals/include/storage.h` header it lived in no longer
// exist; flash geometry now belongs to whichever chip driver is compiled in, and log-format policy
// lives in `external/nandlog/nandlog_conf.h`. The EVB board revision was removed from
// `boards/revisions.h` too, which is what made `npm run drift:update` fail outright rather than
// merely go stale. Both are fixed here. The lesson, recorded because it is the failure this file
// exists to prevent: the drift checker was broken and stale *at the same time*, so the breakage hid
// the staleness. A broken extractor must be treated as a red build, not as a chore to defer.
// ---------------------------------------------------------------------------------------------

/** Firmware source root, relative to the repository's `software/` directory. */
export const FIRMWARE_ROOT = 'firmware/src';

/**
 * Board revisions the firmware supports, and the revision ordering used by its `#if REVISION_ID <
 * REVISION_N` guards. Sourced from boards/revisions.h; the extractor verifies these IDs still match.
 *
 * EVB was removed from the firmware and is therefore removed here. It is *not* kept as a historical
 * entry: this list drives extraction, so a revision in it that the firmware does not define is a
 * hard failure, which is the correct behaviour and is exactly what happened.
 */
export const REVISIONS = ['M', 'N', 'O', 'P'];

/**
 * SPI NAND parts the log supports, and the driver that owns each one's geometry.
 *
 * This replaces the former PER_REVISION flash geometry. Geometry is a property of the fitted part,
 * not of the board revision, and `nandlog` now identifies the part at runtime by its JEDEC ID
 * (Storage_Redesign.md §14.5). A .ttg file records neither the revision nor the part, so anything
 * selected from here still needs to come from outside the file — but the set of possibilities is
 * now two parts rather than five revisions.
 */
export const CHIPS = [
  { key: 'W25N01GWZEIG', file: 'external/nandlog/chips/nandlog_chip_W25N01GWZEIG.c', note: 'Winbond, 2048-byte page, 1024 blocks = 128 MiB.' },
  { key: 'AS5F18G04SND', file: 'external/nandlog/chips/nandlog_chip_AS5F18G04SND.c', note: 'Alliance Memory, 4096-byte page, 4096 blocks = 1 GiB. Fitted on revP.' },
];

/** Per-chip `#define`s, extracted once per entry in CHIPS from that entry's driver. */
export const PER_CHIP_DEFINES = [
  { name: 'NANDLOG_CHIP_PAGE_SIZE_BYTES', note: 'Data area of one page, excluding spare.' },
  { name: 'NANDLOG_CHIP_SPARE_SIZE_BYTES', note: 'Spare/OOB area, used for ECC and bad-block marking. Never part of a payload.' },
  { name: 'NANDLOG_CHIP_PAGES_PER_BLOCK', note: 'Erase-block size in pages. Asserted a power of two by the chip driver.' },
  { name: 'NANDLOG_CHIP_BLOCK_COUNT', note: 'Total blocks on the part.' },
  { name: 'NANDLOG_CHIP_RESERVED_BLOCKS', note: 'Blocks held back for bad-block replacement, at the top of the array.' },
];

/**
 * Simple `#define NAME VALUE` constants. `file` is relative to FIRMWARE_ROOT.
 *
 * `note` is carried through into the generated TypeScript so the reason a constant matters is
 * visible at the point of use rather than only here.
 */
export const DEFINES = [
  // --- Identity and capacity -------------------------------------------------------------------
  { name: 'EUI_LEN', file: 'app/app_config.h', note: 'Bytes in a device EUI. Also the BLE MAC length.' },
  { name: 'EUI_NAME_MAX_LEN', file: 'app/app_config.h', note: 'Max bytes in a per-device label, NUL padded, not NUL terminated.' },
  { name: 'MAX_NUM_RANGING_DEVICES', file: 'app/app_config.h', note: 'Max devices in one deployment.' },
  { name: 'COMPRESSED_RANGE_DATUM_LENGTH', file: 'app/app_config.h', note: 'Bytes per (peer, range) pair in a RANGES record: u8 uid + i16 mm.' },
  { name: 'MAX_COMPRESSED_RANGE_DATA_LENGTH', file: 'app/app_config.h', note: 'Max RANGES record body: count byte + MAX_NUM_RANGING_DEVICES data.' },
  { name: 'MAX_IMU_DATA_LENGTH', file: 'app/app_config.h', note: 'Max IMU record body excluding the self-inclusive length byte.' },

  // --- Timing ----------------------------------------------------------------------------------
  { name: 'BATTERY_CHECK_INTERVAL_S', file: 'app/app_config.h', note: 'Period of the TimeAlignedTask loop, and therefore of VOLTAGE and periodic TIME_ANCHOR records. Measures 299.38 s of real time, not 300, because the FreeRTOS tick divisor truncates (Storage_Redesign.md §12.27 Finding 4).' },
  { name: 'SCHEDULING_INTERVAL_US', file: 'app/app_config.h', note: 'Ranging round period; 500000 => 2 Hz rounds. NOT the RANGES record rate: a record is written only when a round yields at least one range, so the record rate measures round success.' },
  { name: 'DEVICE_TIMEOUT_SECONDS', file: 'app/app_config.h', note: 'Peer considered gone after this long with no traffic.' },
  { name: 'RANGING_ROUNDS_PER_SECOND', file: 'app/app_config.h', note: 'Ranging rounds per second, derived from SCHEDULING_INTERVAL_US. Introduced when the round counters moved from seconds to rounds; several timeouts below are expressed in it, so it must be extracted before them.' },
  { name: 'MAX_EMPTY_ROUNDS_BEFORE_STATE_CHANGE', file: 'app/app_config.h', note: 'Consecutive rounds with no detected devices and no results before the scheduler drops the network. Six rounds = 3 s.' },
  { name: 'STORAGE_FLUSH_TIMEOUT_S', file: 'app/app_config.h', note: 'Upper bound on how long a record can sit unflushed in RAM, and therefore on how much a hard reset costs. 120 s.' },
  { name: 'TIME_BASE_CHANGE_THRESHOLD_MS', file: 'app/app_config.h', note: 'An offset move at least this large writes an audit TIME_ANCHOR before adopting the new base. The host mirrors this as its benign/alarming split for backward page bounds.' },
  { name: 'BATTERY_EVENT_DEBOUNCE_MS', file: 'app/app_config.h', note: 'Minimum spacing between accepted charger edges. A change inside the window is counted into charger_suppressed_edges and deferred, not dropped. Load-bearing for the host: it is the floor on how fast genuine CHARGING_EVENT records can arrive, and therefore what makes a storm diagnosable.' },
  { name: 'RANGING_ROUND_STALL_TIMEOUT_MS', file: 'app/app_config.h', note: 'A ranging round producing no phase transition for this long restarts the schedule phase. Bounds how long a wedged radio can suppress RANGES records before recovering on its own, which is what separates a radio stall from a device outage in a log gap.' },

  // --- Watchdog --------------------------------------------------------------------------------
  // The reset window bounds how much a hang costs, so a reader needs it to interpret a gap.
  { name: 'WATCHDOG_TICK_S', file: 'app/app_config.h', note: 'Nominal seconds per WDT tick at AM_HAL_WDT_1_16HZ. The LFRC has no specified tolerance and measures ~20.94 s, 24% slow (§12.20), so this understates every window below.' },
  { name: 'WATCHDOG_INTERRUPT_TICKS', file: 'app/app_config.h', note: 'Ticks from the last pet to the pre-reset interrupt. Nothing depends on it arriving; §15.2 records that it does not fire at these counts.' },
  { name: 'WATCHDOG_RESET_TICKS', file: 'app/app_config.h', note: 'Ticks from the last pet to the hardware reset. 8 ticks is ~167 s at the measured tick — the bound on how long a stall can persist.' },
  { name: 'WATCHDOG_CHECKIN_INTERVAL_MS', file: 'app/app_config.h', note: 'How often each monitored task checks in.' },
  { name: 'WATCHDOG_CHECKIN_DEADLINE_MS', file: 'app/app_config.h', note: 'A task later than this is stalled; the pet is declined and the task named in the reset diagnostic.' },
  { name: 'WATCHDOG_STARTUP_GRACE_MS', file: 'app/app_config.h', note: 'After arming, the watchdog is petted unconditionally for this long. No stall is diagnosed inside the grace window.' },

  // --- Range validity --------------------------------------------------------------------------
  { name: 'MIN_VALID_RANGE_MM', file: 'app/app_config.h', note: 'Lower bound for a raw two-way-TOF sample. Note: compute_ranges clamps the resulting median to >= 0, so a stored range is never negative and this bound never reaches a log file.' },
  { name: 'MAX_VALID_RANGE_MM', file: 'app/app_config.h', note: 'Upper bound for a stored range. The Python tool used 16000 here; the firmware allows 32000.' },

  // --- Log policy, from the nandlog configuration ------------------------------------------------
  { name: 'NANDLOG_MAX_PAGE_SIZE_BYTES', file: 'external/nandlog/nandlog_conf.h', note: 'RAM sizing budget for a page. A fitted part larger than this is a compile error in its driver.' },
  { name: 'NANDLOG_MAX_SPARE_SIZE_BYTES', file: 'external/nandlog/nandlog_conf.h', note: 'RAM sizing budget for a spare area.' },
  { name: 'NANDLOG_ERASE_AHEAD_BLOCKS', file: 'external/nandlog/nandlog_conf.h', note: 'Blocks kept erased ahead of the write head.' },
  { name: 'NANDLOG_BLOCK_ERRORS_BEFORE_REMOVAL', file: 'external/nandlog/nandlog_conf.h', note: 'Program or erase attempts on one block before it is retired.' },
  { name: 'NANDLOG_PAGE_PLACEMENT_ATTEMPTS', file: 'external/nandlog/nandlog_conf.h', note: 'Blocks one page write may relocate through before the log declares the part unwritable.' },
  { name: 'NANDLOG_MAX_EPOCH_DETAILS_BYTES', file: 'external/nandlog/nandlog_conf.h', note: 'Largest caller-defined metadata blob. experiment_details_t must fit.' },
  { name: 'NANDLOG_TIMESTAMP_TOLERANCE_MS', file: 'external/nandlog/nandlog_conf.h', note: 'A backward timestamp step no larger than this is clamped forward rather than committing the page. 250 ms separates writer disagreement from a moved time base (§12.28 A).' },
  { name: 'NANDLOG_RECORD_FRAMING', file: 'external/nandlog/nandlog_conf.h', note: 'ON. Every record carries its own uint16 length, pages carry TTP2, and a reader can step over a type it has never heard of. Turned on when STORAGE_TYPE_DIAGNOSTICS was added, because adding a record type is exactly the change framing makes safe -- an unframed reader that stops at an unknown type loses the rest of every page containing one, which is how this package went stale against types 7 and 8.' },
  { name: 'STORAGE_DIAGNOSTIC_NUM_POOLS', file: 'app/app_config.h', note: 'WSF buffer pools reported in a diagnostics record. Fixed at the five ble_task.c declares.' },
  { name: 'STORAGE_DIAGNOSTIC_NUM_STACKS', file: 'app/app_config.h', note: 'Stack headroom entries in a diagnostics record: each watchdog task in watchdog_task_t order, then the timer service.' },
  { name: 'STORAGE_DIAGNOSTIC_FLAG_TEMPCO_AVAILABLE', file: 'tasks/storage_records.h', note: 'Diagnostics status bit: this chip\'s factory trims support TempCo.' },
  { name: 'STORAGE_DIAGNOSTIC_FLAG_TEMPCO_APPLIED', file: 'tasks/storage_records.h', note: 'Diagnostics status bit: the last temperature reading adjusted the voltage trims.' },
  { name: 'STORAGE_DIAGNOSTIC_FLAG_FIRMWARE_MODIFIED', file: 'tasks/storage_records.h', note: 'Diagnostics status bit: built from a tree with uncommitted firmware changes, so the revision alone does not identify the build.' },
  { name: 'STORAGE_DIAGNOSTIC_STACK_UNMONITORED', file: 'tasks/storage_records.h', note: 'Stack headroom value for a task that is not running in the current mode.' },
  { name: 'STORAGE_DIAGNOSTIC_FLAG_DIAGNOSTIC_BUILD', file: 'tasks/storage_records.h', note: 'Diagnostics status bit: a DIAGNOSTIC_BUILD, which logs every late radio arm and times radio interrupts.' },
  { name: 'STORAGE_DIAGNOSTIC_FLAG_TEMPCO_DISABLED', file: 'tasks/storage_records.h', note: 'Diagnostics status bit: built with TempCo switched off, whatever the chip supports.' },
  { name: 'STORAGE_RADIO_ABORT_PHASE_RANGING', file: 'tasks/storage_records.h', note: 'Radio abort record phase: a ranging-slot receive, whose failure abandons the round.' },
  { name: 'STORAGE_RADIO_ABORT_PHASE_STATUS', file: 'tasks/storage_records.h', note: 'Radio abort record phase: a status-slot receive, whose failure only ends the status exchange early.' },
  { name: 'STORAGE_RADIO_ABORT_UNMEASURED', file: 'tasks/storage_records.h', note: 'Radio abort record value for a field this build could not measure.' },
  { name: 'STORAGE_RADIO_ABORT_TRIGGER_UNKNOWN', file: 'tasks/storage_records.h', note: 'Radio abort trigger: not inside a radio interrupt, or before it had handled an event.' },
  { name: 'STORAGE_RADIO_ABORT_TRIGGER_TX_DONE', file: 'tasks/storage_records.h', note: 'Radio abort trigger: the interrupt was handling a frame this device sent.' },
  { name: 'STORAGE_RADIO_ABORT_TRIGGER_RX_FRAME', file: 'tasks/storage_records.h', note: 'Radio abort trigger: the interrupt was handling a frame this device received.' },
  { name: 'STORAGE_RADIO_ABORT_TRIGGER_RX_TIMEOUT', file: 'tasks/storage_records.h', note: 'Radio abort trigger: the interrupt was handling a receive window that closed empty.' },
  { name: 'STORAGE_RADIO_ABORT_TRIGGER_RX_ERROR', file: 'tasks/storage_records.h', note: 'Radio abort trigger: the interrupt was handling a frame that could not be decoded.' },
  { name: 'STORAGE_RADIO_ABORT_NO_EVENT_TIME', file: 'tasks/storage_records.h', note: 'Radio abort event_to_isr_us when the triggering event has no radio timestamp, or the interrupt start is unknown.' },
  { name: 'STORAGE_SCHEDULE_CATCH_NONE', file: 'tasks/storage_records.h', note: 'Schedule catch first_copy when the network was lost before any copy was decoded.' },
  { name: 'STORAGE_SCHEDULE_CATCH_UNKNOWN', file: 'tasks/storage_records.h', note: 'Schedule catch rounds_missed when the schedule timestamps went backwards, so the count is meaningless.' },
  { name: 'STORAGE_SCHEDULE_CATCH_UNMEASURED', file: 'tasks/storage_records.h', note: 'Schedule catch and round start value for a time that was not measured; a wake_us of this value means the radio had to be reset.' },
  { name: 'STORAGE_ROUND_START_FLAG_SECOND_COPY_FAILED', file: 'tasks/storage_records.h', note: 'Round start bit: the master\'s second schedule copy could not be armed in time.' },
  { name: 'STORAGE_ROUND_START_FLAG_COMPUTED', file: 'tasks/storage_records.h', note: 'Round start bit: the round reached the computation phase.' },
  { name: 'STORAGE_ROUND_START_FLAG_ABANDONED', file: 'tasks/storage_records.h', note: 'Round start bit: the round ended early on a radio or ranging error.' },
  { name: 'STORAGE_ROUND_START_FLAG_JOIN_HEARD', file: 'tasks/storage_records.h', note: 'Round start bit: the master\'s join window heard a request directly.' },
  { name: 'STORAGE_ROUND_START_FLAG_JOIN_RELAYED', file: 'tasks/storage_records.h', note: 'Round start bit: a join request reached the master relayed through the status exchange.' },
  { name: 'STORAGE_SESSION_END_UNKNOWN', file: 'tasks/storage_records.h', note: 'Session end reason: nothing recorded why.' },
  { name: 'STORAGE_SESSION_END_STOPPED', file: 'tasks/storage_records.h', note: 'Session end reason: the application stopped the scheduler, as it does on finding a higher-ID master.' },
  { name: 'STORAGE_SESSION_END_SEARCH_TIMEOUT', file: 'tasks/storage_records.h', note: 'Session end reason: no round completed within NETWORK_SEARCH_TIME_SECONDS.' },
  { name: 'STORAGE_SESSION_END_COLLISION', file: 'tasks/storage_records.h', note: 'Session end reason: a frame of an unexpected type arrived mid-round, which the scheduler treats as a second network.' },
  { name: 'STORAGE_SESSION_END_SILENT', file: 'tasks/storage_records.h', note: 'Session end reason: as master, heard nobody for MAX_EMPTY_ROUNDS_BEFORE_STATE_CHANGE rounds.' },
  { name: 'STORAGE_RADIO_TIMING_BANDS', file: 'tasks/storage_records.h', note: 'Radio timing record: bands in its frame-to-interrupt histogram, the first below STORAGE_RADIO_TIMING_FIRST_US and the last open-ended.' },
  { name: 'STORAGE_RADIO_TIMING_FIRST_US', file: 'tasks/storage_records.h', note: 'Radio timing record: upper edge of the first frame-to-interrupt band, µs.' },
  { name: 'STORAGE_RADIO_TIMING_STEP_US', file: 'tasks/storage_records.h', note: 'Radio timing record: width of each frame-to-interrupt band after the first, µs.' },
  { name: 'STORAGE_RADIO_TIMING_INTERVAL_MS', file: 'tasks/storage_records.h', note: 'Radio timing record: how often a diagnostic build writes one, ms.' },
  { name: 'NANDLOG_FRAMING_LENGTH_BYTES', file: 'external/nandlog/nandlog.h', note: 'Size of the per-record length prefix when framing is on.' },
  { name: 'NANDLOG_BUSY_TIMEOUT_MS', file: 'external/nandlog/nandlog_conf.h', note: 'How long the chip may hold BUSY before the driver declares it dead and resets.' },
  { name: 'NANDLOG_MAX_RETRANSMIT_PAGES', file: 'external/nandlog/nandlog.h', note: 'Most pages one repair round may request.' },

  // --- v2 on-flash and wire format ---------------------------------------------------------------
  // These were V2_SPEC_PENDING literals transcribed from the design document while the format was
  // unreleased. The format shipped, so they are extracted facts now and drift-checked like anything
  // else. Magics are little-endian uint32 in the firmware; the extractor keeps them numeric and
  // constants.ts renders the ASCII.
  { name: 'NANDLOG_PAGE_MAGIC', file: 'external/nandlog/nandlog.h', note: "'TTP1' little-endian: a data page whose records are opaque to the log." },
  { name: 'NANDLOG_PAGE_MAGIC_FRAMED', file: 'external/nandlog/nandlog.h', note: "'TTP2' little-endian: a data page whose records carry their own lengths." },
  { name: 'NANDLOG_META_MAGIC', file: 'external/nandlog/nandlog.h', note: "'TTM1' little-endian: a metadata page in the epoch ring." },
  { name: 'NANDLOG_STREAM_MAGIC', file: 'external/nandlog/nandlog.h', note: "'TTS1' little-endian: the first four bytes of an offload stream, and the v1/v2 discriminator." },
  { name: 'NANDLOG_NO_TIMESTAMP', file: 'external/nandlog/nandlog.h', note: 'Sentinel in first_timestamp/last_timestamp for a page holding no timestamped record.' },

  // --- USB identity ----------------------------------------------------------------------------
  { name: 'USB_VID', file: 'app/app_config.h', note: 'Web Serial device filter.' },
  { name: 'USB_PID', file: 'app/app_config.h', note: 'Web Serial device filter.' },

  // --- BLE connection parameters, which bound download throughput ------------------------------
  { name: 'BLE_DESIRED_MTU', file: 'app/app_config.h', note: 'Requested ATT MTU. Log chunks are sent as MTU-3 byte notifications.' },
  { name: 'BLE_MIN_CONNECTION_INTERVAL_1_25_MS', file: 'app/app_config.h', note: 'In 1.25 ms units. Bulk download is one notification per connection interval, so this sets the ceiling on transfer rate.' },
  { name: 'BLE_MAX_CONNECTION_INTERVAL_1_25_MS', file: 'app/app_config.h', note: 'In 1.25 ms units.' },
  { name: 'MAX_NUM_CONNECTIONS', file: 'app/app_config.h', note: 'Concurrent BLE connections the tag accepts.' },
  { name: 'BLE_FAST_CONNECTION_INTERVAL_1_25_MS', file: 'app/app_config.h', note: 'In 1.25 ms units. Requested once an offload starts, so this — not the idle interval — is what sets download throughput.' },
  { name: 'BLE_CONNECTION_SLAVE_LATENCY', file: 'app/app_config.h', note: 'Connection events the peripheral may skip when it has nothing to send. Irrelevant during an offload, which has something to send every event, but it is why an idle link feels slow to probe.' },
  { name: 'BLE_SUPERVISION_TIMEOUT_10_MS', file: 'app/app_config.h', note: 'In 10 ms units. A link with no successful exchange for this long drops. The transport must not let a host-side stall exceed it, and a dropped offload resumes via DOWNLOAD_LOG_CONTINUE.' },

  // --- BLE maintenance opcodes -----------------------------------------------------------------
  { name: 'BLE_MAINTENANCE_NEW_EXPERIMENT', file: 'tasks/bluetooth/maintenance_functionality.h' },
  { name: 'BLE_MAINTENANCE_DELETE_EXPERIMENT', file: 'tasks/bluetooth/maintenance_functionality.h' },
  { name: 'BLE_MAINTENANCE_DOWNLOAD_LOG', file: 'tasks/bluetooth/maintenance_functionality.h' },
  { name: 'BLE_MAINTENANCE_SET_LOG_DOWNLOAD_DATES', file: 'tasks/bluetooth/maintenance_functionality.h' },
  { name: 'BLE_MAINTENANCE_DOWNLOAD_LOG_CONTINUE', file: 'tasks/bluetooth/maintenance_functionality.h' },
  { name: 'BLE_MAINTENANCE_PACKET_COMPLETE', file: 'tasks/bluetooth/maintenance_functionality.h' },
  { name: 'BLE_MAINTENANCE_RETRANSMIT_PAGES', file: 'tasks/bluetooth/maintenance_functionality.h', note: 'Count byte then that many u32 sequence numbers. Accumulated across writes because one ATT payload holds a bounded number.' },
  { name: 'BLE_MAINTENANCE_START_RADIO_TEST', file: 'tasks/bluetooth/maintenance_functionality.h', note: 'Start a radio test: u32 start and u32 end Unix seconds, a count byte, then that many 6-byte EUIs including the device written to. The device restarts into the test, and refuses with an ATT error a test it could not run as asked.' },
  { name: 'BLE_MAINTENANCE_STOP_RADIO_TEST', file: 'tasks/bluetooth/maintenance_functionality.h', note: 'End a radio test early. The device restarts into whatever it would otherwise be doing.' },
  { name: 'BLE_MAINTENANCE_RADIO_TEST_HEADER_LEN', file: 'tasks/bluetooth/maintenance_functionality.h', note: 'Bytes in a START_RADIO_TEST write before its EUIs: the opcode, both times and the count.' },
  { name: 'RADIO_TEST_MAX_SECONDS', file: 'tasks/radio_test.h', note: 'Longest radio test a device will accept.' },
  { name: 'RADIO_TEST_LIST_WAIT_S', file: 'tasks/radio_test.h', note: 'How long a device that restarted into a test without its device list waits for the client to send it again, before giving the test up.' },
  { name: 'NUM_XMIT_ANTENNAS', file: 'app/app_config.h', note: 'Antennas each device cycles through in a ranging round, and the per-antenna counts in the live radio statistics.' },
  { name: 'BLE_RADIO_STATS_VERSION', file: 'tasks/bluetooth/live_stats_functionality.h', note: 'Layout version opening the live radio statistics. A reader must refuse any other.' },
  { name: 'BLE_RADIO_STATS_FLAG_TEST_RUNNING', file: 'tasks/bluetooth/live_stats_functionality.h', note: 'Live radio statistics bit: this boot is a radio test.' },
  { name: 'BLE_RADIO_STATS_FLAG_TEST_WAITING', file: 'tasks/bluetooth/live_stats_functionality.h', note: 'Live radio statistics bit: a radio test that lost its device list across the restart and is waiting for the client to send it again.' },
  { name: 'BLE_MAINTENANCE_MAX_SEQS_PER_WRITE', file: 'tasks/bluetooth/maintenance_functionality.h', note: 'Sequence numbers the firmware will read from ONE retransmit write. It applies MIN(count, this) and silently ignores the excess, so a host that sends more loses the remainder with no error. The transport must chunk at exactly this size.' },
];

/**
 * C enums whose numeric values appear in the log format or on the wire.
 *
 * `implicit` enums have no explicit values and are numbered from 0; the extractor asserts that,
 * because an explicit value appearing later would silently renumber everything after it.
 */
export const ENUMS = [
  {
    name: 'storage_data_type_t',
    file: 'tasks/storage_records.h',
    note: 'The type byte that opens every log record. Moved here from peripherals/include/storage.h by the nandlog extraction: record types are the application\'s business, not the log\'s.',
  },
  {
    name: 'motion_code_t',
    file: 'tasks/app_tasks.h',
    note: 'Body of a MOTION record. Now just NOT_IN_MOTION and IN_MOTION — the charger codes that motivated the `motion-record-charger-codes` guess were deleted.',
  },
  {
    name: 'battery_event_t',
    file: 'peripherals/include/battery.h',
    note: 'Body of a CHARGING_EVENT record. 1..4 match the Python tool BATTERY_CODES mapping; 5 (BATTERY_CRITICAL_VOLTAGE) was added for the gated VCOMP brownout path.',
  },
  {
    name: 'schedule_role_t',
    file: 'tasks/app_tasks.h',
    note: 'Advertised ranging role, third byte of the BLE manufacturer data, and the role in a SESSION_END record.',
  },
  {
    name: 'scheduler_phase_t',
    file: 'tasks/ranging/scheduler.h',
    note: 'Ranging protocol phase. Reaches the log only as the phase a SESSION_END record\'s colliding frame arrived in.',
  },
  {
    name: 'packet_t',
    file: 'tasks/ranging/scheduler.h',
    note: 'Message type byte opening every ranging-protocol frame. Reaches the log only as the type of a SESSION_END record\'s colliding frame.',
  },
  {
    name: 'usb_command_t',
    file: 'peripherals/include/usb.h',
    note: 'USB CDC command bytes. Several alias the BLE_MAINTENANCE_* opcodes; the extractor resolves the aliases.',
  },
  {
    name: 'imu_data_type_t',
    file: 'peripherals/include/imu.h',
    note: 'Bitmask of enabled IMU outputs. Only IMU_ACCELEROMETER reaches storage in the default build.',
  },
  {
    name: 'watchdog_task_t',
    file: 'peripherals/include/system.h',
    note: 'The five tasks that must all check in for the watchdog to be petted. Ordering is load-bearing: reset_diagnostic_t\'s per-task stall codes are this enum offset by RESET_DIAGNOSTIC_STALL_TIME_ALIGNED, which the firmware asserts statically.',
  },
  {
    name: 'reset_diagnostic_t',
    file: 'peripherals/include/system.h',
    note: 'The firmware\'s own verdict on what stopped it, carried in the top four bits of a RESET_REASON record\'s status word. RESET_DIAGNOSTIC_NOTHING_RECORDED is re-armed on every boot, so reading it back proves the scratch register survived AND that no handler wrote to it.',
  },
];

/**
 * The packed struct exchanged over BLE and USB to configure a deployment, and stored verbatim in
 * the log metadata page. The extractor parses the declaration and computes offsets, so a field
 * being added, removed, reordered, or resized is caught rather than inferred.
 */
export const STRUCTS = [
  {
    name: 'experiment_details_t',
    file: 'tasks/app_tasks.h',
    note: 'Measured at 239 bytes. Sent as a 240-byte BLE write prefixed with BLE_MAINTENANCE_NEW_EXPERIMENT, and echoed verbatim in the offload stream header.',
    expectedSize: 239,
  },
  {
    name: 'nandlog_page_header_t',
    file: 'external/nandlog/nandlog.h',
    note: 'On-flash page header. 32 bytes, so payload capacity is the chip page size minus 32 — 4064 on a 4096-byte part.',
    expectedSize: 32,
  },
  {
    name: 'nandlog_meta_header_t',
    file: 'external/nandlog/nandlog.h',
    note: 'Metadata ring entry header, followed by details_length bytes of experiment_details_t.',
    expectedSize: 32,
  },
  {
    name: 'nandlog_stream_header_t',
    file: 'external/nandlog/nandlog.h',
    note: 'First 16 bytes of an offload stream, followed by details_length bytes of experiment details.',
    expectedSize: 16,
  },
  {
    name: 'storage_diagnostics_t',
    file: 'tasks/storage_records.h',
    note: 'Payload of a STORAGE_TYPE_DIAGNOSTICS record: counters for conditions the firmware recovered from, then the firmware revision, TempCo state and chip temperature, radio health, dropped records, stack headroom and recoveries. Receive counts per antenna are read live over Bluetooth instead (ble_radio_stats_t), so deployment logs do not carry them. Counters are cumulative since boot and saturating, so a reboot partitions them and a pegged value still reads as bad.',
    expectedSize: 69,
  },
  {
    name: 'storage_radio_abort_t',
    file: 'tasks/storage_records.h',
    note: 'Payload of a STORAGE_TYPE_RADIO_ABORT record, written only by a DIAGNOSTIC_BUILD: one radio receive that could not be armed before its slot, with how late it was, what the radio interrupt had been doing, which radio event it was handling, when it started, how long after that event\'s radio timestamp, and whether it first had to wake the processor.',
    expectedSize: 19,
  },
  {
    name: 'storage_schedule_catch_t',
    file: 'tasks/storage_records.h',
    note: 'Payload of a STORAGE_TYPE_SCHEDULE_CATCH record, written only by a DIAGNOSTIC_BUILD: one participant wake-up from its timer, from the receiver opening to the first schedule copy decoded. lead_us is how far ahead of the expected round\'s first copy the receiver opened, from the radio clock; the carrier offset of the decoded copy separates a late receiver from one that could not lock on; wake_correction_us is the head start this wake-up was armed with beyond its fixed margin, what the device had learned plus any one-round allowance for a master that sent the previous round late, and timer_latency_us how long its wake-up timer\'s interrupt took to run.',
    expectedSize: 20,
  },
  {
    name: 'storage_round_start_t',
    file: 'tasks/storage_records.h',
    note: 'Payload of a STORAGE_TYPE_ROUND_START record, written only by a DIAGNOSTIC_BUILD as the round ends: how long the master took from its wake-up timer to the first schedule copy, and how that round went.',
    expectedSize: 11,
  },
  {
    name: 'storage_session_end_t',
    file: 'tasks/storage_records.h',
    note: 'Payload of a STORAGE_TYPE_SESSION_END record, written only by a DIAGNOSTIC_BUILD: why one run of the ranging scheduler ended, the frame that caused a collision stop, and what the run saw while it lasted.',
    expectedSize: 23,
  },
  {
    name: 'storage_radio_timing_t',
    file: 'tasks/storage_records.h',
    note: 'Payload of a STORAGE_TYPE_RADIO_TIMING record, written once a minute only by a DIAGNOSTIC_BUILD: how close that minute\'s receives came to their deadlines, counting each delayed receive armed in time straight after a received frame.',
    expectedSize: 32,
  },
  {
    name: 'ble_radio_stats_t',
    file: 'tasks/bluetooth/live_stats_functionality.h',
    note: 'Value of BLE_LIVE_STATS_RADIO_CHAR: the device\'s radio counters since it booted, read live by a client instead of logged, with its role, schedule size and any radio test\'s state. Versioned by its first byte.',
    expectedSize: 58,
  },
  {
    name: 'nandlog_wire_page_t',
    file: 'external/nandlog/nandlog.h',
    note: 'Per-page frame in an offload stream, followed by payload_length payload bytes. payload_length 0 marks a page the device could not read.',
    expectedSize: 20,
  },
];

/**
 * 128-bit BLE UUIDs, declared in app_config.h as comma-separated little-endian byte lists.
 *
 * `implemented: false` records a UUID that is defined in the firmware but has no characteristic
 * behind it. Keeping it in the snapshot rather than dropping it means the drift check tells us if
 * that ever changes.
 */
export const BLE_UUIDS = [
  { name: 'BLE_LIVE_STATS_SERVICE_ID', file: 'app/app_config.h', implemented: true },
  { name: 'BLE_LIVE_STATS_BATTERY_CHAR', file: 'app/app_config.h', implemented: true, note: 'Read: u16 millivolts.' },
  { name: 'BLE_LIVE_STATS_TIMESTAMP_CHAR', file: 'app/app_config.h', implemented: true, note: 'Read/write: u32 Unix seconds.' },
  { name: 'BLE_LIVE_STATS_FINDMYTOTTAG_CHAR', file: 'app/app_config.h', implemented: true, note: 'Write: u32 seconds to buzz.' },
  { name: 'BLE_LIVE_STATS_RANGING_CHAR', file: 'app/app_config.h', implemented: true, note: 'Notify: count byte then (u8 uid, i16 mm) pairs.' },
  {
    name: 'BLE_LIVE_STATS_ADDRESS_CHAR',
    file: 'app/app_config.h',
    implemented: false,
    note: 'Defined in app_config.h but absent from live_stats_service.h and never registered as an attribute. Reading it returns an ATT error. Device identity must come from GATT System ID 0x2A23 instead, which matters because Web Bluetooth cannot see MAC addresses.',
  },
  { name: 'BLE_LIVE_STATS_IMU_DATA_CHAR', file: 'app/app_config.h', implemented: true, note: 'Notify: 3 x i16 accelerometer.' },
  { name: 'BLE_LIVE_STATS_RADIO_CHAR', file: 'app/app_config.h', implemented: true, note: 'Read: ble_radio_stats_t, the radio counters since boot.' },
  { name: 'BLE_MAINTENANCE_SERVICE_ID', file: 'app/app_config.h', implemented: true },
  { name: 'BLE_MAINTENANCE_EXPERIMENT_CHAR', file: 'app/app_config.h', implemented: true, note: 'Read: experiment_details_t.' },
  { name: 'BLE_MAINTENANCE_COMMAND_CHAR', file: 'app/app_config.h', implemented: true, note: 'Write: opcode byte then payload.' },
  { name: 'BLE_MAINTENANCE_DATA_CHAR', file: 'app/app_config.h', implemented: true, note: 'Notify: log download stream.' },
  {
    name: 'BLE_MODE_SWITCH_CHAR',
    file: 'app/app_config.h',
    implemented: false,
    note: 'Only registered when the firmware is built with _REMOTE_MODE_SWITCH_ENABLED, which no shipping build sets. Writes are accepted by the stack and silently ignored.',
  },
];

/**
 * Fixed-point scale factors for IMU sample decoding, from the BNO08x driver.
 *
 * These are revision-dependent in a way the log file cannot express: revM uses the BNO055 with
 * UNIT_SEL=0 (1 LSB = 1/100 m/s^2) while revN/O/P use the BNO08x with Q8 scaling (1 LSB = 1/256
 * m/s^2). See guess `imu-accel-scale-unknown-revision`.
 */
export const SCALE_FACTORS = [
  { name: 'SCALE_Q4', file: 'peripherals/src/imu.c', note: 'Magnetometer, uT.' },
  { name: 'SCALE_Q8', file: 'peripherals/src/imu.c', note: 'Accelerometer / linear accel / gravity, m/s^2. This is the one that reaches storage.' },
  { name: 'SCALE_Q9', file: 'peripherals/src/imu.c', note: 'Gyroscope, rad/s.' },
  { name: 'SCALE_Q14', file: 'peripherals/src/imu.c', note: 'Rotation vector quaternion components.' },
];

/**
 * The record-length function the extractor parses out of the firmware, rather than a list of names.
 *
 * This is the third copy of the record grammar — `stored_record_length()` in the firmware,
 * `recordLength()` in `src/log.ts`, and `_record_length()` in the Python tool — and it is the one
 * kind of drift the constant snapshot cannot see, because it is a function rather than a value.
 *
 * A length disagreement is not a one-record error. In an unframed page the reader steps by the
 * length it computed, so a wrong length desynchronises every record after it; in a framed page the
 * declared length keeps the walk honest but the decoded body is still wrong. Either way the damage
 * is unbounded, which is why this is extracted and asserted rather than reviewed.
 *
 * The extractor recognises exactly the two shapes the firmware uses today — a fixed size, and a
 * base plus a count byte read from the record's own body — and fails on anything else. That is
 * deliberate: a grammar parser that falls back to a guess produces a reader that runs and lies.
 */
export const RECORD_GRAMMAR = {
  file: 'tasks/storage_records.h',
  function: 'stored_record_length',
  note: 'Every storage_data_type_t except SHUTDOWN (a queue message, never stored) must appear.',
};
