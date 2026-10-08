// The scenarios that hold the Python dashboard's radio check (management/dashboard/radio_check.py) to this one.
//
// Each scenario's inputs, and this package's answers to them, are written to fixtures/radio-check-parity.json.
// test_radio_check.py beside the dashboard replays the inputs through the Python port and expects the same answers;
// radio-check-parity.test.ts here fails when this package's answers move without the fixture being rewritten
// (npm run parity:update), so a rule changed on one side cannot quietly leave the other behind.
//
// The live scenario is raw Bluetooth payloads, so it also holds the two sides' decoders to each other.

import { BLE_RADIO_STATS_FLAG_TEST_RUNNING, BLE_RADIO_STATS_VERSION, RADIO_STATS_LAYOUT, SCHEDULE_ROLE } from '../src/constants.ts';
import {
  LiveRadioRecorder, decodeRadioStats, decodeRangeResults, encodeRadioTestStartCommand, encodeRadioTestStopCommand,
} from '../src/liveRadio.ts';
import { analyseRadio, LOG_BIN_MS, ROUNDS_PER_MINUTE, type PeerBin, type RadioCheckResult, type RadioSummary } from '../src/radioCheck.ts';

const MINUTE_MS = 60_000;
const START_TIME = 1_791_300_000;

/** Deterministic, so the fixture only changes when the rules do. */
function generator(seed: number) {
  let state = seed >>> 0;
  return () => {
    state = (state + 0x6d2b79f5) >>> 0;
    let t = state;
    t = Math.imul(t ^ (t >>> 15), t | 1);
    t ^= t + Math.imul(t ^ (t >>> 7), t | 61);
    return ((t ^ (t >>> 14)) >>> 0) / 4294967296;
  };
}

const hex = (bytes: Uint8Array) => [...bytes].map((b) => b.toString(16).padStart(2, '0')).join('');

// --- Summaries built to fail in known ways -------------------------------------------------------------------

interface DeviceSpec {
  readonly uid: number;
  readonly label: string;
  /** No summary at all, or one with no records (firstMs null). */
  readonly absent?: 'none' | 'empty';
  readonly firstMs?: number;
  readonly lastMs?: number;
  readonly participation?: number;
  readonly coverage?: number;
  readonly biasMm?: number;
  readonly noiseMm?: number;
  readonly rxFailureRate?: number;
  /** Failure rate per antenna, for a live summary; absent for one from a log, which has no split. */
  readonly antennaFailureRates?: readonly number[];
  /** Receives per antenna, where one should have too few to judge. */
  readonly antennaSamples?: readonly number[];
  readonly wakeFailures?: number;
  readonly irqStuck?: number;
  readonly rxArmLate?: number;
  /** Explicit rows per minute, for rounding at exact halves. */
  readonly rows?: number;
}

interface Scenario {
  readonly name: string;
  readonly minutes: number;
  readonly devices: readonly DeviceSpec[];
  readonly positions?: ReadonlyArray<readonly [number, number, number]>;
}

function buildSummaries(scenario: Scenario, random: () => number) {
  const where = new Map((scenario.positions ?? []).map(([uid, x, y]) => [uid, { x, y }]));
  const jitter = (spread: number) => 1 - spread * random();
  const uids = scenario.devices.map((b) => b.uid);
  const labels = scenario.devices.map((b) => b.label);
  return scenario.devices.map((spec) => {
    if (spec.absent === 'none') return { uid: spec.uid, label: spec.label, summary: null };
    const firstMs = spec.absent === 'empty' ? null : (spec.firstMs ?? 5_000);
    const lastMs = spec.absent === 'empty' ? null : (spec.lastMs ?? scenario.minutes * MINUTE_MS + 20_000);
    const minutes = firstMs === null || lastMs === null ? [] : Array.from(
      { length: Math.floor(lastMs / MINUTE_MS) - Math.floor(firstMs / MINUTE_MS) + 1 }, (_, i) => Math.floor(firstMs / MINUTE_MS) + i);

    const rowsByBin = minutes.map((minute) => [minute, spec.rows ?? Math.round(ROUNDS_PER_MINUTE * (spec.participation ?? 0.99) * jitter(0.02))] as const);
    const peers = scenario.devices.filter((peer) => peer.uid !== spec.uid && peer.absent !== 'none').map((peer) => {
      const a = where.get(spec.uid);
      const b = where.get(peer.uid);
      const trueMm = a && b ? Math.hypot(a.x - b.x, a.y - b.y) * 1000 : 1500 + 37 * ((spec.uid + peer.uid) % 11);
      const noise = Math.max(spec.noiseMm ?? 18, peer.noiseMm ?? 18);
      const coverage = Math.min(spec.coverage ?? 0.97, peer.coverage ?? 0.97, spec.participation ?? 1);
      return {
        uid: peer.uid,
        bins: minutes.map((minute): PeerBin => [
          minute,
          Math.round(ROUNDS_PER_MINUTE * coverage * jitter(0.03)),
          Math.round(trueMm + (spec.biasMm ?? 0) + (peer.biasMm ?? 0) + (random() - 0.5) * noise * 0.4),
          Math.round(noise * (0.55 + 0.25 * random())),
        ]),
      };
    });

    const received = minutes.length * ROUNDS_PER_MINUTE * 6;
    const failureRate = spec.rxFailureRate ?? 0.04 + 0.02 * random();
    const rxFailed = Math.round(received * failureRate);
    const antennas = spec.antennaFailureRates;
    const perAntenna = Math.floor(received / 3);
    const summary: RadioSummary = {
      experimentStartTime: START_TIME,
      deploymentUids: uids,
      deploymentLabels: labels,
      selfUid: spec.uid,
      firstMs, lastMs,
      binMs: LOG_BIN_MS,
      rowsByBin: firstMs === null ? [] : rowsByBin,
      peers: firstMs === null ? [] : peers,
      diagnostics: firstMs === null ? null : {
        rxOk: received - rxFailed,
        rxFailed,
        rxOkByAntenna: antennas ? antennas.map((rate, i) => (spec.antennaSamples?.[i] ?? perAntenna) - Math.round((spec.antennaSamples?.[i] ?? perAntenna) * rate)) : [],
        rxFailedByAntenna: antennas ? antennas.map((rate, i) => Math.round((spec.antennaSamples?.[i] ?? perAntenna) * rate)) : [],
        wakeFailures: spec.wakeFailures ?? 0,
        irqStuck: spec.irqStuck ?? 0,
        rxArmLate: spec.rxArmLate ?? 0,
        txLate: 0,
        samples: minutes.length,
      },
      aborts: 0,
    };
    return { uid: spec.uid, label: spec.label, summary };
  });
}

const circle = (uids: number[], radius: number) =>
  uids.map((uid, i) => [uid, Math.round(radius * Math.cos((2 * Math.PI * i) / uids.length) * 1000) / 1000,
    Math.round(radius * Math.sin((2 * Math.PI * i) / uids.length) * 1000) / 1000] as const);
const line = (uids: number[], spacing: number) => uids.map((uid, i) => [uid, i * spacing, 0] as const);
const healthyAntennas = [0.04, 0.05, 0.045];

const SCENARIOS: readonly Scenario[] = [
  {
    name: 'five healthy devices, live, in a circle',
    minutes: 6,
    devices: [0x02, 0x0b, 0x3e, 0x4a, 0x51].map((uid) => ({ uid, label: uid.toString(16).toUpperCase().padStart(2, '0'), antennaFailureRates: healthyAntennas, biasMm: (uid % 5) * 8 - 16 })),
    positions: circle([0x02, 0x0b, 0x3e, 0x4a, 0x51], 1.5),
  },
  {
    name: 'one device per fault, in a line',
    minutes: 8,
    devices: [
      { uid: 0x02, label: 'Healthy', antennaFailureRates: healthyAntennas },
      { uid: 0x0b, label: 'Deaf', rxFailureRate: 0.35, antennaFailureRates: [0.34, 0.36, 0.35] },
      { uid: 0x3e, label: 'Antenna 2', rxFailureRate: 0.17, antennaFailureRates: [0.05, 0.4, 0.06] },
      { uid: 0x4a, label: 'Antenna 3', antennaFailureRates: [0.04, 0.05, 0.2] },
      { uid: 0x51, label: 'Absent-minded', participation: 0.7, coverage: 0.6, antennaFailureRates: healthyAntennas },
      { uid: 0x62, label: 'Long', biasMm: 350, antennaFailureRates: healthyAntennas },
      { uid: 0x77, label: 'Noisy', noiseMm: 160, wakeFailures: 2, irqStuck: 1, rxArmLate: 30, antennaFailureRates: healthyAntennas },
      { uid: 0x88, label: 'Short', biasMm: -150, antennaFailureRates: [0.05, 0.3, 0.05], antennaSamples: [3000, 150, 3000] },
    ],
    positions: line([0x02, 0x0b, 0x3e, 0x4a, 0x51, 0x62, 0x77, 0x88], 0.8),
  },
  {
    name: 'from logs, with no antenna split and no positions',
    minutes: 5,
    devices: [
      { uid: 0x10, label: 'A' },
      { uid: 0x20, label: 'B', participation: 0.4 },
      { uid: 0x30, label: 'C', coverage: 0.3 },
      { uid: 0x40, label: 'D', rxFailureRate: 0.15 },
    ],
  },
  {
    name: 'a missing log, an empty one, and too short a run',
    minutes: 1,
    devices: [
      { uid: 0x05, label: 'Here', lastMs: 100_000 },
      { uid: 0x06, label: 'Gone', absent: 'none' },
      { uid: 0x07, label: 'Blank', absent: 'empty' },
      { uid: 0x08, label: 'Late', firstMs: 30_000, lastMs: 110_000 },
    ],
  },
  {
    name: 'positions for too few devices to separate offsets',
    minutes: 4,
    devices: [{ uid: 0x21, label: 'X' }, { uid: 0x22, label: 'Y' }, { uid: 0x23, label: 'Z' }],
    positions: [[0x21, 0, 0], [0x22, 1, 0]],
  },
  {
    // A five-minute window is 600 rounds, so n rows a minute is n/120 of them: 87 is 72.5% and 3 is 2.5%, where
    // JavaScript rounds the half up and Python's round() would take it to the even neighbour
    name: 'percentages that land exactly on a half',
    minutes: 6,
    devices: [
      { uid: 0x31, label: 'Seventy-two and a half', rows: 87 },
      { uid: 0x32, label: 'Seventy-seven and a half', rows: 93 },
      { uid: 0x33, label: 'Healthy', rows: 119 },
      { uid: 0x34, label: 'Two and a half', rows: 3 },
    ],
  },
];

// --- A live test, as the Bluetooth payloads it would stream -------------------------------------------------

function statsPayload(values: Record<string, number | readonly number[]>): Uint8Array {
  const bytes = new Uint8Array(RADIO_STATS_LAYOUT.size);
  const view = new DataView(bytes.buffer);
  for (const field of RADIO_STATS_LAYOUT.fields) {
    const value = values[field.name];
    if (value === undefined) continue;
    const items: readonly number[] = typeof value === 'number' ? [value] : value;
    items.forEach((item, index) => {
      const at = field.offset + index * field.elementSize;
      if (field.elementSize === 1) view.setUint8(at, item);
      else if (field.elementSize === 2) view.setUint16(at, item, true);
      else view.setUint32(at, item, true);
    });
  }
  return bytes;
}

function rangesPayload(ranges: ReadonlyArray<readonly [number, number]>, count = ranges.length): Uint8Array {
  const bytes = new Uint8Array(1 + ranges.length * 3);
  bytes[0] = count;
  ranges.forEach(([uid, mm], i) => { bytes[1 + i * 3] = uid; bytes[2 + i * 3] = mm & 0xff; bytes[3 + i * 3] = mm >> 8; });
  return bytes;
}

type LiveEvent = readonly [ms: number, kind: 'ranges' | 'stats', payload: string];

/**
 * Four devices four and a half minutes into a test: ranges every round with some notifications dropped, counters
 * read every five seconds, one device restarting mid-test, one whose notifications reach the host in bunches of
 * seven rounds across bin edges, one notification cut short by a small Bluetooth packet, and one range past the
 * valid limit.
 */
function buildLiveScenario(random: () => number) {
  const devices = [{ uid: 0x02, label: 'Red' }, { uid: 0x0b, label: 'Green' }, { uid: 0x3e, label: 'Blue' }, { uid: 0x4a, label: 'Gold' }];
  const positions = circle(devices.map((b) => b.uid), 1.2);
  const where = new Map(positions.map(([uid, x, y]) => [uid, { x, y }]));
  const startMs = START_TIME * 1000;
  const roundsTotal = 540;
  const streams = devices.map((device, index) => {
    const events: LiveEvent[] = [];
    const deafness = device.uid === 0x3e ? 0.25 : 0.03;
    let roundsRanged = 0;
    let rxOk = [0, 0, 0];
    let rxFailed = [0, 0, 0];
    let restarted = false;
    for (let round = 0; round < roundsTotal; round += 1) {
      const ms = startMs + 3_000 + round * 500 + Math.floor(random() * 40);
      if (device.uid === 0x4a && round >= 260 && round < 270) continue;   // restarting: no rounds, no reads
      const heard = devices.filter((peer) => peer.uid !== device.uid && random() > deafness);
      for (let antenna = 0; antenna < 3; antenna += 1) {
        const tries = 2;
        const failRate = device.uid === 0x3e && antenna === 1 ? 0.45 : deafness / 2;
        for (let t = 0; t < tries; t += 1) {
          if (random() < failRate) rxFailed[antenna]! += 1; else rxOk[antenna]! += 1;
        }
      }
      if (heard.length) {
        roundsRanged += 1;
        const special = (index === 0 && round === 100) || (index === 1 && round === 150);
        const dropped = random() < 0.08 && !special;
        if (!dropped) {
          const ranges = heard.map((peer) => {
            const a = where.get(device.uid)!;
            const b = where.get(peer.uid)!;
            const mm = Math.round(Math.hypot(a.x - b.x, a.y - b.y) * 1000 + (device.uid === 0x0b ? 180 : 0) + (peer.uid === 0x0b ? 180 : 0) + (random() - 0.5) * 60);
            return [peer.uid, mm] as const;
          });
          // Green's notifications are held back and delivered with the last round of every seven
          const delivered = index === 1 ? startMs + 3_000 + (Math.floor(round / 7) * 7 + 6) * 500 + 45 : ms;
          if (index === 0 && round === 100) events.push([ms, 'ranges', hex(rangesPayload([...ranges, [0x77, 40_000]]))]);
          else if (index === 1 && round === 150) events.push([delivered, 'ranges', hex(rangesPayload(ranges.slice(0, 1), ranges.length + 1))]);
          else events.push([delivered, 'ranges', hex(rangesPayload(ranges))]);
        }
      }
      if (round % 10 === 9) {
        if (device.uid === 0x4a && round >= 270 && !restarted) {
          // Booted back into the test: everything from zero
          restarted = true;
          roundsRanged = 3;
          rxOk = [6, 6, 5];
          rxFailed = [0, 0, 1];
        }
        events.push([ms + 120, 'stats', hex(statsPayload({
          version: BLE_RADIO_STATS_VERSION, role: index === 0 ? SCHEDULE_ROLE.ROLE_MASTER : SCHEDULE_ROLE.ROLE_PARTICIPANT,
          schedule_size: devices.length, flags: BLE_RADIO_STATS_FLAG_TEST_RUNNING, test_seconds_left: 600 - Math.floor(round / 2),
          rounds_scheduled: round + 1, rounds_ranged: roundsRanged,
          rx_ok: rxOk.reduce((s, v) => s + v, 0), rx_failed: rxFailed.reduce((s, v) => s + v, 0),
          rx_ok_by_antenna: rxOk, rx_failed_by_antenna: rxFailed,
          tx_late: 0, rx_arm_late: index === 2 ? 4 : 0, isr_over_budget: 0, wake_max_us: 1900 + index * 10, wake_failures: index === 3 ? 1 : 0,
        }))]);
      }
    }
    return { uid: device.uid, label: device.label, events };
  });

  const uids = devices.map((b) => b.uid);
  const labels = devices.map((b) => b.label);
  const recorded = streams.map((stream) => {
    const recorder = new LiveRadioRecorder({ startTime: START_TIME, deploymentUids: uids, deploymentLabels: labels, selfUid: stream.uid });
    for (const [ms, kind, payload] of stream.events) {
      const bytes = Uint8Array.from(payload.match(/../g)!.map((pair) => Number.parseInt(pair, 16)));
      if (kind === 'stats') recorder.addStats(ms, decodeRadioStats(bytes));
      else {
        const { ranges, truncated } = decodeRangeResults(bytes);
        recorder.addRanges(ms, ranges, truncated);
      }
    }
    return { uid: stream.uid, label: stream.label, summary: recorder.summary(), truncated: recorder.truncated };
  });
  const analysis = analyseRadio({
    devices: recorded.map(({ uid, label, summary }) => ({ uid, label, summary })),
    positions: new Map(positions.map(([uid, x, y]) => [uid, { x, y }])),
  });

  const sample = streams[2]!.events.find((event) => event[1] === 'stats')!;
  return {
    startTime: START_TIME, uids, labels, positions, streams,
    expected: { summaries: recorded, analysis: plain(analysis) },
    decodedStats: { payload: sample[2], stats: decodeRadioStats(Uint8Array.from(sample[2].match(/../g)!.map((p) => Number.parseInt(p, 16)))) },
  };
}

/** A result as JSON holds it: Maps and typed arrays are not used in results, so this only strips readonly-ness. */
function plain(result: RadioCheckResult) {
  return JSON.parse(JSON.stringify(result)) as RadioCheckResult;
}

export function buildRadioParityFixture() {
  const random = generator(0x7077a6);
  const analysis = SCENARIOS.map((scenario) => {
    const devices = buildSummaries(scenario, random);
    const positions = scenario.positions ?? [];
    const result = analyseRadio({ devices, positions: new Map(positions.map(([uid, x, y]) => [uid, { x, y }])) });
    return { name: scenario.name, devices, positions, expected: plain(result) };
  });
  const euis = [Uint8Array.of(0x02, 0x11, 0x22, 0x33, 0x44, 0x55), Uint8Array.of(0xf3, 0xaa, 0xbb, 0xcc, 0xdd, 0xee)];
  return {
    about: 'Generated by tools/radio-check-parity.mjs from test/radioParityFixture.ts; replayed by management/dashboard/test_radio_check.py. Do not edit by hand.',
    commands: {
      start: { startTime: START_TIME, endTime: START_TIME + 600, euis: euis.map(hex), expected: hex(encodeRadioTestStartCommand(START_TIME, START_TIME + 600, euis)) },
      stop: { expected: hex(encodeRadioTestStopCommand()) },
    },
    analysis,
    live: buildLiveScenario(random),
  };
}
