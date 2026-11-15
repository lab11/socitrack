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
  assert.ok(differences.some((d) => /different end time/.test(d)), differences.join('; '));
  assert.ok(differences.some((d) => /stored 1 tag\(s\) in the deployment where 2 were sent/.test(d)), differences.join('; '));
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
  assert.match(manifest.summary, /1 of 2 tags confirmed/);
  assert.match(manifest.summary, /1 tag did not take it/);
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

test('each tag is named the way the screen names it, with the byte that has to be unique spelled out', () => {
  const manifest = buildManifest(config, bytes, [
    { eui: eui(0x7a, 1, 2, 3, 4, 5), label: 'x', outcome: 'confirmed' },
  ], new Date());
  assert.equal(manifest.devices[0]!.name, 'TotTag 01:7A');
  assert.equal(manifest.devices[0]!.shortUid, '0x7A');
  assert.equal(manifest.devices[0]!.eui, '05:04:03:02:01:7A');
});

test('times are on the local clock with the zone named, and nobody is asked to read UTC', () => {
  // 13:00 to 03:00 on the tag's clock is 8:00 AM to 10:00 PM in Chicago in August.
  const manifest = buildManifest(config, bytes, [], new Date('2026-09-11T21:00:00Z'), 'Family 10051');
  assert.equal(manifest.name, 'Family 10051');
  assert.equal(manifest.deployment.timezone, 'America/Chicago');
  assert.deepEqual(manifest.deployment.dailyHours, { start: '08:00', end: '22:00' });
  assert.equal(manifest.deployment.start, '2026-09-01T17:00');
  const text = manifestToText(manifest, 'en-US');
  assert.match(text, /Family 10051/);
  assert.match(text, /8:00 AM to 10:00 PM each day/);
  assert.doesNotMatch(text, /UTC/);
});

test('a clock change during the deployment is spelled out with the hours that follow it', () => {
  const autumn: DeploymentConfig = {
    ...config,
    startTime: Date.parse('2026-10-30T12:00:00Z') / 1000,
    endTime: Date.parse('2026-11-03T12:00:00Z') / 1000,
    dailyStartTime: 12 * 3600,
    dailyEndTime: 1 * 3600,
  };
  const manifest = buildManifest(autumn, encodeExperimentDetails(autumn), [], new Date());
  assert.deepEqual(manifest.deployment.dailyHours, { start: '07:00', end: '20:00' });
  assert.equal(manifest.deployment.afterClockChange.length, 1);
  assert.deepEqual(manifest.deployment.afterClockChange[0]!.dailyHours, { start: '06:00', end: '19:00' });
  assert.match(manifestToText(manifest, 'en-US'), /clocks change on Sunday, November 1; from then on, 6:00 AM to 7:00 PM each day/);
});

test('the text companion names every device and its outcome', () => {
  const text = manifestToText(buildManifest(config, bytes, [
    { eui: config.devices[0]!.eui, label: 'S1', outcome: 'confirmed' },
    { eui: config.devices[1]!.eui, label: '', outcome: 'unconfirmed', detail: 'Disconnected before read-back' },
  ], new Date('2026-09-11T21:00:00Z')));
  assert.match(text, /S1/);
  assert.match(text, /\(no label\)/);
  assert.match(text, /could not be checked/);
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
