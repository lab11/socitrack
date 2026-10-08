import { test } from 'node:test';
import assert from 'node:assert/strict';

import { buildManifest, diffReadback, manifestToText, MANIFEST_KIND } from '../src/manifest.ts';
import { encodeExperimentDetails, type DeploymentConfig } from '../src/deployment.ts';
import { parseExperimentDetails } from '../src/log.ts';

const eui = (...b: number[]) => Uint8Array.from(b);
const config: DeploymentConfig = {
  startTime: 1_788_300_000,
  endTime: 1_788_300_000 + 2 * 86400,
  timezone: 'America/Chicago',
  useDailyTimes: true,
  dailyStartTime: 13 * 3600,
  dailyEndTime: 3 * 3600,
  devices: [
    { eui: eui(0x11, 0x22, 0x33, 0x44, 0x55, 0x66), label: 'S1' },
    { eui: eui(0x12, 0x22, 0x33, 0x44, 0x55, 0x66), label: 'CG1' },
  ],
};
const bytes = encodeExperimentDetails(config);

test('a device that stored what was sent shows no differences', () => {
  assert.deepEqual(diffReadback(config, parseExperimentDetails(bytes)), []);
});

test('a device that stored something different says exactly what', () => {
  // The whole point of reading back: a device that silently accepted different settings is the
  // failure this catches, and "it did not match" is not enough to act on months later.
  const tampered = Uint8Array.from(bytes);
  new DataView(tampered.buffer).setUint32(4, config.endTime - 3600, true);
  tampered[17] = 1;
  const differences = diffReadback(config, parseExperimentDetails(tampered));
  assert.ok(differences.some((d) => d.startsWith('end time:')), differences.join('; '));
  assert.ok(differences.some((d) => d.startsWith('device count:')), differences.join('; '));
});

test('the manifest records outcomes per device and totals them honestly', () => {
  const manifest = buildManifest(config, bytes, [
    { eui: config.devices[0]!.eui, label: 'S1', outcome: 'confirmed', firmware: 'Commit abc1234' },
    { eui: config.devices[1]!.eui, label: 'CG1', outcome: 'failed', detail: 'Timed out' },
  ], new Date('2026-09-11T21:00:00Z'));

  assert.equal(manifest.kind, MANIFEST_KIND);
  assert.equal(manifest.devices.length, 2);
  assert.equal(manifest.devices[0]!.outcome, 'confirmed');
  assert.equal(manifest.devices[1]!.detail, 'Timed out');
  assert.match(manifest.summary, /1 of 2 TotTag\(s\) confirmed/);
  assert.match(manifest.summary, /1 did not take it/);
});

test('the manifest carries the exact bytes written', () => {
  // So a manifest can be compared byte for byte against another, or replayed, without depending on
  // this app still existing or its field names still meaning the same thing.
  const manifest = buildManifest(config, bytes, [], new Date());
  assert.equal(manifest.experimentDetailsHex.length, bytes.length * 2);
  const restored = Uint8Array.from(
    manifest.experimentDetailsHex.match(/../g)!.map((pair) => parseInt(pair, 16)),
  );
  assert.deepEqual([...restored], [...bytes]);
  assert.deepEqual(diffReadback(config, parseExperimentDetails(restored)), []);
});

test('the short UID is spelled out, because it is the byte that has to be unique', () => {
  const manifest = buildManifest(config, bytes, [
    { eui: eui(0x7a, 1, 2, 3, 4, 5), label: 'x', outcome: 'confirmed' },
  ], new Date());
  assert.equal(manifest.devices[0]!.shortUid, '0x7A');
  assert.equal(manifest.devices[0]!.eui, '05:04:03:02:01:7A');
});

test('daily times are written in UTC and labelled as such', () => {
  // Rendering them in the entry timezone would read better and would also make an off-by-an-hour
  // argument unresolvable a year later. The device compares against UTC.
  const manifest = buildManifest(config, bytes, [], new Date());
  assert.equal(manifest.deployment.dailyStartUtc, '13:00:00');
  assert.equal(manifest.deployment.dailyEndUtc, '03:00:00');
  assert.match(manifestToText(manifest), /13:00:00 to 03:00:00 UTC/);
});

test('the text companion names every device and its outcome', () => {
  const text = manifestToText(buildManifest(config, bytes, [
    { eui: config.devices[0]!.eui, label: 'S1', outcome: 'confirmed' },
    { eui: config.devices[1]!.eui, label: '', outcome: 'unconfirmed', detail: 'Disconnected before read-back' },
  ], new Date('2026-09-11T21:00:00Z')));
  assert.match(text, /S1/);
  assert.match(text, /<unlabelled>/);
  assert.match(text, /unconfirmed/);
  assert.match(text, /Disconnected before read-back/);
});

test('the manifest is plain JSON with no undefined-only keys lost in a round trip', () => {
  const manifest = buildManifest(config, bytes, [
    { eui: config.devices[0]!.eui, label: 'S1', outcome: 'confirmed' },
  ], new Date());
  const round = JSON.parse(JSON.stringify(manifest));
  assert.equal(round.devices[0].eui, manifest.devices[0]!.eui);
  assert.equal(round.experimentDetailsHex, manifest.experimentDetailsHex);
  assert.equal(round.deployment.timezone, 'America/Chicago');
});
