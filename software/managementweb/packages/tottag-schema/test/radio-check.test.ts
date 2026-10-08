// The radio-health check: one log reduced to its radio summary, and a deployment's devices judged against
// each other. The fleets here are built to fail in one known way each, so a verdict that lands on the
// wrong device, or a reason that names the wrong antenna, shows up as a test failure rather than a
// plausible-looking report about a healthy device.

import { test } from 'node:test';
import assert from 'node:assert/strict';

import { parseV2 } from '../src/log.ts';
import {
  analyseRadio, identifyRadioLog, radioDeploymentKey, summarizeRadio, ROUNDS_PER_MINUTE,
  type RadioSummary, type PeerBin, LOG_BIN_MS,
} from '../src/radioCheck.ts';
import { STORAGE_TYPE } from '../src/constants.ts';
import { buildDetails, buildDiagnosticsBody, buildTestStream } from './helpers.ts';

const DEVICES = [{ uid: 0x02, label: '02' }, { uid: 0x0b, label: '0B' }, { uid: 0x3e, label: '3E' }];

test('a log is reduced to per-minute ranges, its own identity, and its radio totals across boots, with no antenna split', () => {
  const records: Array<[number, number, number[]]> = [];
  // Device 02 ranging to 0B (1500 mm) and 3E (2000 mm) every round for three minutes
  for (let round = 0; round < 3 * ROUNDS_PER_MINUTE; round += 1) {
    records.push([STORAGE_TYPE.STORAGE_TYPE_RANGES, 60_000 + round * 500, [2, 0x0b, 0xdc, 0x05, 0x3e, 0xd0, 0x07]]);
  }
  // Two boots: the counters reset in between, so the totals are the sum of each boot's last record
  records.push([STORAGE_TYPE.STORAGE_TYPE_DIAGNOSTICS, 120_000, buildDiagnosticsBody({ radio_rx_ok: 50, radio_rx_failed: 5 })]);
  records.push([STORAGE_TYPE.STORAGE_TYPE_DIAGNOSTICS, 180_000, buildDiagnosticsBody({ radio_rx_ok: 90, radio_rx_failed: 10 })]);
  records.push([STORAGE_TYPE.STORAGE_TYPE_RESET_REASON, 200_000, [0x08, 0x00]]);
  records.push([STORAGE_TYPE.STORAGE_TYPE_DIAGNOSTICS, 230_000, buildDiagnosticsBody({ radio_rx_ok: 40, radio_rx_failed: 4 })]);
  records.sort((a, b) => a[1] - b[1]);

  const summary = summarizeRadio(parseV2(buildTestStream(records, buildDetails(DEVICES))));
  assert.deepEqual(summary.deploymentUids, [0x02, 0x0b, 0x3e]);
  assert.deepEqual(summary.deploymentLabels, ['02', '0B', '3E']);
  assert.equal(summary.selfUid, 0x02, 'the one selected device it never ranged to is itself');
  assert.equal(summary.binMs, LOG_BIN_MS);
  assert.deepEqual(summary.rowsByBin, [[1, 120], [2, 120], [3, 120]]);
  const toB = summary.peers.find((peer) => peer.uid === 0x0b)!;
  assert.deepEqual(toB.bins.map((m) => [m[0], m[1], m[2], m[3]]), [[1, 120, 1500, 0], [2, 120, 1500, 0], [3, 120, 1500, 0]]);
  assert.equal(summary.diagnostics!.rxOk, 130);
  assert.equal(summary.diagnostics!.rxFailed, 14);
  assert.deepEqual([summary.diagnostics!.rxOkByAntenna, summary.diagnostics!.rxFailedByAntenna], [[], []], 'only a live test splits receives by antenna');
  assert.equal(radioDeploymentKey(summary), '1788301320:2,11,62');
});

/** A summary for a device that ranged to each peer in `coverage` of rounds, at the given distances. */
function device(uid: number, options: {
  peers: Record<number, { coverage: number; mm: number; spread?: number }>;
  participation?: number;
  rx?: [number, number];
  antennas?: [readonly number[], readonly number[]];
  minutes?: number;
}): { uid: number; label: string; summary: RadioSummary } {
  const minutes = options.minutes ?? 10;
  const label = uid.toString(16).toUpperCase().padStart(2, '0');
  const [ok, failed] = options.rx ?? [10_000, 800];
  const okBy = options.antennas?.[0] ?? [ok / 3, ok / 3, ok / 3];
  const failedBy = options.antennas?.[1] ?? [failed / 3, failed / 3, failed / 3];
  return {
    uid, label,
    summary: {
      experimentStartTime: 1788301320,
      deploymentUids: [], deploymentLabels: [], selfUid: uid,
      firstMs: 0, lastMs: (minutes + 1) * 60_000 + 30_000,
      binMs: LOG_BIN_MS,
      rowsByBin: Array.from({ length: minutes + 1 }, (_, m) => [m, Math.round((options.participation ?? 0.99) * ROUNDS_PER_MINUTE)] as const),
      peers: Object.entries(options.peers).map(([peer, link]) => ({
        uid: Number(peer),
        bins: Array.from({ length: minutes + 1 }, (_, m): PeerBin => [m, Math.round(link.coverage * ROUNDS_PER_MINUTE), link.mm, link.spread ?? 15]),
      })),
      diagnostics: { rxOk: ok, rxFailed: failed, rxOkByAntenna: okBy, rxFailedByAntenna: failedBy,
        wakeFailures: 0, irqStuck: 0, rxArmLate: 0, txLate: 0, samples: 3 },
      aborts: 0,
    },
  };
}

test('a weak receiver fails, and the healthy devices around it pass', () => {
  const result = analyseRadio({ devices: [
    device(0x02, { peers: { 0x0b: { coverage: 0.97, mm: 1000 }, 0x3e: { coverage: 0.97, mm: 1000 }, 0x49: { coverage: 0.9, mm: 1000 } } }),
    device(0x0b, { peers: { 0x02: { coverage: 0.97, mm: 1000 }, 0x3e: { coverage: 0.97, mm: 1000 }, 0x49: { coverage: 0.9, mm: 1000 } } }),
    device(0x3e, { peers: { 0x02: { coverage: 0.97, mm: 1000 }, 0x0b: { coverage: 0.97, mm: 1000 }, 0x49: { coverage: 0.9, mm: 1000 } } }),
    device(0x49, { rx: [6_000, 4_400], peers: { 0x02: { coverage: 0.9, mm: 1000 }, 0x0b: { coverage: 0.9, mm: 1000 }, 0x3e: { coverage: 0.9, mm: 1000 } } }),
  ] });
  const verdicts = Object.fromEntries(result.devices.map((d) => [d.label, d.verdict]));
  assert.deepEqual(verdicts, { '02': 'pass', '0B': 'pass', '3E': 'pass', '49': 'fail' });
  const reason = result.devices.find((d) => d.uid === 0x49)!.reasons[0]!;
  assert.match(reason, /42% of its ranging receives failed against 7% for the other devices/);
});

test('a bad antenna is named, even when the device overall is only a little worse', () => {
  const result = analyseRadio({ devices: [
    device(0x02, { peers: { 0x0b: { coverage: 0.97, mm: 1000 }, 0x3e: { coverage: 0.97, mm: 1000 } } }),
    device(0x0b, { antennas: [[3_500, 3_500, 2_000], [150, 150, 1_500]], rx: [9_000, 1_800],
      peers: { 0x02: { coverage: 0.97, mm: 1000 }, 0x3e: { coverage: 0.97, mm: 1000 } } }),
    device(0x3e, { peers: { 0x02: { coverage: 0.97, mm: 1000 }, 0x0b: { coverage: 0.97, mm: 1000 } } }),
  ] });
  const bad = result.devices.find((d) => d.uid === 0x0b)!;
  assert.equal(bad.verdict, 'fail');
  assert.ok(bad.reasons.some((r) => /^Antenna 3 failed 43% .* against 4% and 4% on the others/.test(r)), bad.reasons.join(' | '));
});

test('a device that reads long on every link is singled out by its solved offset', () => {
  // Three devices on a 2 m triangle; 3E adds 250 mm to every range it is part of
  const positions = new Map([[0x02, { x: 0, y: 0 }], [0x0b, { x: 2, y: 0 }], [0x3e, { x: 1, y: Math.sqrt(3) }]]);
  const result = analyseRadio({ positions, devices: [
    device(0x02, { peers: { 0x0b: { coverage: 0.97, mm: 2000 }, 0x3e: { coverage: 0.97, mm: 2250 } } }),
    device(0x0b, { peers: { 0x02: { coverage: 0.97, mm: 2000 }, 0x3e: { coverage: 0.97, mm: 2250 } } }),
    device(0x3e, { peers: { 0x02: { coverage: 0.97, mm: 2250 }, 0x0b: { coverage: 0.97, mm: 2250 } } }),
  ] });
  const offsets = Object.fromEntries(result.devices.map((d) => [d.label, Math.round(d.biasMm!)]));
  assert.deepEqual(offsets, { '02': 0, '0B': 0, '3E': 250 });
  assert.equal(result.devices.find((d) => d.uid === 0x3e)!.verdict, 'check');
  assert.match(result.devices.find((d) => d.uid === 0x3e)!.reasons[0]!, /about 250 mm long on every link/);
});

test('a selected device with no log is reported as missing, not as a pass', () => {
  const result = analyseRadio({ devices: [
    device(0x02, { peers: { 0x0b: { coverage: 0.97, mm: 1000 } } }),
    device(0x0b, { peers: { 0x02: { coverage: 0.97, mm: 1000 } } }),
    { uid: 0x3e, label: '3E', summary: null },
  ] });
  assert.equal(result.devices.find((d) => d.uid === 0x3e)!.verdict, 'missing');
  assert.ok(result.notes.some((note) => /1 of the 3 selected devices has no log loaded/.test(note)));
  assert.ok(result.notes.some((note) => /fewer than three devices/.test(note)));
});

test('a device that barely took part fails on participation and coverage', () => {
  const result = analyseRadio({ devices: [
    device(0x02, { peers: { 0x0b: { coverage: 0.97, mm: 1000 }, 0x3e: { coverage: 0.3, mm: 1000 } } }),
    device(0x0b, { peers: { 0x02: { coverage: 0.97, mm: 1000 }, 0x3e: { coverage: 0.3, mm: 1000 } } }),
    device(0x3e, { participation: 0.3, peers: { 0x02: { coverage: 0.3, mm: 1000 }, 0x0b: { coverage: 0.3, mm: 1000 } } }),
  ] });
  const quiet = result.devices.find((d) => d.uid === 0x3e)!;
  assert.equal(quiet.verdict, 'fail');
  assert.match(quiet.reasons[0]!, /Ranged in only 30% of rounds/);
});

test('a log names its device by itself, by the transport, or by its file name', () => {
  const base: RadioSummary = {
    experimentStartTime: 1, deploymentUids: [0x02, 0x0b], deploymentLabels: ['Alice', 'Bob'], selfUid: null,
    firstMs: 0, lastMs: 0, binMs: LOG_BIN_MS, rowsByBin: [], peers: [], diagnostics: null, aborts: 0,
  };
  assert.equal(identifyRadioLog({ ...base, selfUid: 0x0b }, 'x.ttg'), 0x0b);
  assert.equal(identifyRadioLog(base, 'x.ttg', 0x02), 0x02);
  assert.equal(identifyRadioLog(base, 'bob_1791288000.ttg'), 0x0b);
  assert.equal(identifyRadioLog(base, '0B_1791288000.ttg'), 0x0b);
  assert.equal(identifyRadioLog(base, 'unrelated.ttg'), null);
});
