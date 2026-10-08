import { test } from 'node:test';
import assert from 'node:assert/strict';

import {
  encodeExperimentDetails, encodeNewExperimentCommand, formatEui, hasErrors, shortUid,
  shortUidConflicts, validateDeployment, type DeploymentConfig,
} from '../src/deployment.ts';
import { parseExperimentDetails } from '../src/log.ts';
import {
  BLE_MAINTENANCE_NEW_EXPERIMENT, EUI_NAME_MAX_LEN, MAX_DEPLOYMENT_SECONDS, MAX_NUM_RANGING_DEVICES,
} from '../src/constants.ts';

const eui = (...bytes: number[]) => Uint8Array.from(bytes);
const base: DeploymentConfig = {
  startTime: 1_788_300_000,
  endTime: 1_788_300_000 + 3 * 86400,
  timezone: 'America/Chicago',
  useDailyTimes: false,
  dailyStartTime: 0,
  dailyEndTime: 0,
  devices: [
    { eui: eui(0x11, 0x22, 0x33, 0x44, 0x55, 0x66), label: '10043_S1' },
    { eui: eui(0x12, 0x22, 0x33, 0x44, 0x55, 0x66), label: '10043_CG1' },
  ],
};

test('a well-formed deployment has no errors', () => {
  assert.equal(hasErrors(validateDeployment(base)), false);
});

test('two tags sharing a low EUI byte is an error, not a warning', () => {
  // The firmware identifies a peer by that byte alone: compute_ranges writes it as the single-byte
  // id in a RANGES record, and the BLE scan list stores discovered_devices[i][0]. Two tags sharing
  // it produce data that cannot be separated afterwards, so this has to block the write.
  const clashing: DeploymentConfig = {
    ...base,
    devices: [
      { eui: eui(0x7a, 0x01, 0x02, 0x03, 0x04, 0x05), label: 'child' },
      { eui: eui(0x7a, 0xff, 0xee, 0xdd, 0xcc, 0xbb), label: 'caregiver' },
    ],
  };
  const problems = validateDeployment(clashing);
  const conflict = problems.find((problem) => problem.code === 'short-uid-conflict');
  assert.ok(conflict, 'a low-byte collision must be reported');
  assert.equal(conflict.severity, 'error');
  assert.deepEqual([...conflict.devices], [0, 1], 'both devices should be highlighted');
  assert.match(conflict.message, /7A/, 'the message should name the colliding byte');
  assert.match(conflict.message, /child/);
  assert.match(conflict.message, /caregiver/);
  // It is repeated once per affected row, so it has to stay short enough not to bury the row.
  assert.ok(conflict.message.length < 240, `too long to repeat per row: ${conflict.message.length} chars`);
  assert.ok(hasErrors(problems));
});

test('full addresses that differ only above the low byte still collide', () => {
  // The trap this guards: the addresses look completely different in the UI, because the UI shows
  // them most-significant byte first, and the byte that matters is the one printed last.
  const a = eui(0x40, 0x00, 0x00, 0x00, 0x00, 0x01);
  const b = eui(0x40, 0xff, 0xff, 0xff, 0xff, 0xfe);
  assert.notEqual(formatEui(a), formatEui(b), 'they display differently');
  assert.equal(shortUid(a), shortUid(b), 'and still collide');
  assert.equal(shortUidConflicts([{ eui: a, label: 'a' }, { eui: b, label: 'b' }]).length, 1);
});

test('a three-way collision is reported once, not as three pairs', () => {
  const devices = [0, 1, 2].map((i) => ({ eui: eui(0x55, i, 0, 0, 0, 0), label: `tag${i}` }));
  const conflicts = shortUidConflicts(devices);
  assert.equal(conflicts.length, 1);
  assert.deepEqual(conflicts[0]!.devices, [0, 1, 2]);
});

test('no collision when low bytes differ, however similar the rest', () => {
  const devices = [
    { eui: eui(0x01, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa), label: 'a' },
    { eui: eui(0x02, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa), label: 'b' },
  ];
  assert.deepEqual(shortUidConflicts(devices), []);
});

test('duplicate labels are an error and blank ones only a warning', () => {
  const problems = validateDeployment({
    ...base,
    devices: [
      { eui: eui(1, 0, 0, 0, 0, 0), label: 'same' },
      { eui: eui(2, 0, 0, 0, 0, 0), label: 'same' },
      { eui: eui(3, 0, 0, 0, 0, 0), label: '' },
    ],
  });
  assert.equal(problems.find((p) => p.code === 'duplicate-label')?.severity, 'error');
  assert.equal(problems.find((p) => p.code === 'label-missing')?.severity, 'warning');
});

test('the duration limit is the measured one', () => {
  const tooLong = { ...base, endTime: base.startTime + MAX_DEPLOYMENT_SECONDS + 1 };
  assert.ok(validateDeployment(tooLong).some((p) => p.code === 'too-long'));
  const justFits = { ...base, endTime: base.startTime + MAX_DEPLOYMENT_SECONDS };
  assert.ok(!validateDeployment(justFits).some((p) => p.code === 'too-long'));
});

test('more devices than the firmware can hold is an error', () => {
  const many = Array.from({ length: MAX_NUM_RANGING_DEVICES + 1 }, (_, i) => ({
    eui: eui(i + 1, 0, 0, 0, 0, 0),
    label: `t${i}`,
  }));
  assert.ok(validateDeployment({ ...base, devices: many }).some((p) => p.code === 'too-many-devices'));
});

test('encoded details round-trip through the log reader', () => {
  // The strongest available check on the encoder: decode it with the same function that reads a
  // real device's read-back, so encoder and decoder cannot drift into agreeing with each other and
  // disagreeing with the firmware.
  const bytes = encodeExperimentDetails(base);
  assert.equal(bytes.length, 239);
  const decoded = parseExperimentDetails(bytes);
  assert.equal(decoded.experimentStartTime, base.startTime);
  assert.equal(decoded.experimentEndTime, base.endTime);
  assert.equal(decoded.numDevices, 2);
  assert.equal(decoded.useDailyTimes, false);
  assert.equal(decoded.isTerminated, false);
  assert.deepEqual([...decoded.uids[0]!], [...base.devices[0]!.eui]);
  assert.equal(decoded.labels[0], '10043_S1');
  assert.equal(decoded.labels[1], '10043_CG1');
});

test('daily times are normalised into 0..86399 instead of going negative', () => {
  // The Python tool computed local-seconds-minus-UTC-offset and packed it unsigned, which raised
  // struct.error outright anywhere east of UTC. The firmware compares against rtc_get_time_of_day(),
  // which is always in range, and handles a window that wraps midnight itself.
  const eastOfUtc: DeploymentConfig = {
    ...base,
    useDailyTimes: true,
    dailyStartTime: 7 * 3600 - 36000,   // 07:00 in UTC+10
    dailyEndTime: 22 * 3600 - 36000,
  };
  const decoded = parseExperimentDetails(encodeExperimentDetails(eastOfUtc));
  assert.equal(decoded.dailyStartTime, 21 * 3600, 'should wrap to 21:00 UTC, not go negative');
  assert.equal(decoded.dailyEndTime, 12 * 3600);
  assert.ok(decoded.dailyStartTime >= 0 && decoded.dailyStartTime < 86400);
});

test('a label longer than the field is cut on a byte boundary and blocked by validation', () => {
  const long = 'x'.repeat(EUI_NAME_MAX_LEN + 4);
  assert.ok(validateDeployment({ ...base, devices: [{ eui: eui(9, 0, 0, 0, 0, 0), label: long }] })
    .some((p) => p.code === 'label-too-long'));
  // Encoding still must not overflow into the next device's field.
  const decoded = parseExperimentDetails(
    encodeExperimentDetails({ ...base, devices: [{ eui: eui(9, 0, 0, 0, 0, 0), label: long }] }),
  );
  assert.equal(decoded.labels[0]!.length, EUI_NAME_MAX_LEN);
});

test('the command payload is the opcode followed by the struct', () => {
  const command = encodeNewExperimentCommand(base);
  assert.equal(command.length, 240);
  assert.equal(command[0], BLE_MAINTENANCE_NEW_EXPERIMENT);
  assert.deepEqual([...command.subarray(1)], [...encodeExperimentDetails(base)]);
});
