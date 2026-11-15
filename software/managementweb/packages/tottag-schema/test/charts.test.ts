// The reducers that turn one log into what its charts draw.
//
// Each is checked against synthetic streams built record by record, so the expected answer is known
// exactly, and the whole set against the real fixtures, so the shapes hold on firmware output.

import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import { dirname, join } from 'node:path';

import { BATTERY_EVENT, STORAGE_TYPE } from '../src/constants.ts';
import { parse, parseV2 } from '../src/log.ts';
import {
  bleScanPerMinute, buildLogCharts, capSpans, chargingSpans, chartBuffers, imuSeries, motionSteps, recordingSegments,
  RECORDING_GAP_S, temperatureSeries, voltageSeries,
} from '../src/charts.ts';
import { buildDiagnosticsBody, buildTestPages, buildTestStream } from './helpers.ts';

const HERE = dirname(fileURLToPath(import.meta.url));
const fixture = (name: string) => parse(new Uint8Array(readFileSync(join(HERE, 'fixtures', name))));

const START = 1788301320;   // the experiment start buildTestStream writes into its details block
type Raw = [number, number, number[]];
const u16 = (value: number) => [value & 0xff, (value >>> 8) & 0xff];
const u32 = (value: number) => [value & 0xff, (value >>> 8) & 0xff, (value >>> 16) & 0xff, (value >>> 24) & 0xff];
const T = STORAGE_TYPE;
const voltage = (ms: number, mv: number): Raw => [T.STORAGE_TYPE_VOLTAGE, ms, u32(mv)];
const charging = (ms: number, event: number): Raw => [T.STORAGE_TYPE_CHARGING_EVENT, ms, [event]];
const motion = (ms: number, moving: boolean): Raw => [T.STORAGE_TYPE_MOTION, ms, [moving ? 1 : 0]];
const scan = (ms: number, peers: number[]): Raw => [T.STORAGE_TYPE_BLE_SCAN, ms, [peers.length, ...peers]];
const imu = (ms: number, x: number, y: number, z: number): Raw => [T.STORAGE_TYPE_IMU, ms, [7, ...u16(x & 0xffff), ...u16(y & 0xffff), ...u16(z & 0xffff)]];
const records = (raw: Raw[]) => parseV2(buildTestStream(raw)).records;
/** Many records, split across pages as the tag would: one page's payload length is only 16 bits. */
const manyRecords = (raw: Raw[]) => {
  const pages: Raw[][] = [];
  for (let i = 0; i < raw.length; i += 1000) pages.push(raw.slice(i, i + 1000));
  return parseV2(buildTestPages(pages)).records;
};
const values = (array: Float32Array) => [...array].map((v) => (Number.isNaN(v) ? 'break' : Math.round(v * 1000) / 1000));

test('recording runs until the tag falls silent for longer than two heartbeats', () => {
  const gap = RECORDING_GAP_S * 1000;
  const segments = recordingSegments(records([
    voltage(0, 4000), voltage(300_000, 4000), voltage(600_000, 4000),
    voltage(600_000 + gap + 1000, 3990), voltage(900_000 + gap + 1000, 3980),
  ]), START);
  assert.deepEqual([...segments], [START, START + 600, START + 600 + RECORDING_GAP_S + 1, START + 900 + RECORDING_GAP_S + 1]);
});

test('a small step backwards in time does not split or reorder recording', () => {
  const segments = recordingSegments(records([voltage(10_000, 4000), voltage(9_000, 4000), voltage(20_000, 4000)]), START);
  assert.deepEqual([...segments], [START + 9, START + 20]);
});

test('time on the charger comes from plugged and unplugged records', () => {
  const raw = records([
    charging(0, BATTERY_EVENT.BATTERY_UNPLUGGED),      // the boot-time report: not on the charger
    charging(100_000, BATTERY_EVENT.BATTERY_PLUGGED),
    charging(100_100, BATTERY_EVENT.BATTERY_CHARGING),  // charge state while plugged changes nothing
    charging(400_000, BATTERY_EVENT.BATTERY_PLUGGED),   // reported again at the restart: still the same stretch
    charging(900_000, BATTERY_EVENT.BATTERY_UNPLUGGED),
    charging(950_000, BATTERY_EVENT.BATTERY_CRITICAL_VOLTAGE),
    charging(1_000_000, BATTERY_EVENT.BATTERY_PLUGGED), // still on the charger when the log ends
    voltage(1_200_000, 4100),
  ]);
  const { spans, lowBattery } = chargingSpans(raw, START, START + 1200);
  assert.deepEqual([...spans], [START + 100, START + 900, START + 1000, START + 1200]);
  assert.deepEqual([...lowBattery], [START + 950]);
});

test('thousands of charger records are merged into a drawable number of stretches', () => {
  const pairs: number[] = [];
  for (let i = 0; i < 10_000; i += 1) pairs.push(i * 10, i * 10 + 5);
  const capped = capSpans(pairs, 100);
  assert.ok(capped.length / 2 <= 100);
  assert.equal(capped[0], 0);
  assert.equal(capped[capped.length - 1], 99_995, 'the whole extent survives merging');
});

test('motion is a step that holds each state, and stops where recording stopped', () => {
  const gap = (RECORDING_GAP_S + 100) * 1000;
  const raw = records([
    motion(0, false), motion(5_000, true), motion(6_000, true), motion(9_000, false), voltage(60_000, 4000),
    motion(60_000 + gap, true), voltage(90_000 + gap, 4000),
  ]);
  const steps = motionSteps(raw, START, recordingSegments(raw, START));
  assert.deepEqual([...steps.t], [START, START + 5, START + 9, START + 60, START + 60 + gap / 1000, START + 90 + gap / 1000]);
  assert.deepEqual(values(steps.v), [0, 1, 0, 'break', 1, 'break'], 'a repeated state is dropped and each stretch ends in a break');
});

test('Bluetooth scans count the distinct tags heard each minute, and a minute with no scans is a break', () => {
  const series = bleScanPerMinute(records([
    scan(1_000, [0x10, 0x11]), scan(1_500, [0x11, 0x12]), scan(59_000, []),
    scan(61_000, [0x10]),
    scan(185_000, [0x10, 0x11, 0x12, 0x13]),
  ]), START);
  assert.deepEqual([...series.t], [START, START + 60, START + 120, START + 180, START + 240]);
  assert.deepEqual(values(series.v), [3, 1, 'break', 4, 'break']);
});

test('battery readings are in volts, broken where heartbeats are missing', () => {
  const series = voltageSeries(records([voltage(0, 4012), voltage(300_000, 4005), voltage(3_000_000, 3900)]), START);
  assert.deepEqual(values(series.v), [4.012, 4.005, 'break', 3.9]);
});

test('temperature comes from the diagnostics records and skips one taken before the first reading', () => {
  const series = temperatureSeries(records([
    [T.STORAGE_TYPE_DIAGNOSTICS, 0, buildDiagnosticsBody({ temperature_c: 0x80 })],
    [T.STORAGE_TYPE_DIAGNOSTICS, 300_000, buildDiagnosticsBody({ temperature_c: 31 })],
    [T.STORAGE_TYPE_DIAGNOSTICS, 600_000, buildDiagnosticsBody({ temperature_c: 0xfe })],
  ]), START);
  assert.deepEqual(values(series.v), [31, -2]);
});

test('accelerometer axes are kept apart and broken between bursts of movement', () => {
  const raw = records([imu(0, 1, 2, 3), imu(900, -4, 5, 6), imu(60_000, 7, 8, -9)]);
  const { x, y, z } = imuSeries(raw, START);
  assert.deepEqual(values(x.v), [1, -4, 'break', 7]);
  assert.deepEqual(values(y.v), [2, 5, 'break', 8]);
  assert.deepEqual(values(z.v), [3, 6, 'break', -9]);
});

test('a long burst of movement is thinned, keeping its extremes', () => {
  const raw: Raw[] = [];
  for (let i = 0; i < 20_000; i += 1) raw.push(imu(i * 900, i === 12_345 ? 30_000 : 100, 0, 0));
  const parsed = manyRecords(raw);
  assert.equal(parsed.length, 20_000);
  const { x } = imuSeries(parsed, START, 500);
  assert.ok(x.t.length <= 2_002, `kept ${x.t.length} points`);
  assert.ok([...x.v].includes(30_000), 'the spike survives');
});

test('a real log reduces to small charts with everything in its place', () => {
  for (const name of ['boot.ttg', 'watchdog.ttg']) {
    const result = fixture(name);
    const charts = buildLogCharts(result);
    assert.equal(charts.start, result.experimentStartTime);
    assert.equal(charts.first, result.experimentStartTime + Math.min(...result.records.map((r) => r.ms)) / 1000);
    assert.ok(charts.recording.length >= 2, `${name} recorded something`);
    assert.ok(charts.voltage.t.length >= 2);
    assert.ok(charts.bleScan.t.length > 0);
    for (const t of charts.bleScan.t) assert.ok(t >= charts.start);
    // Every buffer can be handed over without copying, and none is shared.
    const buffers = chartBuffers(charts);
    assert.equal(new Set(buffers).size, buffers.length);
  }
  const boot = buildLogCharts(fixture('boot.ttg'));
  assert.ok(boot.motion.t.length > 10, 'the boot fixture moves');
  assert.ok(boot.imu.x.t.length > 10);
  // Three other tags were in range of the tag that wrote boot.ttg throughout.
  assert.equal(Math.max(...[...boot.bleScan.v].filter((v) => !Number.isNaN(v))), 3);
});

test('rounds that measured someone are counted per minute, and an empty round is not', async () => {
  const { rangedRoundsPerMinute } = await import('../src/charts.ts');
  const round = (ms: number, peers: Array<[number, number]>): Raw =>
    [T.STORAGE_TYPE_RANGES, ms, [peers.length, ...peers.flatMap(([uid, mm]) => [uid, mm & 0xff, mm >>> 8])]];
  const series = rangedRoundsPerMinute(records([round(0, [[1, 900]]), round(500, []), round(1000, [[1, 900], [2, 800]]), round(130_000, [[2, 10]])]), START);
  assert.deepEqual([...series.t], [START, START + 120]);
  assert.deepEqual([...series.v], [2, 1]);
});
