// Detecting which firmware wrote a log, from evidence in the log.

import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import { dirname, join } from 'node:path';

import { parseV2 } from '../src/log.ts';
import { analyseDeployment } from '../src/health.ts';
import { detectFirmwareEra } from '../src/firmwareEra.ts';
import { DIAGNOSTICS_LAYOUT, STORAGE_TYPE, TIME_ALIGNED_INTERVAL_S } from '../src/constants.ts';
import { buildTestStream } from './helpers.ts';

const FIXTURES = join(dirname(fileURLToPath(import.meta.url)), 'fixtures');
const read = (name: string) => parseV2(new Uint8Array(readFileSync(join(FIXTURES, name))));

test('the committed fixtures predate the current firmware, and the detector says so', () => {
  // Found by building this detector and pointing it at them. Both real fixtures report stream
  // format 1 — UNFRAMED — and contain no diagnostics records at all. They were cut from the 3.9-day
  // deployment, which ran before record framing and the diagnostics record shipped together.
  //
  // Pinned rather than corrected, because the consequence is worth keeping visible: the framed
  // record path and the entire near-miss section have never been exercised against real data. If a
  // newer capture is committed this test fails, which is the moment to re-run the analysis against
  // it and see what was only ever true of synthetic bytes.
  for (const name of ['boot.ttg', 'watchdog.ttg']) {
    const result = read(name);
    assert.equal(result.report.formatVersion, 1, `${name} is unframed`);
    const health = analyseDeployment(result);
    assert.equal(health.era.framedRecords, false, name);
    assert.notEqual(health.era.writesDiagnostics, 'yes', `${name} carries no diagnostics record`);
    assert.equal(health.nearMisses.samples, 0, `${name} has no near-miss data to analyse`);
    // What they DO carry, and what the analysis is therefore genuinely validated against.
    assert.equal(health.era.writesTimeAnchors, 'yes', name);
    assert.equal(health.era.recordsResetReason, 'yes', name);
  }
});

test('a short log says "unknown", not "old"', () => {
  // The trap: diagnostics are written once per TimeAlignedTask pass, so a ten-minute log from
  // current firmware contains none. Concluding "old firmware" there would make every message about
  // device behaviour wrong on exactly the logs people look at most — short test runs.
  const brief = buildTestStream([
    [STORAGE_TYPE.STORAGE_TYPE_VOLTAGE, 0, [0, 16, 0, 0]],
    [STORAGE_TYPE.STORAGE_TYPE_VOLTAGE, 60_000, [0, 16, 0, 0]],
  ]);
  const era = detectFirmwareEra(parseV2(brief).records, parseV2(brief).report, 60);
  assert.equal(era.writesDiagnostics, 'unknown');
  assert.equal(era.chargerDebounced, 'unknown');
});

test('a long log with no diagnostics is conclusively older firmware', () => {
  const span = Math.ceil(3 * TIME_ALIGNED_INTERVAL_S);
  const long = buildTestStream([
    [STORAGE_TYPE.STORAGE_TYPE_VOLTAGE, 0, [0, 16, 0, 0]],
    [STORAGE_TYPE.STORAGE_TYPE_VOLTAGE, span * 1000, [0, 16, 0, 0]],
  ]);
  const parsed = parseV2(long);
  const era = detectFirmwareEra(parsed.records, parsed.report, span);
  assert.equal(era.writesDiagnostics, 'no');
  assert.equal(era.writesTimeAnchors, 'no');
  // But the de-bounce still cannot be concluded absent: it shipped before the diagnostics record,
  // so a log from between the two has one and not the other.
  assert.equal(era.chargerDebounced, 'unknown');
});

test('a per-event record missing from a healthy run is never concluded absent', () => {
  // A log with no reboot has no reset record. That says nothing about the firmware.
  const span = Math.ceil(3 * TIME_ALIGNED_INTERVAL_S);
  const stream = buildTestStream([[STORAGE_TYPE.STORAGE_TYPE_VOLTAGE, 0, [0, 16, 0, 0]]]);
  const parsed = parseV2(stream);
  assert.equal(detectFirmwareEra(parsed.records, parsed.report, span).recordsResetReason, 'unknown');
});

test('the charger-storm message does not assert a de-bounce it cannot prove', () => {
  // This is the regression that motivated the whole module: the message asserted behaviour that
  // firmware before 626b34bc did not have, on logs it could not have come from.
  const span = Math.ceil(3 * TIME_ALIGNED_INTERVAL_S);
  const records: Array<[number, number, number[]]> = [];
  for (let i = 0; i < 150; i += 1) records.push([STORAGE_TYPE.STORAGE_TYPE_CHARGING_EVENT, i * 500, [4]]);
  records.push([STORAGE_TYPE.STORAGE_TYPE_VOLTAGE, span * 1000, [0, 16, 0, 0]]);

  const health = analyseDeployment(parseV2(buildTestStream(records)));
  const storm = health.anomalies.find((anomaly) => anomaly.code === 'charging-event-storm');
  assert.ok(storm);
  assert.equal(health.era.chargerDebounced, 'unknown');
  assert.match(storm.message, /cannot be confirmed/i, 'it should hedge when the era is unknown');
  assert.doesNotMatch(storm.message, /This firmware accepts a charger edge only when/);
});

test('and does assert it on a log that proves it', () => {
  // Synthetic, because no real capture in this repo contains a diagnostics record — see the fixture
  // test above. A diagnostics record is the evidence that the de-bounce exists.
  const records: Array<[number, number, number[]]> = [];
  for (let i = 0; i < 150; i += 1) records.push([STORAGE_TYPE.STORAGE_TYPE_CHARGING_EVENT, i * 500, [4]]);
  records.push([STORAGE_TYPE.STORAGE_TYPE_DIAGNOSTICS, 300_000, new Array(DIAGNOSTICS_LAYOUT.size).fill(0)]);

  const health = analyseDeployment(parseV2(buildTestStream(records)));
  assert.equal(health.era.writesDiagnostics, 'yes');
  assert.equal(health.era.chargerDebounced, 'yes');
  const storm = health.anomalies.find((anomaly) => anomaly.code === 'charging-event-storm');
  assert.ok(storm);
  assert.match(storm.message, /This firmware accepts a charger edge only when/);
  assert.doesNotMatch(storm.message, /cannot be confirmed/i);
});

test('the evidence is stated, not just the conclusion', () => {
  const health = analyseDeployment(read('boot.ttg'));
  assert.ok(health.era.evidence.length >= 3);
  for (const line of health.era.evidence) assert.ok(line.length > 5);
});
