// The live radio check: the commands that start and stop a radio test, what a device streams during one, and the
// recorder that turns those streams into the summary analyseRadio() judges. The recorder is held to the same
// verdicts the log path produces, since a live test that disagreed with the logs of the same run would be worse
// than no live test.

import { test } from 'node:test';
import assert from 'node:assert/strict';

import {
  BLE_MAINTENANCE_START_RADIO_TEST, BLE_MAINTENANCE_STOP_RADIO_TEST, BLE_RADIO_STATS_FLAG_TEST_RUNNING,
  BLE_RADIO_STATS_FLAG_TEST_WAITING, BLE_RADIO_STATS_VERSION, EUI_LEN, NUM_XMIT_ANTENNAS, RADIO_STATS_LAYOUT,
  RADIO_TEST_MAX_SECONDS, SCHEDULE_ROLE,
} from '../src/constants.ts';
import {
  LIVE_BIN_MS, LiveRadioRecorder, decodeRadioStats, decodeRangeResults, encodeRadioTestStartCommand, encodeRadioTestStopCommand,
  type RadioStats,
} from '../src/liveRadio.ts';
import { analyseRadio, ROUNDS_PER_MINUTE } from '../src/radioCheck.ts';

const eui = (low: number) => Uint8Array.of(low, 0x11, 0x22, 0x33, 0x44, 0x55);

test('a start command carries both times, the count and every EUI, in the firmware\'s layout', () => {
  const command = encodeRadioTestStartCommand(1_791_300_000, 1_791_300_600, [eui(0x02), eui(0xf3)]);
  const view = new DataView(command.buffer);
  assert.equal(command[0], BLE_MAINTENANCE_START_RADIO_TEST);
  assert.equal(view.getUint32(1, true), 1_791_300_000);
  assert.equal(view.getUint32(5, true), 1_791_300_600);
  assert.equal(command[9], 2);
  assert.deepEqual([...command.subarray(10, 10 + EUI_LEN)], [...eui(0x02)]);
  assert.deepEqual([...command.subarray(10 + EUI_LEN)], [...eui(0xf3)]);
  assert.deepEqual([...encodeRadioTestStopCommand()], [BLE_MAINTENANCE_STOP_RADIO_TEST]);
});

test('a start command the firmware would refuse is refused here first', () => {
  assert.throws(() => encodeRadioTestStartCommand(100, 100, [eui(1)]), /must end after it starts/);
  assert.throws(() => encodeRadioTestStartCommand(100, 100 + RADIO_TEST_MAX_SECONDS + 1, [eui(1)]), /at most/);
  assert.throws(() => encodeRadioTestStartCommand(100, 200, []), /between 1 and/);
  assert.throws(() => encodeRadioTestStartCommand(100, 200, [Uint8Array.of(1, 2)]), /6 bytes/);
});

function statsBytes(values: Partial<Record<string, number | number[]>>): Uint8Array {
  const bytes = new Uint8Array(RADIO_STATS_LAYOUT.size);
  const view = new DataView(bytes.buffer);
  const all = { version: BLE_RADIO_STATS_VERSION, ...values };
  for (const field of RADIO_STATS_LAYOUT.fields) {
    const value = all[field.name as keyof typeof all];
    if (value === undefined) continue;
    const items = Array.isArray(value) ? value : [value];
    items.forEach((item, index) => {
      const at = field.offset + index * field.elementSize;
      if (field.elementSize === 1) view.setUint8(at, item);
      else if (field.elementSize === 2) view.setUint16(at, item, true);
      else view.setUint32(at, item, true);
    });
  }
  return bytes;
}

test('radio statistics decode every field, and a layout from another version is refused', () => {
  const stats = decodeRadioStats(statsBytes({
    role: SCHEDULE_ROLE.ROLE_PARTICIPANT, schedule_size: 9, flags: BLE_RADIO_STATS_FLAG_TEST_RUNNING, test_seconds_left: 412,
    rounds_scheduled: 380, rounds_ranged: 377, rx_ok: 18_000, rx_failed: 240,
    rx_ok_by_antenna: [6000, 6000, 6000], rx_failed_by_antenna: [80, 70, 90],
    tx_late: 1, rx_arm_late: 2, isr_over_budget: 3, wake_max_us: 2196, wake_failures: 0, antenna: 2, antenna_changes: 3,
  }));
  assert.equal(stats.role, 'ROLE_PARTICIPANT');
  assert.deepEqual([stats.scheduleSize, stats.testRunning, stats.testWaiting, stats.testSecondsLeft], [9, true, false, 412]);
  assert.deepEqual([stats.roundsScheduled, stats.roundsRanged, stats.rxOk, stats.rxFailed], [380, 377, 18_000, 240]);
  assert.deepEqual([stats.rxOkByAntenna, stats.rxFailedByAntenna], [[6000, 6000, 6000], [80, 70, 90]]);
  assert.deepEqual([stats.txLate, stats.rxArmLate, stats.isrOverBudget, stats.wakeMaxUs, stats.wakeFailures], [1, 2, 3, 2196, 0]);
  assert.deepEqual([stats.antenna, stats.antennaChanges], [2, 3]);
  assert.equal(decodeRadioStats(statsBytes({ flags: BLE_RADIO_STATS_FLAG_TEST_WAITING })).testWaiting, true);
  assert.throws(() => decodeRadioStats(statsBytes({ version: BLE_RADIO_STATS_VERSION + 1 })), /layout/);
  assert.throws(() => decodeRadioStats(new Uint8Array(RADIO_STATS_LAYOUT.size - 1)), /bytes/);
});

test('a ranges notification decodes, and one cut short by a small Bluetooth packet says so', () => {
  const whole = decodeRangeResults(Uint8Array.of(2, 0x0b, 0xdc, 0x05, 0x3e, 0xd0, 0x07));
  assert.deepEqual([...whole.ranges], [[0x0b, 1500], [0x3e, 2000]]);
  assert.equal(whole.truncated, false);
  const cut = decodeRangeResults(Uint8Array.of(3, 0x0b, 0xdc, 0x05, 0x3e, 0xd0));
  assert.deepEqual([...cut.ranges], [[0x0b, 1500]]);
  assert.equal(cut.truncated, true);
  assert.equal(decodeRangeResults(Uint8Array.of(0)).ranges.size, 0);
});

// --- The recorder --------------------------------------------------------------------------------------------------

const START = 1_791_300_000;
const at = (seconds: number) => (START + seconds) * 1000;

function stats(overrides: Partial<RadioStats>): RadioStats {
  return {
    role: 'ROLE_PARTICIPANT', scheduleSize: 3, testRunning: true, testWaiting: false, testSecondsLeft: 0,
    roundsScheduled: 0, roundsRanged: 0, rxOk: 0, rxFailed: 0,
    rxOkByAntenna: new Array(NUM_XMIT_ANTENNAS).fill(0), rxFailedByAntenna: new Array(NUM_XMIT_ANTENNAS).fill(0),
    txLate: 0, rxArmLate: 0, isrOverBudget: 0, wakeMaxUs: 0, wakeFailures: 0, antenna: 0, antennaChanges: 0, ...overrides,
  };
}

const recorderFor = (selfUid: number) =>
  new LiveRadioRecorder({ startTime: START, deploymentUids: [0x02, 0x0b, 0x3e], deploymentLabels: ['02', '0B', '3E'], selfUid });

test('rounds come from the device\'s own counter, spread across the 15-second bins between reads', () => {
  const recorder = recorderFor(0x02);
  recorder.addRanges(at(4), new Map([[0x0b, 1500]]));
  recorder.addStats(at(30), stats({ roundsRanged: 60 }));
  recorder.addStats(at(90), stats({ roundsRanged: 180 }));
  const summary = recorder.summary();
  // The first read is only a starting point, then 120 rounds over 30-90 s: 30 in each of four 15-second bins, and
  // only those reads' span is judged
  assert.equal(summary.binMs, LIVE_BIN_MS);
  assert.deepEqual(summary.rowsByBin, [[2, 30], [3, 30], [4, 30], [5, 30]]);
  assert.deepEqual([summary.firstMs, summary.lastMs], [30_000, 90_000]);
  assert.equal(summary.selfUid, 0x02);
  assert.equal(summary.experimentStartTime, START);
});

test('notifications Bluetooth dropped are scaled back up from the round counter', () => {
  const recorder = recorderFor(0x02);
  // A full minute of rounds counted by the device, but only every other notification arrived
  recorder.addStats(at(0), stats({}));
  for (let round = 0; round < ROUNDS_PER_MINUTE; round += 2) {
    recorder.addRanges(at(round / 2), new Map([[0x0b, 1500], [0x3e, 2000]]));
  }
  recorder.addStats(at(59.9), stats({ roundsRanged: ROUNDS_PER_MINUTE }));
  // Every 15-second bin had 15 notifications of the 30 rounds the counter says it ranged in
  const toB = recorder.summary().peers.find((peer) => peer.uid === 0x0b)!;
  assert.deepEqual(toB.bins, [[0, 30, 1500, 0], [1, 30, 1500, 0], [2, 30, 1500, 0], [3, 30, 1500, 0]]);
});

test('notifications Bluetooth held back and delivered in a bunch in the next bin are scaled down, not counted twice', () => {
  const recorder = recorderFor(0x02);
  recorder.addStats(at(0), stats({}));
  for (let round = 0; round < ROUNDS_PER_MINUTE; round += 1) {
    // Rounds 26-29, at 13-14.5 s, reach the host together at 16 s
    recorder.addRanges(at(round >= 26 && round < 30 ? 16 : round / 2), new Map([[0x0b, 1500]]));
  }
  recorder.addStats(at(60), stats({ roundsRanged: ROUNDS_PER_MINUTE }));
  // 26 notifications for bin 0's 30 rounds and 34 for bin 1's: both still ranged to 0B in exactly 30
  const toB = recorder.summary().peers.find((peer) => peer.uid === 0x0b)!;
  assert.deepEqual(toB.bins, [[0, 30, 1500, 0], [1, 30, 1500, 0], [2, 30, 1500, 0], [3, 30, 1500, 0]]);
});

test('a device that restarts mid-test keeps the counts it had reached', () => {
  const recorder = recorderFor(0x02);
  recorder.addStats(at(0), stats({}));
  recorder.addStats(at(20), stats({ roundsRanged: 40, rxOk: 1000, rxFailed: 10, rxOkByAntenna: [400, 300, 300], rxFailedByAntenna: [4, 3, 3] }));
  recorder.addStats(at(40), stats({ roundsRanged: 30, rxOk: 600, rxFailed: 6, rxOkByAntenna: [200, 200, 200], rxFailedByAntenna: [2, 2, 2] }));
  const summary = recorder.summary();
  // 40 rounds over 0-20 s (30 + 10), then 30 more over 20-40 s after the restart (15 + 15)
  assert.deepEqual(summary.rowsByBin, [[0, 30], [1, 25], [2, 15]]);
  assert.deepEqual([summary.diagnostics!.rxOk, summary.diagnostics!.rxFailed], [1600, 16]);
  assert.deepEqual([summary.diagnostics!.rxOkByAntenna, summary.diagnostics!.rxFailedByAntenna], [[600, 500, 500], [6, 5, 5]]);
});

test('a live test reaches the same verdicts as logs would: a dead antenna is named', () => {
  // Three devices for three minutes, every round ranging to both others; 3E's third antenna fails half its receives
  const recorders = new Map([0x02, 0x0b, 0x3e].map((uid) => [uid, recorderFor(uid)]));
  const minutes = 3;
  for (const recorder of recorders.values()) recorder.addStats(at(0), stats({}));
  for (let round = 0; round < minutes * ROUNDS_PER_MINUTE; round += 1) {
    for (const [uid, recorder] of recorders) {
      const peers = new Map([0x02, 0x0b, 0x3e].filter((peer) => peer !== uid).map((peer) => [peer, 1500 + peer]));
      recorder.addRanges(at(round / 2), peers);
    }
  }
  for (const [uid, recorder] of recorders) {
    const failed = uid === 0x3e ? [100, 100, 3000] : [100, 100, 100];
    recorder.addStats(at(minutes * 60), stats({
      roundsRanged: minutes * ROUNDS_PER_MINUTE, rxOk: 18_000 - failed.reduce((a, b) => a + b, 0),
      rxFailed: failed.reduce((a, b) => a + b, 0), rxOkByAntenna: failed.map((count) => 6000 - count), rxFailedByAntenna: failed,
    }));
  }
  const result = analyseRadio({
    devices: [0x02, 0x0b, 0x3e].map((uid) => ({ uid, label: uid.toString(16), summary: recorders.get(uid)!.summary() })),
  });
  const verdicts = Object.fromEntries(result.devices.map((device) => [device.uid, device.verdict]));
  assert.equal(verdicts[0x02], 'pass');
  assert.equal(verdicts[0x0b], 'pass');
  assert.notEqual(verdicts[0x3e], 'pass');
  assert.match(result.devices.find((device) => device.uid === 0x3e)!.reasons.join(' '), /antenna 3/i);
});
