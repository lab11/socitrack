// Where every exported constant comes from.
//
// Adopted from the A3EM dashboard's key check, whose important half is the one that is easy to
// leave out. Asserting "everything the firmware declares is handled" catches a constant going
// missing. Asserting "nothing is present beyond the documented exceptions" catches the opposite and
// commoner drift: a number typed into the host because it was needed, which then looks exactly like
// a firmware fact to the next reader.
//
// So every export of constants.ts is classified here, and `provenance.test.ts` enforces both
// directions. Adding a host-only constant means adding a line here with a reason — which is the
// point. The reason is the artefact; the list is just where it lives.

/** Exports whose value is a structured part of the snapshot, by the path it comes from. */
export const SNAPSHOT_BACKED: Readonly<Record<string, string>> = {
  REVISION_IDS: 'revisionIds',
  PER_CHIP: 'perChip',
  STORAGE_TYPE: 'enums.storage_data_type_t.members',
  MOTION_CODE: 'enums.motion_code_t.members',
  BATTERY_EVENT: 'enums.battery_event_t.members',
  SCHEDULE_ROLE: 'enums.schedule_role_t.members',
  SCHEDULER_PHASE: 'enums.scheduler_phase_t.members',
  PACKET_TYPE: 'enums.packet_t.members',
  USB_COMMAND: 'enums.usb_command_t.members',
  IMU_DATA_TYPE: 'enums.imu_data_type_t.members',
  WATCHDOG_TASK: 'enums.watchdog_task_t.members',
  RESET_DIAGNOSTIC: 'enums.reset_diagnostic_t.members',
  EXPERIMENT_DETAILS_LAYOUT: 'structs.experiment_details_t',
  STREAM_HEADER_LAYOUT: 'structs.nandlog_stream_header_t',
  WIRE_PAGE_LAYOUT: 'structs.nandlog_wire_page_t',
  DIAGNOSTICS_LAYOUT: 'structs.storage_diagnostics_t',
  RADIO_ABORT_LAYOUT: 'structs.storage_radio_abort_t',
  SCHEDULE_CATCH_LAYOUT: 'structs.storage_schedule_catch_t',
  ROUND_START_LAYOUT: 'structs.storage_round_start_t',
  SESSION_END_LAYOUT: 'structs.storage_session_end_t',
  RADIO_TIMING_LAYOUT: 'structs.storage_radio_timing_t',
  RADIO_STATS_LAYOUT: 'structs.ble_radio_stats_t',
  PAGE_HEADER_LAYOUT: 'structs.nandlog_page_header_t',
  META_HEADER_LAYOUT: 'structs.nandlog_meta_header_t',
  BLE_UUID: 'uuids',
  SCALE_FACTOR: 'scaleFactors',
  BOARD_REVISIONS: 'revisionIds (keys)',
};

export type Provenance =
  /** Computed from extracted values. The test re-derives it and compares. */
  | 'derived'
  /** Defined by a standard outside this project — Bluetooth SIG, IEEE, the C language. */
  | 'external-standard'
  /** Measured from hardware or real logs. Not in the firmware at any level. */
  | 'measured'
  /** The v1 format, which no current firmware writes, so there is nothing left to extract. */
  | 'legacy-format'
  /** Mirrors firmware source that is not a #define, enum or struct and so cannot be extracted. */
  | 'mirrors-unextractable-source';

/**
 * Every exported constant that is NOT a snapshot value, with why it is allowed to exist.
 *
 * A `derived` entry must state its derivation, because a derivation that is only in someone's head
 * is a literal with extra steps.
 */
export const HOST_ONLY_CONSTANTS: Readonly<Record<string, { provenance: Provenance; reason: string }>> = {
  // --- derived from extracted values --------------------------------------------------------------
  RECORD_HEADER_BYTES: { provenance: 'derived', reason: '1 type byte + 4 timestamp bytes, the record prefix nandlog.h documents.' },
  TIMESTAMP_WRAP_SECONDS: { provenance: 'derived', reason: 'NANDLOG_NO_TIMESTAMP / 1000. The raw uint32-millisecond ceiling.' },
  MAX_EXPERIMENT_ELAPSED_SECONDS: { provenance: 'derived', reason: 'floor((NANDLOG_NO_TIMESTAMP - 1 - 990) / 1000): the sentinel less the RTC sub-second term.' },
  MAX_DEPLOYMENT_DAYS: { provenance: 'derived', reason: 'floor(MAX_EXPERIMENT_ELAPSED_SECONDS / 86400). Whole days, for a control denominated in days.' },
  MAX_DEPLOYMENT_SECONDS: { provenance: 'derived', reason: 'MAX_DEPLOYMENT_DAYS * 86400.' },
  WATCHDOG_RESET_WINDOW_S: { provenance: 'derived', reason: 'WATCHDOG_RESET_TICKS * WATCHDOG_MEASURED_TICK_S. Measured tick, not the nominal one.' },
  V2_DATA_PAGE_MAGIC: { provenance: 'derived', reason: "ASCII of the extracted numeric NANDLOG_PAGE_MAGIC." },
  V2_DATA_PAGE_MAGIC_FRAMED: { provenance: 'derived', reason: 'ASCII of NANDLOG_PAGE_MAGIC_FRAMED.' },
  V2_METADATA_PAGE_MAGIC: { provenance: 'derived', reason: 'ASCII of NANDLOG_META_MAGIC.' },
  V2_STREAM_MAGIC: { provenance: 'derived', reason: 'ASCII of NANDLOG_STREAM_MAGIC. Also the v1/v2 discriminator.' },
  V2_FORMAT_VERSION_UNFRAMED: { provenance: 'derived', reason: 'NANDLOG_FORMAT_VERSION when framing is off: 1.' },
  V2_FORMAT_VERSION_FRAMED: { provenance: 'derived', reason: 'NANDLOG_FORMAT_VERSION when framing is on: 2.' },
  V2_FORMAT_VERSION_THIS_BUILD: { provenance: 'derived', reason: 'Which of the two the current NANDLOG_RECORD_FRAMING selects.' },
  RESET_STATUS_MASK: { provenance: 'derived', reason: 'Low 12 bits of a RESET_REASON status word; the top 4 carry the diagnostic.' },
  RESET_DIAGNOSTIC_SHIFT: { provenance: 'derived', reason: 'The diagnostic occupies bits 12-15, asserted against reset_diagnostic_t fitting in 4 bits.' },
  RESET_DIAGNOSTIC_MASK: { provenance: 'derived', reason: '4 bits wide, paired with RESET_DIAGNOSTIC_SHIFT.' },

  // --- external standards ---------------------------------------------------------------------------
  GATT_SYSTEM_ID_UUID: { provenance: 'external-standard', reason: 'Bluetooth SIG 0x2A23. The only route to a device EUI in a browser, since Web Bluetooth exposes no MAC.' },
  GATT_FIRMWARE_REVISION_UUID: { provenance: 'external-standard', reason: 'Bluetooth SIG 0x2A26.' },
  GATT_HARDWARE_REVISION_UUID: { provenance: 'external-standard', reason: 'Bluetooth SIG 0x2A27.' },
  RTC_RESOLUTION_MS: { provenance: 'external-standard', reason: 'The Apollo4 RTC reports hundredths, so 10 ms is its quantum.' },

  // --- measured -----------------------------------------------------------------------------------
  TIME_ALIGNED_INTERVAL_S: { provenance: 'measured', reason: '299.38 s across 1112-1134 intervals on four devices. BATTERY_CHECK_INTERVAL_S says 300; the FreeRTOS tick divisor truncates.' },
  WATCHDOG_MEASURED_TICK_S: { provenance: 'measured', reason: '20.94 s against a nominal 16. The LFRC has no specified tolerance. See the watchdog-tick-rate guess.' },
  BNO055_ACCEL_SCALE: { provenance: 'measured', reason: '1/100 m/s^2, from the BNO055 UNIT_SEL=0 setting. Not a Q-format constant, so there is nothing to extract.' },

  // --- the v1 format, which no current firmware writes ------------------------------------------------
  V1_PAGE_MAGIC: { provenance: 'legacy-format', reason: "'DA'. The v1 page header, deleted from firmware by the nandlog extraction; archived files still use it." },
  V1_PAGE_HEADER_BYTES: { provenance: 'legacy-format', reason: "2 magic bytes + a uint16 length. Gone from firmware with the nandlog extraction, so it cannot be extracted; archived .ttg files still carry it." },
  V1_METADATA_MAGIC: { provenance: 'legacy-format', reason: "'META', the v1 metadata page marker. The v1 layout was deleted with the nandlog extraction, so there is nothing left in firmware to extract it from." },
  TIMESTAMP_QUANTUM_MS: { provenance: 'legacy-format', reason: '500 ms. The v1 resynchronisation signal; current firmware stores the RTC 10 ms resolution instead.' },

  // --- mirrors firmware that is not extractable -------------------------------------------------------
  DIAGNOSTICS_STACK_NAMES: { provenance: 'derived', reason: 'WATCHDOG_TASK_NAMES then the timer service, matching STORAGE_DIAGNOSTIC_NUM_STACKS and the order storage_task.c fills stack_free_words.' },
  WATCHDOG_TASK_NAMES: { provenance: 'mirrors-unextractable-source', reason: 'watchdog_task_names[] in system.c is a static array of string literals, not a macro. Pinned to watchdog_task_t order by a test.' },
  RESET_STATUS_BITS: { provenance: 'mirrors-unextractable-source', reason: 'am_hal_reset_status_e lives in the Ambiq SDK, outside FIRMWARE_ROOT and outside this spec.' },
  RESET_WATCHDOG_BIT: { provenance: 'mirrors-unextractable-source', reason: 'The watchdog bit within am_hal_reset_status_e.' },
  SYSTEM_ID_EUI_OFFSETS: { provenance: 'mirrors-unextractable-source', reason: 'bluetooth_init() packs [uid0,uid1,uid2,0xFE,0xFF,uid3,uid4,uid5] in C statements, not a declaration.' },
  V2_METADATA_RING_BLOCKS: { provenance: 'mirrors-unextractable-source', reason: 'A static in nandlog.c rather than a header constant. Pinned against the source by constants.test.ts.' },
  V2_RETRANSMIT_RETRY_ROUNDS: { provenance: 'mirrors-unextractable-source', reason: 'A host-side loop guard the firmware does not constrain; NANDLOG_MAX_RETRANSMIT_PAGES bounds the request size only.' },
  V2_RETRANSMIT_PAGE_ATTEMPTS: { provenance: 'mirrors-unextractable-source', reason: 'A host-side retry policy the firmware does not constrain; the device re-reads whatever page it is asked for.' },
};
