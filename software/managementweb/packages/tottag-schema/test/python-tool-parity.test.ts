// Drift detection in the other direction: this package against the Python tool it replaces.
//
// Every assertion here is one line of the disagreement audit, made executable. Two kinds:
//
//   AGREES    — the Python tool and the firmware match, and this package must match both. If this
//               fails, one of the three moved.
//   DIVERGES  — the Python tool and the firmware are known to disagree. The assertion pins the
//               divergence as it exists today, so that a fix to the Python tool shows up here as a
//               failing test rather than as nothing at all.
//
// A DIVERGES test failing is good news. It means someone fixed the Python tool and the audit needs
// updating. It does not mean this package is wrong.
//
// The Python tool is read, never written. It stays untouched.
//
// 2026-09-01: repointed. When this audit was written the record grammar was copy-pasted into three
// files; `tottag_format.py` has since consolidated it, and `tottag.py` / `parse.py` dispatch through
// it. Assertions about parsing therefore read FORMAT_PY. `tottag.py` kept its own copies of the
// record-type constants, which are now dead and stale — pinned below, because a dead constant that
// still looks authoritative is exactly the thing this file exists to notice.

import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import { dirname, join, resolve } from 'node:path';

import {
  BLE_UUID, STORAGE_TYPE, MAX_VALID_RANGE_MM, MAX_NUM_RANGING_DEVICES, EUI_NAME_MAX_LEN,
  EXPERIMENT_DETAILS_LAYOUT, USB_VID, USB_PID, GATT_SYSTEM_ID_UUID,
  TIME_BASE_CHANGE_THRESHOLD_MS, V2_RETRANSMIT_RETRY_ROUNDS, V2_RETRANSMIT_PAGE_ATTEMPTS,
  NANDLOG_MAX_RETRANSMIT_PAGES, NANDLOG_TIMESTAMP_TOLERANCE_MS, BLE_MAINTENANCE_RETRANSMIT_PAGES,
  STORAGE_DIAGNOSTIC_NUM_POOLS, DIAGNOSTICS_LAYOUT, WATCHDOG_TASK, WATCHDOG_TASK_NAMES,
  STORAGE_DIAGNOSTIC_NUM_STACKS, STORAGE_DIAGNOSTIC_FLAG_TEMPCO_AVAILABLE,
  STORAGE_DIAGNOSTIC_FLAG_TEMPCO_APPLIED, STORAGE_DIAGNOSTIC_FLAG_FIRMWARE_MODIFIED, STORAGE_DIAGNOSTIC_STACK_UNMONITORED,
  STORAGE_DIAGNOSTIC_FLAG_DIAGNOSTIC_BUILD, STORAGE_DIAGNOSTIC_FLAG_TEMPCO_DISABLED, STORAGE_RADIO_ABORT_UNMEASURED,
  STORAGE_RADIO_ABORT_NO_EVENT_TIME,
  RADIO_TIMING_LAYOUT, STORAGE_RADIO_TIMING_BANDS, STORAGE_RADIO_TIMING_FIRST_US, STORAGE_RADIO_TIMING_STEP_US,
  RADIO_ABORT_LAYOUT, SCHEDULE_CATCH_LAYOUT, ROUND_START_LAYOUT, SESSION_END_LAYOUT,
  STORAGE_SCHEDULE_CATCH_NONE, STORAGE_SCHEDULE_CATCH_UNMEASURED, STORAGE_ROUND_START_FLAG_SECOND_COPY_FAILED,
  STORAGE_ROUND_START_FLAG_COMPUTED, STORAGE_ROUND_START_FLAG_ABANDONED, STORAGE_ROUND_START_FLAG_JOIN_HEARD,
  STORAGE_ROUND_START_FLAG_JOIN_RELAYED,
  MAX_EXPERIMENT_ELAPSED_SECONDS,
  MAX_DEPLOYMENT_DAYS,
  MAX_DEPLOYMENT_SECONDS,
  BLE_MAINTENANCE_START_RADIO_TEST,
  BLE_MAINTENANCE_STOP_RADIO_TEST,
  BLE_MAINTENANCE_RADIO_TEST_HEADER_LEN,
  BLE_RADIO_STATS_VERSION,
  BLE_RADIO_STATS_FLAG_TEST_RUNNING,
  BLE_RADIO_STATS_FLAG_TEST_WAITING,
  RADIO_TEST_MAX_SECONDS,
  RADIO_STATS_LAYOUT,
  EUI_LEN,
  NUM_XMIT_ANTENNAS,
  SYSTEM_ID_EUI_OFFSETS,
} from '../src/constants.ts';
import { encodeExperimentDetails } from '../src/deployment.ts';
import { parseExperimentDetails } from '../src/log.ts';

const HERE = dirname(fileURLToPath(import.meta.url));
const DASHBOARD = resolve(HERE, '..', '..', '..', '..', 'management', 'dashboard');
const TOTTAG_PY = readFileSync(join(DASHBOARD, 'tottag.py'), 'utf8');
const PARSE_PY = readFileSync(join(DASHBOARD, 'parse.py'), 'utf8');
const FORMAT_PY = readFileSync(join(DASHBOARD, 'tottag_format.py'), 'utf8');
const PROCESSING_PY = readFileSync(join(DASHBOARD, 'processing.py'), 'utf8');
const FIRMWARE = resolve(HERE, '..', '..', '..', '..', 'firmware', 'src');
const BLUETOOTH_C = readFileSync(join(FIRMWARE, 'peripherals', 'src', 'bluetooth.c'), 'utf8');

/** Reads a module-level `NAME = <int>` assignment from a Python source file. */
function pythonInt(source: string, name: string): number {
  const match = new RegExp(`^${name}\\s*=\\s*(-?(?:0[xX][0-9a-fA-F]+|\\d+))\\s*$`, 'm').exec(source);
  assert.ok(match, `${name} not found as a module-level integer assignment`);
  return Number(match[1]);
}

/** Reads a module-level `NAME = '<string>'` assignment. */
function pythonString(source: string, name: string): string {
  const match = new RegExp(`^${name}\\s*=\\s*['"]([^'"]*)['"]\\s*$`, 'm').exec(source);
  assert.ok(match, `${name} not found as a module-level string assignment`);
  return match[1]!;
}

// --- AGREES ---------------------------------------------------------------------------------------

test('AGREES: BLE characteristic UUIDs match the firmware', () => {
  const pairs: [string, keyof typeof BLE_UUID][] = [
    ['VOLTAGE_SERVICE_UUID', 'BLE_LIVE_STATS_BATTERY_CHAR'],
    ['TIMESTAMP_SERVICE_UUID', 'BLE_LIVE_STATS_TIMESTAMP_CHAR'],
    ['FIND_MY_TOTTAG_SERVICE_UUID', 'BLE_LIVE_STATS_FINDMYTOTTAG_CHAR'],
    ['LOCATION_SERVICE_UUID', 'BLE_LIVE_STATS_RANGING_CHAR'],
    ['EXPERIMENT_SERVICE_UUID', 'BLE_MAINTENANCE_EXPERIMENT_CHAR'],
    ['MAINTENANCE_COMMAND_SERVICE_UUID', 'BLE_MAINTENANCE_COMMAND_CHAR'],
    ['MAINTENANCE_DATA_SERVICE_UUID', 'BLE_MAINTENANCE_DATA_CHAR'],
    ['MODE_SWITCH_UUID', 'BLE_MODE_SWITCH_CHAR'],
  ];
  for (const [pythonName, firmwareName] of pairs) {
    assert.equal(pythonString(TOTTAG_PY, pythonName), BLE_UUID[firmwareName].uuid, pythonName);
  }
});

test('AGREES: record type byte values match the firmware enum', () => {
  // The maintained parser. Types 7 and 8 are the ones that matter: both are written at every boot,
  // so a reader that stops at 6 cannot decode past page 0 of any current file.
  for (const name of [
    'STORAGE_TYPE_VOLTAGE', 'STORAGE_TYPE_CHARGING_EVENT', 'STORAGE_TYPE_MOTION', 'STORAGE_TYPE_RANGES',
    'STORAGE_TYPE_IMU', 'STORAGE_TYPE_BLE_SCAN', 'STORAGE_TYPE_RESET_REASON', 'STORAGE_TYPE_TIME_ANCHOR',
    'STORAGE_TYPE_DIAGNOSTICS', 'STORAGE_NUM_TYPES',
  ] as const) {
    assert.equal(pythonInt(FORMAT_PY, name), STORAGE_TYPE[name], name);
  }
});

test('DIVERGES: tottag.py keeps a stale private copy of the record types', () => {
  // It no longer parses anything — process_tottag_data dispatches to tottag_format.parse — but it
  // still defines its own STORAGE_TYPE_* constants, frozen at STORAGE_NUM_TYPES = 7. They are dead,
  // and a dead constant that still reads as authoritative is how this package itself went stale.
  // Delete them from tottag.py and this test starts failing, which is the correct outcome.
  assert.equal(pythonInt(TOTTAG_PY, 'STORAGE_NUM_TYPES'), 7);
  assert.notEqual(
    pythonInt(TOTTAG_PY, 'STORAGE_NUM_TYPES'),
    STORAGE_TYPE.STORAGE_NUM_TYPES,
    'tottag.py now agrees with the firmware; delete this assertion from the audit',
  );
  assert.match(FORMAT_PY, /def parse\(data/, 'tottag_format.py is no longer the maintained parser');
});

test('AGREES: device and label capacities match the firmware', () => {
  assert.equal(pythonInt(TOTTAG_PY, 'MAX_NUM_DEVICES'), MAX_NUM_RANGING_DEVICES);
  assert.equal(pythonInt(TOTTAG_PY, 'MAX_LABEL_LENGTH'), EUI_NAME_MAX_LEN);
});

test('AGREES: USB vendor and product IDs match the firmware', () => {
  assert.equal(pythonInt(TOTTAG_PY, 'TOTTAG_USB_VID'), USB_VID);
  assert.equal(pythonInt(TOTTAG_PY, 'TOTTAG_USB_PID'), USB_PID);
});

test('AGREES: the struct format string encodes the same 239-byte layout', () => {
  // The tool builds its format string from MAX_NUM_DEVICES and MAX_LABEL_LENGTH rather than
  // hardcoding a width, so recompute the width the same way and compare against the C layout.
  assert.match(
    TOTTAG_PY,
    /struct\.unpack\('<IIIIBB' \+ \('6B'\*MAX_NUM_DEVICES\) \+ \(\(str\(MAX_LABEL_LENGTH\)\+'s'\)\*MAX_NUM_DEVICES\) \+ 'B', data\)/,
    'unpack_experiment_details no longer has the format string this test reasons about',
  );
  const width = 4 * 4 + 1 + 1 + 6 * MAX_NUM_RANGING_DEVICES + EUI_NAME_MAX_LEN * MAX_NUM_RANGING_DEVICES + 1;
  assert.equal(width, EXPERIMENT_DETAILS_LAYOUT.size);
  assert.equal(width, 239);
});

test('AGREES: the IMU record length constant matches what the firmware writes', () => {
  // storage_task.c writes `1 + sizeof(imu_accel_data)` = 1 + 6, and the length byte counts itself.
  assert.equal(pythonInt(FORMAT_PY, 'IMU_DATA_LENGTH'), 7);
});

test('AGREES: the benign time-step threshold mirrors the firmware re-basing threshold', () => {
  // Two thresholds that mean the same thing on opposite sides of the wire: the device writes an
  // audit anchor when the offset moves by more than this, and the host reports a backward page
  // bound as alarming above it. They must not drift apart, or the host warns about steps the device
  // considered routine, or stays quiet about ones it flagged.
  assert.equal(pythonInt(FORMAT_PY, 'BENIGN_TIME_STEP_MS'), TIME_BASE_CHANGE_THRESHOLD_MS);
});

test('AGREES: the page overlap a reader lets pass mirrors the firmware\'s timestamp tolerance', () => {
  // The device keeps a record stamped up to this much before the one written ahead of it, so pages can
  // overlap by as much. A reader with a smaller figure reports routine overlaps as clock changes.
  assert.equal(pythonInt(FORMAT_PY, 'TIMESTAMP_TOLERANCE_MS'), NANDLOG_TIMESTAMP_TOLERANCE_MS);
});

test('AGREES: both readers agree on the diagnostics payload size', () => {
  // Adding a record type is the change record framing exists to make safe, but the two readers still have to
  // agree on the payload LAYOUT or they will decode different counters from the same bytes.
  assert.equal(pythonInt(FORMAT_PY, 'DIAGNOSTICS_NUM_POOLS'), STORAGE_DIAGNOSTIC_NUM_POOLS);
  assert.match(FORMAT_PY, /DIAGNOSTICS_STRUCT = struct\.Struct\('<H5sHHH5s5sBIBbIIHHHHHHHH6HBH'\)/,
    'tottag_format.py no longer has the diagnostics layout this test reasons about');
  assert.equal(pythonInt(FORMAT_PY, 'DIAGNOSTICS_NUM_STACKS'), STORAGE_DIAGNOSTIC_NUM_STACKS);
  // '<H5sHHH5s5sB' is 2 + 5 + 2 + 2 + 2 + 5 + 5 + 1, 'IBbIIHHHHHHHH6HBH' is 4 + 1 + 1 + 4 + 4 + 8 * 2 + 6 * 2 + 1 + 2
  assert.equal(2 + WATCHDOG_TASK.WATCHDOG_NUM_TASKS + 2 + 2 + 2 + 2 * STORAGE_DIAGNOSTIC_NUM_POOLS + 1 +
    4 + 1 + 1 + 4 + 4 + 8 * 2 + 2 * STORAGE_DIAGNOSTIC_NUM_STACKS + 1 + 2,
    DIAGNOSTICS_LAYOUT.size);
  for (const [name, value] of [
    ['DIAGNOSTICS_FLAG_TEMPCO_AVAILABLE', STORAGE_DIAGNOSTIC_FLAG_TEMPCO_AVAILABLE],
    ['DIAGNOSTICS_FLAG_TEMPCO_APPLIED', STORAGE_DIAGNOSTIC_FLAG_TEMPCO_APPLIED],
    ['DIAGNOSTICS_FLAG_FIRMWARE_MODIFIED', STORAGE_DIAGNOSTIC_FLAG_FIRMWARE_MODIFIED],
    ['DIAGNOSTICS_STACK_UNMONITORED', STORAGE_DIAGNOSTIC_STACK_UNMONITORED],
    ['DIAGNOSTICS_FLAG_DIAGNOSTIC_BUILD', STORAGE_DIAGNOSTIC_FLAG_DIAGNOSTIC_BUILD],
    ['DIAGNOSTICS_FLAG_TEMPCO_DISABLED', STORAGE_DIAGNOSTIC_FLAG_TEMPCO_DISABLED],
    ['STORAGE_TYPE_RADIO_ABORT', STORAGE_TYPE.STORAGE_TYPE_RADIO_ABORT],
    ['RADIO_ABORT_UNMEASURED', STORAGE_RADIO_ABORT_UNMEASURED],
  ] as const) {
    assert.equal(pythonInt(FORMAT_PY, name), value, `${name} differs between the readers`);
  }
});

test('AGREES: both readers agree on the radio abort payload', () => {
  // '<BBBhHBHBHhHH' is 1 + 1 + 1 + 2 + 2 + 1 + 2 + 1 + 2 + 2 + 2 + 2
  assert.match(FORMAT_PY, /RADIO_ABORT_STRUCT = struct\.Struct\('<BBBhHBHBHhHH'\)/,
    'tottag_format.py no longer has the radio abort layout this test reasons about');
  assert.equal(1 + 1 + 1 + 2 + 2 + 1 + 2 + 1 + 2 + 2 + 2 + 2, RADIO_ABORT_LAYOUT.size);
  assert.equal(pythonInt(FORMAT_PY, 'RADIO_ABORT_NO_EVENT_TIME'), STORAGE_RADIO_ABORT_NO_EVENT_TIME);
});

test('AGREES: both readers agree on the schedule-tracing payloads', () => {
  // '<BBiHHBBHhhH' is 1 + 1 + 4 + 2 + 2 + 1 + 1 + 2 + 2 + 2 + 2, '<HHHBBBH' is 2 + 2 + 2 + 1 + 1 + 1 + 2,
  // and '<BBBBBBHIHHHHHB' is 6 * 1 + 2 + 4 + 5 * 2 + 1
  assert.match(FORMAT_PY, /SCHEDULE_CATCH_STRUCT = struct\.Struct\('<BBiHHBBHhhH'\)/,
    'tottag_format.py no longer has the schedule catch layout this test reasons about');
  assert.match(FORMAT_PY, /ROUND_START_STRUCT = struct\.Struct\('<HHHBBBH'\)/,
    'tottag_format.py no longer has the round start layout this test reasons about');
  assert.match(FORMAT_PY, /SESSION_END_STRUCT = struct\.Struct\('<BBBBBBHIHHHHHB'\)/,
    'tottag_format.py no longer has the session end layout this test reasons about');
  assert.equal(1 + 1 + 4 + 2 + 2 + 1 + 1 + 2 + 2 + 2 + 2, SCHEDULE_CATCH_LAYOUT.size);
  assert.equal(2 + 2 + 2 + 1 + 1 + 1 + 2, ROUND_START_LAYOUT.size);
  assert.equal(6 * 1 + 2 + 4 + 5 * 2 + 1, SESSION_END_LAYOUT.size);
  for (const [name, value] of [
    ['STORAGE_TYPE_SCHEDULE_CATCH', STORAGE_TYPE.STORAGE_TYPE_SCHEDULE_CATCH],
    ['STORAGE_TYPE_ROUND_START', STORAGE_TYPE.STORAGE_TYPE_ROUND_START],
    ['STORAGE_TYPE_SESSION_END', STORAGE_TYPE.STORAGE_TYPE_SESSION_END],
    ['STORAGE_NUM_TYPES', STORAGE_TYPE.STORAGE_NUM_TYPES],
    ['SCHEDULE_CATCH_NONE', STORAGE_SCHEDULE_CATCH_NONE],
    ['SCHEDULE_CATCH_UNMEASURED', STORAGE_SCHEDULE_CATCH_UNMEASURED],
    ['ROUND_START_FLAG_SECOND_COPY_FAILED', STORAGE_ROUND_START_FLAG_SECOND_COPY_FAILED],
    ['ROUND_START_FLAG_COMPUTED', STORAGE_ROUND_START_FLAG_COMPUTED],
    ['ROUND_START_FLAG_ABANDONED', STORAGE_ROUND_START_FLAG_ABANDONED],
    ['ROUND_START_FLAG_JOIN_HEARD', STORAGE_ROUND_START_FLAG_JOIN_HEARD],
    ['ROUND_START_FLAG_JOIN_RELAYED', STORAGE_ROUND_START_FLAG_JOIN_RELAYED],
  ] as const) {
    assert.equal(pythonInt(FORMAT_PY, name), value, `${name} differs between the readers`);
  }
});

test('AGREES: both tools run a live radio test with the firmware\'s commands and read its counters in its layout', () => {
  // The dashboard's live radio check (radio_check.py) writes these commands and decodes these reads itself;
  // test_radio_check.py holds its verdicts to this package's through test/fixtures/radio-check-parity.json
  for (const [pythonName, firmwareName] of [
    ['BLE_TIMESTAMP_UUID', 'BLE_LIVE_STATS_TIMESTAMP_CHAR'],
    ['BLE_RANGES_UUID', 'BLE_LIVE_STATS_RANGING_CHAR'],
    ['BLE_RADIO_STATS_UUID', 'BLE_LIVE_STATS_RADIO_CHAR'],
    ['BLE_MAINTENANCE_COMMAND_UUID', 'BLE_MAINTENANCE_COMMAND_CHAR'],
  ] as const) {
    assert.equal(pythonString(FORMAT_PY, pythonName), BLE_UUID[firmwareName].uuid, pythonName);
  }
  assert.equal(pythonString(FORMAT_PY, 'BLE_SYSTEM_ID_UUID'), GATT_SYSTEM_ID_UUID);
  for (const [name, value] of [
    ['BLE_MAINTENANCE_START_RADIO_TEST', BLE_MAINTENANCE_START_RADIO_TEST],
    ['BLE_MAINTENANCE_STOP_RADIO_TEST', BLE_MAINTENANCE_STOP_RADIO_TEST],
    ['BLE_MAINTENANCE_RADIO_TEST_HEADER_LEN', BLE_MAINTENANCE_RADIO_TEST_HEADER_LEN],
    ['RADIO_TEST_MAX_SECONDS', RADIO_TEST_MAX_SECONDS],
    ['RADIO_STATS_VERSION', BLE_RADIO_STATS_VERSION],
    ['RADIO_STATS_FLAG_TEST_RUNNING', BLE_RADIO_STATS_FLAG_TEST_RUNNING],
    ['RADIO_STATS_FLAG_TEST_WAITING', BLE_RADIO_STATS_FLAG_TEST_WAITING],
    ['EUI_LEN', EUI_LEN],
    ['NUM_XMIT_ANTENNAS', NUM_XMIT_ANTENNAS],
  ] as const) {
    assert.equal(pythonInt(FORMAT_PY, name), value, `${name} differs between the tools`);
  }
  // The struct format the firmware's ble_radio_stats_t implies, field by field, against the one Python unpacks with
  const code: Record<number, string> = { 1: 'B', 2: 'H', 4: 'I' };
  const implied = `<${RADIO_STATS_LAYOUT.fields.map((field) => `${field.size === field.elementSize ? '' : field.size / field.elementSize}${code[field.elementSize]}`).join('')}`;
  const declared = /^RADIO_STATS_STRUCT = struct\.Struct\('([^']+)'\)/m.exec(FORMAT_PY);
  assert.ok(declared, 'tottag_format.py no longer declares RADIO_STATS_STRUCT');
  assert.equal(declared[1], implied);
  const offsets = /^SYSTEM_ID_EUI_OFFSETS = \(([^)]*)\)/m.exec(FORMAT_PY);
  assert.ok(offsets, 'tottag_format.py no longer declares SYSTEM_ID_EUI_OFFSETS');
  assert.deepEqual(offsets[1]!.split(',').map((v) => Number(v.trim())), [...SYSTEM_ID_EUI_OFFSETS]);
});

test('AGREES: both readers agree on the radio timing payload', () => {
  // '<HHHhHHH7HHBB' is 3 * 2 + 2 + 3 * 2 + 7 * 2 + 2 + 2 * 1
  assert.match(FORMAT_PY, /RADIO_TIMING_STRUCT = struct\.Struct\('<HHHhHHH(\d+)HHBB'\)/,
    'tottag_format.py no longer has the radio timing layout this test reasons about');
  assert.equal(Number(/RADIO_TIMING_STRUCT = struct\.Struct\('<HHHhHHH(\d+)HHBB'\)/.exec(FORMAT_PY)![1]), STORAGE_RADIO_TIMING_BANDS);
  assert.equal(3 * 2 + 2 + 3 * 2 + STORAGE_RADIO_TIMING_BANDS * 2 + 2 + 2, RADIO_TIMING_LAYOUT.size);
  for (const [name, value] of [
    ['STORAGE_TYPE_RADIO_TIMING', STORAGE_TYPE.STORAGE_TYPE_RADIO_TIMING],
    ['RADIO_TIMING_BANDS', STORAGE_RADIO_TIMING_BANDS],
    ['RADIO_TIMING_FIRST_US', STORAGE_RADIO_TIMING_FIRST_US],
    ['RADIO_TIMING_STEP_US', STORAGE_RADIO_TIMING_STEP_US],
  ] as const) {
    assert.equal(pythonInt(FORMAT_PY, name), value, `${name} differs between the readers`);
  }
});

test('AGREES: the watchdog task names match the firmware order', () => {
  // Both readers index a diagnostics record's late-episode array by watchdog_task_t, so both must spell and
  // order the tasks the same way or the same log reads differently through each.
  const match = /WATCHDOG_TASK_NAMES = \[([^\]]*)\]/.exec(FORMAT_PY);
  assert.ok(match, 'tottag_format.py no longer declares WATCHDOG_TASK_NAMES');
  const pythonNames = [...match[1]!.matchAll(/'([^']+)'/g)].map((m) => m[1]);
  assert.deepEqual(pythonNames, [...WATCHDOG_TASK_NAMES]);
});

test('AGREES: the retransmission retry cap matches the documented design', () => {
  // Per-page attempts are the give-up policy in both tools; the round count is only a loop guard.
  assert.equal(pythonInt(TOTTAG_PY, 'MAX_PAGE_ATTEMPTS'), V2_RETRANSMIT_PAGE_ATTEMPTS);
  assert.equal(pythonInt(TOTTAG_PY, 'MAX_REPAIR_ROUNDS'), V2_RETRANSMIT_RETRY_ROUNDS);
  assert.equal(pythonInt(TOTTAG_PY, 'MAINTENANCE_RETRANSMIT_PAGES'), BLE_MAINTENANCE_RETRANSMIT_PAGES);
  // Both tools must clamp a request to what the device can actually hold, or the overflow is dropped
  // with nothing reported -- which is how a 930-page repair silently became a 256-page one.
  assert.equal(pythonInt(TOTTAG_PY, 'DEVICE_RETRANSMIT_CAPACITY'), NANDLOG_MAX_RETRANSMIT_PAGES);
});

// --- DIVERGES -------------------------------------------------------------------------------------

test('AGREES: both readers keep every range the firmware was willing to store', () => {
  // Was Finding B.3: the Python tool capped at 16 m while the firmware stores to 32 m, so it silently
  // discarded readings the device had already accepted. Both now use the firmware's own limit.
  assert.equal(pythonInt(FORMAT_PY, 'MAX_RANGING_DISTANCE_MM'), MAX_VALID_RANGE_MM);
});


test('DIVERGES: the System ID characteristic is defined and never read', () => {
  // Finding B.8. This is the only BLE route to device identity that works in a browser, since
  // Web Bluetooth cannot see MAC addresses.
  assert.equal(pythonString(TOTTAG_PY, 'DEVICE_ID_UUID'), GATT_SYSTEM_ID_UUID);
  const uses = [...TOTTAG_PY.matchAll(/\bDEVICE_ID_UUID\b/g)].length;
  assert.equal(uses, 1, 'DEVICE_ID_UUID is now referenced beyond its definition; update the audit');
});

test('AGREES: MOTION bodies outside {0, 1} are rejected by both readers', () => {
  // Was a DIVERGES finding while the firmware enum still held NOT_ON_CHARGER and ON_CHARGER. The
  // firmware deleted them, so restricting the body to {0,1} is now correct rather than lossy, and
  // both readers agree with the device.
  assert.match(
    FORMAT_PY,
    /if data\[i \+ 5\] in \(0, 1\):/,
    'tottag_format.py no longer restricts MOTION bodies to {0, 1}; update the audit',
  );
});

test('DIVERGES: charger transitions are inferred from voltage peaks', () => {
  // Finding B.1 and B.2. The information exists as motion codes 2 and 3 but never reaches storage,
  // so the tool reconstructs it heuristically, with a tuning parameter it ignores.
  assert.match(PROCESSING_PY, /def get_off_and_on_charger_times\(data, label, peak_width=50,/);
  assert.match(PROCESSING_PY, /find_peaks\(voltages, width=50\)/, 'peak_width is now used; update the audit');
  assert.match(PROCESSING_PY, /find_peaks\(-voltages, width=50\)/);
});

test('DIVERGES: parse failures are swallowed whole, but only on the v1 path now', () => {
  // Finding B.12, narrowed. The bare `except Exception: pass` survives in parse_v1, deliberately, so
  // that archived files keep yielding what they always did. parse_v2 does not inherit it: it reports
  // per-page instead. This package's v1 reader makes the same choice, so the divergence is confined
  // to a path neither side can improve without changing what archived files decode to.
  assert.match(FORMAT_PY, /except Exception:\s*\n\s*pass/, 'parse_v1 no longer swallows; update the audit');
  const v2 = FORMAT_PY.slice(FORMAT_PY.indexOf('def parse_v2('));
  assert.ok(!/except Exception:\s*\n\s*pass/.test(v2), 'parse_v2 has grown a bare except; that is a regression');
});

test('DIVERGES: records dated after now are still rejected', () => {
  // Finding B.12. A tag whose RTC ran ahead produces records the reader discards. The RTC runs ~210
  // ppm fast (~18 s/day), so a long deployment downloaded promptly can genuinely produce timestamps
  // a few seconds in the future — this is not hypothetical. On the v2 path the record is stepped
  // over rather than resynchronised through, so it costs one record instead of a page.
  assert.match(FORMAT_PY, /timestamp <= now/, 'tottag_format.py no longer bounds records by now; update the audit');
});

test('DIVERGES: ranges are decoded unsigned though the firmware writes int16', () => {
  // Finding B.10. Latent rather than active: compute_ranges clamps the median to >= 0, so no stored
  // range is negative today. It would become active if that clamp were removed. This package makes
  // the same choice, deliberately, so that the two readers agree on archived files.
  assert.match(
    FORMAT_PY,
    /struct\.unpack\('<H', data\[i \+ 7 \+ \(j \* 3\):i \+ 9 \+ \(j \* 3\)\]\)/,
    'tottag_format.py; update the audit',
  );
});

test('AGREES: the record grammar has one maintained implementation', () => {
  // Was "three copies of the record parser exist". Storage_Redesign.md section 9.3 consolidated
  // them: tottag.py and parse.py now dispatch to tottag_format.parse, and experimental_tottag.py is
  // frozen at v1 with a header comment saying so. Only the frozen one still carries its own grammar.
  const dispatchers = ['tottag.py', 'parse.py'].filter((file) =>
    readFileSync(join(DASHBOARD, file), 'utf8').includes('tottag_format.parse('),
  );
  assert.deepEqual(dispatchers, ['tottag.py', 'parse.py'], 'a dashboard entry point stopped dispatching');
  const frozen = readFileSync(join(DASHBOARD, 'experimental_tottag.py'), 'utf8');
  assert.match(frozen, /FROZEN AT LOG FORMAT v1/, 'experimental_tottag.py lost its frozen marker');
});

test('AGREES: both tools enforce the same measured deployment limit', () => {
  // Was a DIVERGES entry: the Python GUI capped a deployment at 21 days, believed to be the uint32
  // millisecond timestamp limit. It is not — the real ceiling is 4294966 s and 21 days left 28.71
  // days unused. Both tools now derive the limit from the same measurement, so this became an
  // AGREES, and it is asserted rather than assumed because two independent copies of a number that
  // nothing compares is how the 21-day figure survived as long as it did.
  assert.equal(pythonInt(FORMAT_PY, 'MAX_EXPERIMENT_ELAPSED_SECONDS'), MAX_EXPERIMENT_ELAPSED_SECONDS);
  assert.equal(pythonInt(FORMAT_PY, 'MAX_DEPLOYMENT_DAYS'), MAX_DEPLOYMENT_DAYS);
  assert.equal(pythonInt(FORMAT_PY, 'MAX_DEPLOYMENT_SECONDS'), MAX_DEPLOYMENT_SECONDS);

  // And the old literal is gone from the GUI, not merely shadowed.
  assert.doesNotMatch(TOTTAG_PY, /1814400/, 'the 21-day literal is still in tottag.py');
  assert.doesNotMatch(TOTTAG_PY, /21 days/, 'the 21-day message is still in tottag.py');
});

test('AGREES: the Python tool now normalises daily times into range', () => {
  // Was a silent data-loss bug. pack_datetime subtracted the UTC offset without a modulo, so the
  // value left 0..86399 in both directions: negative east of UTC (struct.pack raised outright) and
  // above 86400 west of it (no error at all). The overflow case is the one that cost data — a
  // 14:00-03:00 Central window packed an end of 97200, and since rtc_get_time_of_day() never
  // reaches 97200 the device recorded 14:00 to midnight instead. Seen in a real 81-hour log.
  assert.match(TOTTAG_PY, /timestamp %= 86400/, 'pack_datetime no longer normalises daily times');

  // And this package encodes the same way, so the two tools cannot disagree about a window.
  const eastOfUtc = {
    startTime: 1_788_300_000, endTime: 1_788_300_000 + 86400, timezone: 'Australia/Sydney',
    useDailyTimes: true, dailyStartTime: 7 * 3600 - 36000, dailyEndTime: 22 * 3600 - 36000,
    devices: [{ eui: Uint8Array.of(1, 2, 3, 4, 5, 6), label: 'a' }],
  };
  const decoded = parseExperimentDetails(encodeExperimentDetails(eastOfUtc));
  for (const value of [decoded.dailyStartTime, decoded.dailyEndTime]) {
    assert.ok(value >= 0 && value < 86400, `daily time ${value} is out of range`);
  }
});

test('AGREES: both tools match the advertised name by prefix, because the firmware appends a UID', () => {
  // The firmware advertises a fixed prefix plus the device's own short UID -- 'TotTag-3E' -- so that a
  // host chooser can tell one tag from another. A host matching the WHOLE name therefore finds nothing
  // at all, which is what tottag.py did from the commit that introduced the suffix until 2026-09-15.
  const chars = /adv_name_prefix\[\]\s*=\s*\{([^}]*)\}/.exec(BLUETOOTH_C);
  assert.ok(chars, "the firmware's advertised name prefix could not be located");
  const prefix = [...chars[1]!.matchAll(/'(.)'/g)].map((m) => m[1]).join('');
  assert.equal(prefix, 'TotTag');

  // The suffix is what makes equality wrong, so assert it is really there rather than trusting the name.
  assert.match(BLUETOOTH_C, /adv_local_name\[sizeof\(adv_name_prefix\)\] = '-';/,
    'the firmware no longer appends a UID; equality matching would be safe again');

  assert.equal(pythonString(TOTTAG_PY, 'TOTTAG_ADVERTISED_NAME_PREFIX'), prefix);
  assert.match(TOTTAG_PY, /local_name\.startswith\(TOTTAG_ADVERTISED_NAME_PREFIX\)/,
    'tottag.py is not matching the advertised name by prefix');
  assert.doesNotMatch(TOTTAG_PY, /local_name == /,
    'tottag.py is matching the advertised name by equality, which finds no device');
});

test('AGREES: both tools save the REPAIRED stream, not the transfer that lost pages', () => {
  // Asking the device to resend a page and then writing a file that still lacks it means the recovery
  // lives only in whatever the tool printed at the time, and re-reading the saved log reports holes that
  // had already been fixed. Both tools now splice repairs into the stream before it is written, and both
  // do it by substituting the device's whole page frame -- header, timestamps, record count and CRC --
  // so no field is ever rewritten and a header cannot end up disagreeing with the payload under it.
  assert.match(FORMAT_PY, /^def merge_repairs\(data, repairs\):/m,
    'tottag_format.py has no merge_repairs');
  assert.match(FORMAT_PY, /^def extract_page_frames\(data\):/m,
    'tottag_format.py cannot extract whole page frames, so it cannot substitute one');

  // The download path merges rather than carrying repairs alongside the stream.
  assert.match(TOTTAG_PY, /merged = tottag_format\.merge_repairs\(self\.original_stream, frames\)/,
    'tottag.py is not merging repairs into the stream it saves');
  assert.doesNotMatch(TOTTAG_PY, /tottag_format\.parse\([^)]*self\.repairs\)/,
    'tottag.py still parses with a separate repairs map, so the file it saves can differ from what it reports');

  // The behavioural half of this claim is in log.test.ts, which merges a damaged fixture and re-parses
  // it. Verified once by hand across the language boundary: the same fixture damaged the same way and
  // repaired from the same source gives byte-identical output from both implementations -- 26,003 bytes,
  // CRC-32 0x34af08e3, zero CRC failures and zero holes in both readers.
});
