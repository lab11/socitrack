// Tests for the review page's decisions: which deployment each log belongs to, which clock it is shown
// on, and what the page says about that. Pure functions under node:test, against the real fixtures.

import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import { dirname, join, resolve } from 'node:path';

import { MANIFEST_KIND } from '@tottag/schema';
import { buildReport } from '../src/features/deployment.ts';
import { groupDeployments, readRecordFiles, zoneName, zoneSentence } from '../src/features/studyClock.ts';
import type { LoadedLog } from '../src/ports/logSource.ts';

const HERE = dirname(fileURLToPath(import.meta.url));
const FIXTURES = resolve(HERE, '..', '..', 'packages', 'tottag-schema', 'test', 'fixtures');
const loadFixture = (name: string): LoadedLog => ({ name, bytes: new Uint8Array(readFileSync(join(FIXTURES, name))), origin: 'file' });

const boot = await buildReport(loadFixture('boot.ttg'), 0);
const watchdog = await buildReport(loadFixture('watchdog.ttg'), 1);
const hex = (bytes: Uint8Array) => Array.from(bytes).map((b) => b.toString(16).padStart(2, '0')).join('');

const recordText = (fields: Record<string, unknown> = {}) => JSON.stringify({
  kind: MANIFEST_KIND,
  name: 'Family 10051',
  deployment: { timezone: 'America/Denver', startTime: 1787839200 },
  devices: [{ eui: 'C0:98:E5:42:02:02', label: 'Child' }],
  ...fields,
});

test('logs from the same deployment are grouped, and without a record use the setup page\'s zone, saying so', () => {
  const { deployments, unreadable } = groupDeployments([watchdog, boot], [], 'America/Chicago');
  assert.equal(unreadable.length, 0);
  assert.equal(deployments.length, 1, 'both fixtures come from one deployment');
  const [deployment] = deployments;
  assert.equal(deployment!.reports.length, 2);
  assert.equal(deployment!.timezone, 'America/Chicago');
  assert.equal(deployment!.fromRecord, false);
  const sentence = zoneSentence(deployment!);
  assert.equal(sentence.tone, 'warn');
  assert.match(sentence.text, /America\/Chicago/, 'the zone in use is always named');
  assert.match(sentence.text, /setup page/);
  assert.match(sentence.text, /deployment record/);
  assert.doesNotMatch(sentence.text, /UTC|device|badge/i);
});

test('a matching record puts the whole deployment on the study\'s clock and is named', () => {
  // Only boot.ttg can be identified as tag 02:02; watchdog.ttg cannot say which tag it is. The
  // deployment still takes the record's zone, because its logs belong together.
  const { added } = readRecordFiles([{ name: 'family-10051.json', text: recordText() }], []);
  const { deployments } = groupDeployments([watchdog, boot], added, 'America/Chicago');
  const [deployment] = deployments;
  assert.equal(deployment!.timezone, 'America/Denver');
  assert.equal(deployment!.fromRecord, true);
  assert.equal(deployment!.matchedBy, 'start-and-tag');
  assert.match(deployment!.title, /^Family 10051 \(2 tags\)$/);
  const sentence = zoneSentence(deployment!);
  assert.equal(sentence.tone, 'info');
  assert.match(sentence.text, /America\/Denver/);
  assert.match(sentence.text, /Family 10051/);
});

test('an exact match on the experiment details wins over one on start time and tag', () => {
  const byStart = recordText({ name: 'By start', deployment: { timezone: 'Asia/Tokyo', startTime: 1787839200 } });
  const exact = recordText({ name: 'Exact', deployment: { timezone: 'Europe/Paris', startTime: 1 }, experimentDetailsHex: hex(boot.report!.details!) });
  const { added } = readRecordFiles([{ name: 'a.json', text: byStart }, { name: 'b.json', text: exact }], []);
  const { deployments } = groupDeployments([boot], added, 'UTC');
  assert.equal(deployments[0]!.record?.record.name, 'Exact');
  assert.equal(deployments[0]!.matchedBy, 'details');
});

test('a record for another deployment changes nothing', () => {
  const { added } = readRecordFiles([{ name: 'other.json', text: recordText({ deployment: { timezone: 'Asia/Tokyo', startTime: 1787839201 } }) }], []);
  const { deployments } = groupDeployments([boot], added, 'America/Chicago');
  assert.equal(deployments[0]!.timezone, 'America/Chicago');
  assert.equal(deployments[0]!.fromRecord, false);
});

test('records are read once, and a file that is not one is reported unless it came from a whole folder', () => {
  const first = readRecordFiles([{ name: 'r.json', text: recordText() }], []);
  assert.equal(first.added.length, 1);
  const again = readRecordFiles([{ name: 'copy.json', text: recordText() }], first.added);
  assert.equal(again.added.length, 0, 'the same record opened twice is one record');

  const chosen = readRecordFiles([{ name: 'settings.json', text: '{"theme":"dark"}' }], []);
  assert.equal(chosen.failed.length, 1);
  assert.match(chosen.failed[0]!.reason, /not a deployment record/);
  const fromFolder = readRecordFiles([{ name: 'settings.json', text: '{"theme":"dark"}' }], [], true);
  assert.equal(fromFolder.failed.length, 0, 'a stray .json in a folder of downloads is passed over');
  const broken = readRecordFiles([{ name: 'r.json', text: recordText({ deployment: { startTime: 1 } }) }], [], true);
  assert.equal(broken.failed.length, 1, 'a record that cannot be used is reported even from a folder');
});

test('a log that could not be read is kept apart from the deployments', async () => {
  const junk = await buildReport({ name: 'notes.ttg', bytes: new TextEncoder().encode('hello'), origin: 'file' }, 2);
  const { deployments, unreadable } = groupDeployments([junk, boot], [], 'UTC');
  assert.deepEqual(unreadable.map((r) => r.name), ['notes.ttg']);
  assert.equal(deployments.length, 1);
});

test('a zone is named the way people say it, with its exact name beside it', () => {
  assert.match(zoneName('America/Chicago'), /\(America\/Chicago\)$/);
  assert.equal(zoneName('UTC').includes('UTC'), true);
});
