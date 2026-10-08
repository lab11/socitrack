// The deployment summary, against the same real fixtures the reader is tested on.
//
// Two of the assertions here are the ones that matter, and both exist because the hand audit in
// Storage_Redesign.md §15.8 got them wrong on the first attempt against real data:
//
//   - A watchdog reset's SHAPE. The silence before the reset separates "one task stalled while the
//     rest of the system kept logging" from "the whole device stopped", and those cost very
//     different amounts of data. Neither the diagnostic nor the silence says it alone.
//   - Whether the CHARGER caused a reboot. Every boot writes the current charge state, so keying on
//     a charging record at the reset's own millisecond labels every reset a charger event —
//     including watchdog resets. The transition that caused a reboot comes before it.

import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import { dirname, join } from 'node:path';

import { parseV2 } from '../src/log.ts';
import { analyseDeployment, compareLink } from '../src/health.ts';
import {
  BATTERY_EVENT_DEBOUNCE_MS, STORAGE_FLUSH_TIMEOUT_S, STORAGE_TYPE,
  TIME_ALIGNED_INTERVAL_S, WATCHDOG_TASK,
} from '../src/constants.ts';
import { buildDiagnosticsBody, buildTestStream } from './helpers.ts';

const FIXTURES = join(dirname(fileURLToPath(import.meta.url)), 'fixtures');
const boot = analyseDeployment(parseV2(new Uint8Array(readFileSync(join(FIXTURES, 'boot.ttg')))));
const watchdog = analyseDeployment(parseV2(new Uint8Array(readFileSync(join(FIXTURES, 'watchdog.ttg')))));

test('real device output reads as structurally clean', () => {
  for (const [name, health] of [['boot', boot], ['watchdog', watchdog]] as const) {
    assert.equal(health.integrity.clean, true, `${name} is not clean`);
    assert.equal(health.integrity.holes, 0);
    assert.equal(health.integrity.crcFailures, 0);
    assert.equal(health.integrity.sequenceGaps, 0);
    assert.equal(health.integrity.duplicateSeqs, 0);
    assert.equal(health.integrity.truncated, false);
    assert.equal(health.integrity.pagesDelivered, health.integrity.pagesAdvertised);
  }
});

test('a clean log raises no integrity anomaly', () => {
  const codes = boot.anomalies.map((a) => a.code);
  assert.ok(!codes.includes('pages-lost'));
  assert.ok(!codes.includes('truncated'));
  assert.ok(!codes.includes('time-rebased'));
  assert.ok(!codes.includes('rtc-restarted'));
});

test('the anchor interval measures the tick-corrected period, not the nominal one', () => {
  // 299.38 s, not 300. Measured on a device rather than asserted from a constant: this is the
  // FreeRTOS tick divisor's 0.208% truncation showing up in real data.
  assert.ok(Math.abs(watchdog.clock.medianIntervalSeconds - TIME_ALIGNED_INTERVAL_S) < 0.05);
});

test('a single-task stall is distinguished from a whole-system one', () => {
  const reset = watchdog.reboots.find((r) => r.kind === 'watchdog');
  assert.ok(reset, 'the watchdog fixture no longer contains a watchdog reset');
  assert.equal(reset.diagnosticLabel, 'stalled: BLETask');
  assert.equal(reset.shape, 'single-task');
  // The classification's whole basis: the device kept logging while the watchdog counted down, so
  // the only thing lost was the unflushed page.
  assert.ok(reset.silenceSeconds < STORAGE_FLUSH_TIMEOUT_S, 'silence exceeded the flush window');
  assert.ok(reset.silenceSeconds > 60, 'silence is implausibly short for a caught stall');
});

test('the outage is measured on the clock the reboot does not touch', () => {
  // The anchors bracketing the reset give real elapsed time regardless of what the offset did. The
  // hand audit measured 182.0 s across this reset; the parser must agree.
  const reset = watchdog.reboots.find((r) => r.kind === 'watchdog');
  assert.ok(reset?.outageSeconds !== null && reset?.outageSeconds !== undefined);
  assert.ok(Math.abs(reset.outageSeconds - 182.0) < 0.5, `outage was ${reset.outageSeconds} s`);
});

test('a watchdog reset is not mistaken for a charger reboot', () => {
  // This fixture DOES contain a charging record at the reset's own millisecond — the boot-time
  // status report. Labelling that a charger transition would have made every reset in every log
  // look like someone touched the charger.
  const reset = watchdog.reboots.find((r) => r.kind === 'watchdog');
  assert.equal(reset!.chargerTransition, false);
  const chargingAtReset = watchdog.recordCounts.get('charging');
  assert.ok(chargingAtReset && chargingAtReset > 0, 'the fixture no longer exercises this case');
});

test('a graceful boot reports no stall shape', () => {
  const reset = boot.reboots[0];
  assert.ok(reset);
  assert.equal(reset.kind, 'graceful');
  assert.equal(reset.shape, null);
});

test('the device clock never steps backwards', () => {
  // It is derived from the RTC, which survives a reset. A backward step means power was removed.
  assert.equal(boot.clock.localClockMonotonic, true);
  assert.equal(watchdog.clock.localClockMonotonic, true);
});

test('a fresh experiment starts with a zero network offset and acquires one', () => {
  // The offset is zero until the device hears a schedule, then it adopts the network's base. That
  // first step is normal operation, not a fault, and must not be reported as lost time.
  assert.equal(boot.clock.offsetStartMs, 0);
  assert.ok(boot.clock.offsetDriftMs !== 0, 'the boot fixture no longer spans a network join');
  assert.ok(!boot.anomalies.some((a) => a.severity === 'warning'), 'a network join was reported as a fault');
});

test('the record census matches what a ranging tag writes', () => {
  assert.ok(watchdog.recordCounts.get('ranges')! > 100);
  assert.ok(watchdog.recordCounts.get('bleScan')! > 100);
  assert.equal(watchdog.recordCounts.get('reset'), 1);
  assert.ok(watchdog.rangingPeers.size >= 1);
  assert.ok(boot.batteryStartMv! > 3000 && boot.batteryStartMv! < 4500);
});

test('cross-device comparison scores a link by both presence and agreement', () => {
  // Compared against itself, a log must agree with itself perfectly. That is a weak test of the
  // link but a strong test of the comparison: an off-by-one in the timestamp keying would show here.
  const parsed = parseV2(new Uint8Array(readFileSync(join(FIXTURES, 'watchdog.ttg'))));
  const peer = [...watchdog.rangingPeers.keys()][0]!;
  const self = compareLink({ uid: peer, result: parsed }, { uid: peer, result: parsed });
  assert.equal(self.sharedFraction, 1);
  assert.equal(self.agreeingFraction, 1);
  assert.ok(self.common > 100);

  // A device that logged nothing about the link shares nothing.
  const empty = compareLink({ uid: 1, result: parsed }, { uid: 2, result: parsed });
  assert.equal(empty.common, 0);
  assert.equal(empty.sharedFraction, 0);
});

test('page fill reports what the transfer actually cost', () => {
  assert.equal(watchdog.pageFill.pages, watchdog.integrity.pagesDelivered);
  assert.ok(watchdog.pageFill.meanPayloadBytes > 0);
  assert.ok(watchdog.pageFill.pagesPerDay > 0);
});

// --- Near-miss counters ---------------------------------------------------------------------------------

/**
 * Build a stream of diagnostics and reset records with the given per-boot decline counts.
 *
 * `boots` is a list of counter sequences: `[[1, 2, 3], [1]]` means one boot whose declines rose 1 -> 2 -> 3,
 * a reset, then a second boot that reached 1. The correct total is 3 + 1 = 4.
 */
function buildBootStream(boots: number[][], includeResetRecords: boolean): Uint8Array {
  const records: Array<[number, number, number[]]> = [];
  let ms = 1000;
  boots.forEach((samples, boot) => {
    if (boot > 0 && includeResetRecords) {
      records.push([STORAGE_TYPE.STORAGE_TYPE_RESET_REASON, (ms += 100), [0x08, 0x00]]);
    }
    for (const declines of samples) {
      const late = new Array(WATCHDOG_TASK.WATCHDOG_NUM_TASKS).fill(0);
      late[WATCHDOG_TASK.WATCHDOG_TASK_BLE] = declines;
      const body = buildDiagnosticsBody({ watchdog_declines: declines, watchdog_late_episodes: late });
      records.push([STORAGE_TYPE.STORAGE_TYPE_DIAGNOSTICS, (ms += 299380), body]);
    }
  });
  return buildTestStream(records);
}

test('near-miss counters are summed across boots, not read from the last record', () => {
  // The counters reset at every reboot, so the final record holds only the last boot's share. Reading it
  // alone was the first implementation and it silently discarded most of a multi-reboot log.
  const health = analyseDeployment(parseV2(buildBootStream([[1, 2, 3], [1]], true)));
  assert.equal(health.nearMisses.samples, 4);
  assert.equal(health.nearMisses.watchdogDeclines, 4, 'expected 3 from the first boot plus 1 from the second');
  assert.equal(health.nearMisses.watchdogLate.get('BLETask'), 4);
  assert.ok(health.anomalies.some((a) => a.code === 'watchdog-near-miss'));
});

test('a boot boundary is still found when the reset record never reached flash', () => {
  // Section 12.21: presence of a reset record is proof, absence is not. A device that resets before the
  // record is flushed leaves only a counter that went backwards, and that has to be enough.
  const health = analyseDeployment(parseV2(buildBootStream([[5, 9], [2]], false)));
  assert.equal(health.nearMisses.watchdogDeclines, 11, 'expected 9 from the first boot plus 2 from the second');
});

test('a clean run reports no near-miss anomaly', () => {
  const health = analyseDeployment(parseV2(buildBootStream([[0, 0, 0]], true)));
  assert.equal(health.nearMisses.samples, 3);
  assert.equal(health.nearMisses.watchdogDeclines, 0);
  assert.equal(health.nearMisses.chargerSuppressedEdges, 0);
  assert.equal(health.nearMisses.wsfAllocFailures, 0);
  assert.ok(!health.anomalies.some((a) => a.code.startsWith('watchdog-near')));
  assert.ok(!health.anomalies.some((a) => a.code === 'charger-pin-chatter'));
});

test('a log with no diagnostics records reports no samples rather than zeros', () => {
  // Firmware older than the record type must be distinguishable from firmware that recorded all zeros.
  const health = analyseDeployment(parseV2(new Uint8Array(readFileSync(join(FIXTURES, 'boot.ttg')))));
  assert.equal(health.nearMisses.samples, 0);
});

test('the charger-storm message describes the de-bounce the firmware actually has', () => {
  // This message previously told the reader the charge-status ISR had "no comparison against the
  // last reported state and no debounce". Both were fixed in firmware (battery.c
  // signal_change_accepted), and the message was not, so it spent that time advising researchers to
  // expect a defect that no longer existed.
  //
  // Prose about firmware behaviour is the one thing neither the constant snapshot nor the record
  // grammar check can see. Pinning it to an extracted constant is the cheapest available substitute:
  // the number cannot drift, and a firmware change that removes the constant fails extraction.
  // 150 NotCharging edges 500 ms apart: the shape the real 19,677-record storm had, scaled down.
  // A diagnostics record is included so the log PROVES it came from firmware that de-bounces —
  // without it the message correctly hedges instead, which firmware-era.test.ts covers.
  const stormy: Array<[number, number, number[]]> = [];
  for (let i = 0; i < 150; i += 1) {
    stormy.push([STORAGE_TYPE.STORAGE_TYPE_CHARGING_EVENT, i * 500, [4]]);
  }
  stormy.push([STORAGE_TYPE.STORAGE_TYPE_DIAGNOSTICS, 300_000, buildDiagnosticsBody()]);
  const health = analyseDeployment(parseV2(buildTestStream(stormy)));
  const anomaly = health.anomalies.find((a) => a.code === 'charging-event-storm');
  assert.ok(anomaly, 'a 150-record charging storm should be reported');
  assert.match(anomaly.message, new RegExp(`${BATTERY_EVENT_DEBOUNCE_MS} ms`));
  assert.match(anomaly.message, /charger_suppressed_edges/);
  assert.doesNotMatch(anomaly.message, /no debounce|no comparison/);
});

// --- Diagnostics findings -----------------------------------------------------------------------------

test('the radio, storage, stack and recovery counters each raise their own finding', () => {
  const body = buildDiagnosticsBody({
    firmware_revision: 0x607993c9, status_flags: 0x07, temperature_c: 31,
    radio_rx_ok: 120000, radio_rx_failed: 400, radio_wake_failures: 2, radio_rx_arm_late: 1,
    storage_records_dropped: 3, ble_resets: 1, nand_bad_blocks: 4,
    stack_free_words: [500, 400, 300, 40, 600, 200],
  });
  const later = buildDiagnosticsBody({ firmware_revision: 0x607993c9, status_flags: 0x07, nand_bad_blocks: 5,
    stack_free_words: [500, 400, 300, 40, 600, 200], storage_records_dropped: 3, ble_resets: 1,
    radio_wake_failures: 2, radio_rx_arm_late: 1 });
  const health = analyseDeployment(parseV2(buildTestStream([
    [STORAGE_TYPE.STORAGE_TYPE_DIAGNOSTICS, 300_000, body],
    [STORAGE_TYPE.STORAGE_TYPE_DIAGNOSTICS, 600_000, later],
  ])));
  const codes = new Set(health.anomalies.map((a) => a.code));
  assert.deepEqual([...health.nearMisses.firmwareBuilds], ['607993c9 (modified)']);
  assert.equal(health.nearMisses.recordsDropped, 3, 'counters are cumulative, so one boot contributes its last value');
  assert.equal(health.nearMisses.radioWakeFailures, 2);
  assert.equal(health.nearMisses.bleResets, 1);
  assert.equal(health.nearMisses.lowestStackWords.get('BLETask'), 40);
  assert.equal(health.nearMisses.nandBlocksRetired, 1);
  for (const code of ['records-dropped', 'uwb-radio-trouble', 'ble-controller-restarts', 'stack-nearly-exhausted', 'flash-blocks-retired']) {
    assert.ok(codes.has(code), `expected a ${code} finding`);
  }
});
