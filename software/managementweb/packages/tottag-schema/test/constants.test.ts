// Consistency of the generated constants module with the snapshot, and internal consistency of the
// firmware facts themselves.
//
// The second half matters more than it looks. Relationships like "payload bytes per page equals page
// size minus the header" are true today by construction, but they are exactly what silently breaks
// when someone changes one side of a `#define` pair. Asserting them turns an implicit dependency
// into a caught one.

import { test } from 'node:test';
import assert from 'node:assert/strict';

import { readSnapshot } from '../tools/extract-constants.mjs';
import { CHIPS } from '../tools/spec.mjs';
import {
  PER_CHIP, STORAGE_TYPE, MOTION_CODE, BATTERY_EVENT, USB_COMMAND, WATCHDOG_TASK, RESET_DIAGNOSTIC,
  EXPERIMENT_DETAILS_LAYOUT, PAGE_HEADER_LAYOUT, META_HEADER_LAYOUT, STREAM_HEADER_LAYOUT,
  WIRE_PAGE_LAYOUT, BLE_UUID, SCALE_FACTOR,
  EUI_LEN, EUI_NAME_MAX_LEN, MAX_NUM_RANGING_DEVICES, COMPRESSED_RANGE_DATUM_LENGTH,
  MAX_COMPRESSED_RANGE_DATA_LENGTH, MIN_VALID_RANGE_MM, MAX_VALID_RANGE_MM,
  V1_PAGE_HEADER_BYTES, RECORD_HEADER_BYTES, TIMESTAMP_QUANTUM_MS, TIMESTAMP_WRAP_SECONDS,
  BLE_MAINTENANCE_NEW_EXPERIMENT, BLE_MAINTENANCE_DELETE_EXPERIMENT,
  BLE_MAINTENANCE_DOWNLOAD_LOG, BLE_MAINTENANCE_SET_LOG_DOWNLOAD_DATES,
  SYSTEM_ID_EUI_OFFSETS, BLE_DESIRED_MTU, V2_STREAM_MAGIC, V2_DATA_PAGE_MAGIC,
  V2_DATA_PAGE_MAGIC_FRAMED, V2_METADATA_PAGE_MAGIC, V2_FORMAT_VERSION_THIS_BUILD,
  V2_METADATA_RING_BLOCKS, NANDLOG_RECORD_FRAMING, NANDLOG_MAX_PAGE_SIZE_BYTES,
  NANDLOG_MAX_EPOCH_DETAILS_BYTES, WATCHDOG_CHECKIN_DEADLINE_MS, WATCHDOG_CHECKIN_INTERVAL_MS,
  WATCHDOG_INTERRUPT_TICKS, WATCHDOG_RESET_TICKS, WATCHDOG_RESET_WINDOW_S, STORAGE_FLUSH_TIMEOUT_S,
  TIME_ALIGNED_INTERVAL_S, BATTERY_CHECK_INTERVAL_S, WATCHDOG_TASK_NAMES, DIAGNOSTICS_LAYOUT,
  STORAGE_DIAGNOSTIC_NUM_POOLS, STORAGE_DIAGNOSTIC_NUM_STACKS,
  DIAGNOSTICS_STACK_NAMES, NANDLOG_FRAMING_LENGTH_BYTES,
  NANDLOG_NO_TIMESTAMP,
  MAX_EXPERIMENT_ELAPSED_SECONDS,
  MAX_DEPLOYMENT_DAYS,
  MAX_DEPLOYMENT_SECONDS,
} from '../src/constants.ts';

const snapshot = readSnapshot();

// --- The module agrees with the snapshot ---------------------------------------------------------

test('scalar exports equal their snapshot values', () => {
  assert.equal(EUI_LEN, snapshot.defines.EUI_LEN);
  assert.equal(EUI_NAME_MAX_LEN, snapshot.defines.EUI_NAME_MAX_LEN);
  assert.equal(MAX_NUM_RANGING_DEVICES, snapshot.defines.MAX_NUM_RANGING_DEVICES);
  assert.equal(MAX_VALID_RANGE_MM, snapshot.defines.MAX_VALID_RANGE_MM);
  assert.equal(MIN_VALID_RANGE_MM, snapshot.defines.MIN_VALID_RANGE_MM);
  assert.equal(BLE_DESIRED_MTU, snapshot.defines.BLE_DESIRED_MTU);
});

test('enum exports equal their snapshot members', () => {
  assert.deepEqual(STORAGE_TYPE, snapshot.enums.storage_data_type_t.members);
  assert.deepEqual(MOTION_CODE, snapshot.enums.motion_code_t.members);
  assert.deepEqual(BATTERY_EVENT, snapshot.enums.battery_event_t.members);
  assert.deepEqual(USB_COMMAND, snapshot.enums.usb_command_t.members);
});

// --- Internal consistency of the firmware facts --------------------------------------------------

test('record and page header sizes agree with the format', () => {
  // A record is `type:u8, timestamp:u32, body`.
  assert.equal(RECORD_HEADER_BYTES, 1 + 4);
  // A v1 page is `'D','A', length:u16, payload`.
  assert.equal(V1_PAGE_HEADER_BYTES, 2 + 2);
});

test('payload bytes per page equals page size minus the page header, on every part', () => {
  // The single most consequential derived number in the format: get it wrong and every page is
  // mis-sliced with nothing to signal it. The snapshot value is derived from the measured struct, so
  // this re-derives it from the other direction.
  for (const chip of CHIPS as ReadonlyArray<{ key: string }>) {
    const geometry = PER_CHIP[chip.key as keyof typeof PER_CHIP];
    assert.equal(
      geometry.PAYLOAD_BYTES_PER_PAGE,
      geometry.NANDLOG_CHIP_PAGE_SIZE_BYTES - PAGE_HEADER_LAYOUT.size,
      chip.key,
    );
    assert.ok(
      geometry.NANDLOG_CHIP_PAGE_SIZE_BYTES <= NANDLOG_MAX_PAGE_SIZE_BYTES,
      `${chip.key} page exceeds the RAM sizing budget the firmware asserts against`,
    );
    // Every part's pages-per-block must be a power of two; nandlog masks rather than divides.
    const perBlock = geometry.NANDLOG_CHIP_PAGES_PER_BLOCK;
    assert.equal(perBlock & (perBlock - 1), 0, `${chip.key} pages per block is not a power of two`);
  }
});

test('experiment details fit inside the metadata budget', () => {
  // nandlog_store_metadata refuses a blob larger than this, and it refuses it at runtime with a
  // return value the caller has to check -- so a struct that outgrew the budget would present as
  // experiments silently failing to save.
  assert.ok(EXPERIMENT_DETAILS_LAYOUT.size <= NANDLOG_MAX_EPOCH_DETAILS_BYTES);
});

test('range datum size and maximum body size agree', () => {
  // u8 uid + i16 millimetres.
  assert.equal(COMPRESSED_RANGE_DATUM_LENGTH, 1 + 2);
  // One count byte plus one datum per possible peer.
  assert.equal(
    MAX_COMPRESSED_RANGE_DATA_LENGTH,
    1 + COMPRESSED_RANGE_DATUM_LENGTH * MAX_NUM_RANGING_DEVICES,
  );
});

test('experiment_details_t layout is contiguous, packed, and 239 bytes', () => {
  assert.equal(EXPERIMENT_DETAILS_LAYOUT.size, 239);

  let expectedOffset = 0;
  for (const field of EXPERIMENT_DETAILS_LAYOUT.fields) {
    assert.equal(field.offset, expectedOffset, `field ${field.name} is not contiguous with its predecessor`);
    const elements = field.dimensions.length === 0
      ? 1
      : field.dimensions.reduce((a: number, b: number) => a * b, 1);
    assert.equal(field.size, field.elementSize * elements, `field ${field.name} size does not match its dimensions`);
    expectedOffset += field.size;
  }
  assert.equal(expectedOffset, EXPERIMENT_DETAILS_LAYOUT.size);

  // The two array fields are the ones a resize would silently shift everything after.
  const byName = Object.fromEntries(EXPERIMENT_DETAILS_LAYOUT.fields.map((f) => [f.name, f]));
  assert.deepEqual(byName.uids?.dimensions, [MAX_NUM_RANGING_DEVICES, EUI_LEN]);
  assert.deepEqual(byName.uid_name_mappings?.dimensions, [MAX_NUM_RANGING_DEVICES, EUI_NAME_MAX_LEN]);
});

test('storage type values are dense and start at zero', () => {
  // The reader validates `1 <= type < STORAGE_NUM_TYPES`, which is only meaningful if the values
  // form a contiguous run.
  const values = Object.entries(STORAGE_TYPE)
    .filter(([name]) => name !== 'STORAGE_NUM_TYPES')
    .map(([, value]) => value)
    .sort((a, b) => a - b);
  assert.deepEqual(values, values.map((_, index) => index));
  assert.equal(STORAGE_TYPE.STORAGE_NUM_TYPES, values.length);
});

test('USB commands that alias BLE maintenance opcodes still alias them', () => {
  // usb.h defines these by reference. If either side is renumbered independently, the USB transport
  // starts issuing the wrong command with no compile error on either side.
  assert.equal(USB_COMMAND.USB_NEW_EXPERIMENT_COMMAND, BLE_MAINTENANCE_NEW_EXPERIMENT);
  assert.equal(USB_COMMAND.USB_DELETE_EXPERIMENT_COMMAND, BLE_MAINTENANCE_DELETE_EXPERIMENT);
  assert.equal(USB_COMMAND.USB_DOWNLOAD_LOG_COMMAND, BLE_MAINTENANCE_DOWNLOAD_LOG);
  assert.equal(USB_COMMAND.USB_SET_LOG_DL_DATES_COMMAND, BLE_MAINTENANCE_SET_LOG_DOWNLOAD_DATES);
});

test('USB command values do not collide', () => {
  const values = Object.values(USB_COMMAND);
  assert.equal(new Set(values).size, values.length, 'two USB commands share a byte value');
});

test('battery event codes cover the values the Python tool maps', () => {
  // BATTERY_CODES in tottag.py maps 1..4. BATTERY_CRITICAL_VOLTAGE (5) was added later and is
  // rejected by that tool's `0 < value < 5` guard.
  assert.equal(BATTERY_EVENT.BATTERY_PLUGGED, 1);
  assert.equal(BATTERY_EVENT.BATTERY_UNPLUGGED, 2);
  assert.equal(BATTERY_EVENT.BATTERY_CHARGING, 3);
  assert.equal(BATTERY_EVENT.BATTERY_NOT_CHARGING, 4);
  assert.equal(BATTERY_EVENT.BATTERY_CRITICAL_VOLTAGE, 5);
});

test('motion codes are exactly the two motion states', () => {
  // The former NOT_ON_CHARGER / ON_CHARGER codes were deleted from the firmware, which is what
  // closed the `motion-record-charger-codes` guess. A body outside {0,1} is now corruption, and the
  // reader rejects it -- so a third code reappearing here silently changes that verdict.
  assert.deepEqual(MOTION_CODE, { NOT_IN_MOTION: 0, IN_MOTION: 1 });
});

test('timestamp quantum and wrap point are what the format implies', () => {
  assert.equal(TIMESTAMP_QUANTUM_MS, 500);
  // uint32 milliseconds.
  assert.equal(TIMESTAMP_WRAP_SECONDS, 4294967.295);
  assert.ok(TIMESTAMP_WRAP_SECONDS / 86400 > 49 && TIMESTAMP_WRAP_SECONDS / 86400 < 50);
});

// --- Bluetooth --------------------------------------------------------------------------------------

test('BLE UUIDs are well formed and distinct', () => {
  const seen = new Set<string>();
  for (const [name, entry] of Object.entries(BLE_UUID)) {
    assert.match(entry.uuid, /^[0-9a-f]{8}(-[0-9a-f]{4}){3}-[0-9a-f]{12}$/, `${name} is malformed`);
    assert.ok(!seen.has(entry.uuid), `${name} duplicates another UUID`);
    seen.add(entry.uuid);
  }
});

test('UUIDs with no characteristic behind them stay marked unimplemented', () => {
  // If either of these gains an implementation in firmware, this test should fail so the app can
  // start using it rather than continuing to route around it.
  assert.equal(BLE_UUID.BLE_LIVE_STATS_ADDRESS_CHAR.implemented, false);
  assert.equal(BLE_UUID.BLE_MODE_SWITCH_CHAR.implemented, false);
});

test('System ID offsets select six bytes from the eight-byte value', () => {
  // bluetooth_init packs [uid0, uid1, uid2, 0xFE, 0xFF, uid3, uid4, uid5].
  assert.equal(SYSTEM_ID_EUI_OFFSETS.length, EUI_LEN);
  assert.deepEqual([...SYSTEM_ID_EUI_OFFSETS], [0, 1, 2, 5, 6, 7]);
});

// --- IMU ----------------------------------------------------------------------------------------------

test('Q-format scale factors are exact powers of two', () => {
  for (const [name, value] of Object.entries(SCALE_FACTOR)) {
    const exponent = Math.log2(value);
    assert.ok(Number.isInteger(exponent), `${name} = ${value} is not a power of two`);
    assert.ok(exponent < 0, `${name} should be a fractional scale`);
  }
  assert.equal(SCALE_FACTOR.SCALE_Q8, 1 / 256);
});

// --- v2 ------------------------------------------------------------------------------------------------

test('v2 magics decode to the expected ASCII', () => {
  // These were prose literals until the format shipped. They are extracted now, so the assertion is
  // that the little-endian uint32 in the firmware still spells what the format document says.
  assert.equal(V2_STREAM_MAGIC, 'TTS1');
  assert.equal(V2_DATA_PAGE_MAGIC, 'TTP1');
  assert.equal(V2_DATA_PAGE_MAGIC_FRAMED, 'TTP2');
  assert.equal(V2_METADATA_PAGE_MAGIC, 'TTM1');
  assert.equal(V2_METADATA_RING_BLOCKS, 8);
});

test('v1 and v2 stream magics cannot be confused', () => {
  // Format detection reads the first byte: a v1 stream opens with a record type in 1..8, a v2
  // stream opens with 'T'. This holds only while 'T' (0x54) is outside the v1 type range.
  const streamMagicFirstByte = V2_STREAM_MAGIC.charCodeAt(0);
  assert.equal(streamMagicFirstByte, 0x54);
  assert.ok(streamMagicFirstByte >= STORAGE_TYPE.STORAGE_NUM_TYPES, 'v2 magic collides with the v1 type range');
});

test('the stream format version matches the framing switch', () => {
  assert.equal(V2_FORMAT_VERSION_THIS_BUILD, NANDLOG_RECORD_FRAMING ? 2 : 1);
});

test('every v2 wire struct is packed at the size the format document states', () => {
  // A wire struct that gained padding would shift every field after it, and the failure would be
  // per-page garbage rather than a parse error.
  assert.equal(PAGE_HEADER_LAYOUT.size, 32);
  assert.equal(META_HEADER_LAYOUT.size, 32);
  assert.equal(STREAM_HEADER_LAYOUT.size, 16);
  assert.equal(WIRE_PAGE_LAYOUT.size, 20);
  for (const layout of [PAGE_HEADER_LAYOUT, META_HEADER_LAYOUT, STREAM_HEADER_LAYOUT, WIRE_PAGE_LAYOUT]) {
    let expected = 0;
    for (const field of layout.fields) {
      assert.equal(field.offset, expected, `field ${field.name} is not contiguous`);
      expected += field.size;
    }
    assert.equal(expected, layout.size);
  }
});

// --- Watchdog -------------------------------------------------------------------------------------------

test('per-task stall diagnostics stay aligned with the watchdog task list', () => {
  // The firmware asserts this statically. It is re-checked here because a reordering would rename
  // every stall in every archived log with nothing else failing -- the decoder derives task names
  // from this offset rather than transcribing them.
  const base = RESET_DIAGNOSTIC.RESET_DIAGNOSTIC_STALL_TIME_ALIGNED;
  assert.equal(base + WATCHDOG_TASK.WATCHDOG_NUM_TASKS, RESET_DIAGNOSTIC.RESET_DIAGNOSTIC_STALL_MULTIPLE);
  assert.equal(WATCHDOG_TASK.WATCHDOG_TASK_BLE - WATCHDOG_TASK.WATCHDOG_TASK_TIME_ALIGNED,
    RESET_DIAGNOSTIC.RESET_DIAGNOSTIC_STALL_BLE - base);
});

test('the diagnostic field fits in the four bits above the reset status', () => {
  const highest = Math.max(...Object.values(RESET_DIAGNOSTIC));
  assert.ok(highest <= 0xf, 'reset diagnostics no longer fit alongside the status word');
  assert.equal(RESET_DIAGNOSTIC.RESET_DIAGNOSTIC_NOTHING_RECORDED, highest);
});

test('the watchdog configuration cannot reset on a single missed check-in', () => {
  // The firmware guards this with #error. Re-checked because the guard protects a deployment from a
  // reboot loop, and a config that drifts past it is the one change that is worse than the hang.
  assert.ok(WATCHDOG_INTERRUPT_TICKS < WATCHDOG_RESET_TICKS);
  assert.ok(WATCHDOG_RESET_TICKS <= 255 && WATCHDOG_INTERRUPT_TICKS <= 255);
  assert.ok(WATCHDOG_CHECKIN_DEADLINE_MS > 2 * WATCHDOG_CHECKIN_INTERVAL_MS);
  assert.ok(WATCHDOG_RESET_WINDOW_S * 1000 > WATCHDOG_CHECKIN_DEADLINE_MS);
});

test('the flush timeout is shorter than the watchdog window', () => {
  // This is what makes silence readable: under the flush timeout, a reset cost only the unflushed
  // page; past it, the device had stopped executing. The two thresholds must not cross, or that
  // distinction collapses.
  assert.ok(STORAGE_FLUSH_TIMEOUT_S < WATCHDOG_RESET_WINDOW_S);
});

test('the time-aligned interval is the tick-corrected one, not the nominal one', () => {
  // The FreeRTOS port truncates 32768/100 to 327, so a 300 s delay measures 299.38 s. Anything
  // checking cadence against BATTERY_CHECK_INTERVAL_S reports a 0.2% shortfall on a healthy device.
  assert.ok(TIME_ALIGNED_INTERVAL_S < BATTERY_CHECK_INTERVAL_S);
  assert.ok(Math.abs(TIME_ALIGNED_INTERVAL_S - BATTERY_CHECK_INTERVAL_S * (327 / 327.68)) < 0.01);
});

test('the watchdog task name list is pinned to the enum', () => {
  // The diagnostics record's late-episode array is indexed by watchdog_task_t, so the order of these names
  // is load-bearing and their count must track the enum. Adding a task should break the build here rather
  // than silently mislabel a counter.
  assert.equal(WATCHDOG_TASK_NAMES.length, WATCHDOG_TASK.WATCHDOG_NUM_TASKS);
  assert.equal(WATCHDOG_TASK_NAMES[WATCHDOG_TASK.WATCHDOG_TASK_BLE], 'BLETask');
  assert.equal(WATCHDOG_TASK_NAMES[WATCHDOG_TASK.WATCHDOG_TASK_TIME_ALIGNED], 'TimeAlignedTask');
});

test('the diagnostics payload is packed at the size the record grammar assumes', () => {
  // 69 bytes and every field contiguous. A gap here would shift every counter after it.
  assert.equal(DIAGNOSTICS_LAYOUT.size, 69);
  assert.equal(DIAGNOSTICS_STACK_NAMES.length, STORAGE_DIAGNOSTIC_NUM_STACKS);
  let expected = 0;
  for (const field of DIAGNOSTICS_LAYOUT.fields) {
    assert.equal(field.offset, expected, `field ${field.name} is not contiguous`);
    expected += field.size;
  }
  assert.equal(expected, DIAGNOSTICS_LAYOUT.size);
  const byName = Object.fromEntries(DIAGNOSTICS_LAYOUT.fields.map((f) => [f.name, f]));
  assert.deepEqual(byName.watchdog_late_episodes?.dimensions, [WATCHDOG_TASK.WATCHDOG_NUM_TASKS]);
  assert.deepEqual(byName.wsf_pool_high_water?.dimensions, [STORAGE_DIAGNOSTIC_NUM_POOLS]);
  assert.deepEqual(byName.stack_free_words?.dimensions, [STORAGE_DIAGNOSTIC_NUM_STACKS]);
  assert.equal(STORAGE_DIAGNOSTIC_NUM_STACKS, WATCHDOG_TASK.WATCHDOG_NUM_TASKS + 1);
  assert.equal(byName.radio_rx_ok_by_antenna, undefined, 'per-antenna counts are read live, not logged');
});

test('record framing is on, and the stream version follows it', () => {
  // Turned on when STORAGE_TYPE_DIAGNOSTICS was added. If this ever reverts to 0, a reader that does not
  // know every record type silently loses the rest of every page containing an unknown one.
  assert.equal(NANDLOG_RECORD_FRAMING, 1);
  assert.equal(V2_FORMAT_VERSION_THIS_BUILD, 2);
  assert.equal(NANDLOG_FRAMING_LENGTH_BYTES, 2);
});

test('the experiment timestamp wraps at the measured point, not the advertised one', () => {
  // Measured 2026-09-11 by compiling the exact arithmetic from rtc.c:rtc_get_timestamp_diff_ms()
  // and storage_records.h:storage_experiment_ms_from_rtc(). Both compute 1000 * (now - start) in
  // uint32, and the RTC adds 10 * hundredths on top.
  //
  // The naive ceiling — UINT32_MAX / 1000 — is one second too generous, because that sub-second
  // term pushes the last second past the wrap. Pinned because "it is a 32-bit millisecond counter,
  // so about 49.7 days" is the kind of reasoning that is right to four significant figures and
  // still ships a bug.
  assert.equal(MAX_EXPERIMENT_ELAPSED_SECONDS, 4294966);
  assert.ok(MAX_EXPERIMENT_ELAPSED_SECONDS < Math.floor(TIMESTAMP_WRAP_SECONDS));

  // Every timestamp inside the limit, at any sub-second offset, stays below the sentinel.
  const worstCase = 1000 * MAX_EXPERIMENT_ELAPSED_SECONDS + 990;
  assert.ok(worstCase < NANDLOG_NO_TIMESTAMP, 'the last representable second must not reach the sentinel');
  // And the next second does not.
  assert.ok(1000 * (MAX_EXPERIMENT_ELAPSED_SECONDS + 1) + 990 > NANDLOG_NO_TIMESTAMP);

  // The limit offered to a user: whole days, and strictly inside the format ceiling.
  assert.equal(MAX_DEPLOYMENT_DAYS, 49);
  assert.equal(MAX_DEPLOYMENT_SECONDS, 49 * 86400);
  assert.ok(MAX_DEPLOYMENT_SECONDS < MAX_EXPERIMENT_ELAPSED_SECONDS);
});
