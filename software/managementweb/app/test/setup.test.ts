// Setting up a deployment: what is typed, what the tags are told, and when replacing what a tag holds
// could lose data. Pure logic, run under node:test with no DOM.

import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import { dirname, join, resolve } from 'node:path';

import { encodeExperimentDetails, parseExperimentDetails, type ExperimentDetails } from '@tottag/schema';
import {
  defaultDraft, describeSchedule, draftProblems, draftToConfig, migrateV1Draft, nextDraft, recordBaseName,
  type DeploymentDraft,
} from '../src/features/schedule.ts';
import { assessSnapshot, batteryLow, replaceDecision, storedFromDetails } from '../src/features/tagStatus.ts';
import { PLAIN_FINDINGS } from '../src/features/plainLanguage.ts';
import { buildReport } from '../src/features/deployment.ts';
import { summariseLog } from '../src/features/logSummary.ts';

const HERE = dirname(fileURLToPath(import.meta.url));
const utc = (iso: string) => Date.parse(iso) / 1000;
const eui = (low: number) => Uint8Array.of(low, 0x00, 0x42, 0xe5, 0x98, 0xc0);

const family: DeploymentDraft = {
  name: 'Family 10051',
  timezone: 'America/Chicago',
  start: '2026-10-10T07:00',
  end: '2026-10-12T21:00',
  useDailyTimes: true,
  dailyStart: '07:00',
  dailyEnd: '20:00',
  clockChangePolicy: 'start',
  devices: [{ eui: eui(0x0a), label: '10051_CG1' }, { eui: eui(0x0c), label: '10051_TC' }],
};
const BEFORE = utc('2026-10-08T15:00:00Z');

test('the times typed are on the study clock, and the tags are told the matching instants', () => {
  const config = draftToConfig(family);
  assert.equal(config.startTime, utc('2026-10-10T12:00:00Z'));
  assert.equal(config.endTime, utc('2026-10-13T02:00:00Z'));
  // 7:00 AM to 8:00 PM in Chicago in October, on the tag's own clock.
  assert.equal(config.dailyStartTime, 12 * 3600);
  assert.equal(config.dailyEndTime, 1 * 3600);
  assert.deepEqual(draftProblems(family, BEFORE), []);
});

test('changing the time zone moves the deployment, rather than relabelling it', () => {
  const denver = draftToConfig({ ...family, timezone: 'America/Denver' });
  assert.equal(denver.startTime - draftToConfig(family).startTime, 3600);
});

test('an unknown time zone is one clear problem, not a cascade about the dates', () => {
  const problems = draftProblems({ ...family, timezone: 'Central' }, BEFORE);
  assert.equal(problems[0]!.code, 'bad-timezone');
  assert.ok(!problems.some((p) => p.code === 'missing-start' || p.code === 'missing-end'));
});

test('a blank or half-typed date blocks the write', () => {
  for (const partial of [{ end: '' }, { start: '0002-10-10T07:00' }, { dailyStart: '' }]) {
    const problems = draftProblems({ ...family, ...partial }, BEFORE);
    assert.ok(problems.some((p) => p.severity === 'error'), JSON.stringify(partial));
  }
});

test('last week\'s dates left in the draft are caught', () => {
  const problems = draftProblems(family, utc('2026-10-20T12:00:00Z'));
  assert.ok(problems.some((p) => p.code === 'already-over' && p.severity === 'error'));
});

test('the schedule reads back in words, with no UTC in sight', () => {
  const summary = describeSchedule(family, 'en-US');
  assert.equal(summary.complete, true);
  assert.equal(summary.hours, '7:00 AM – 8:00 PM each day');
  assert.equal(summary.length, '2 days, 14 hours');
  assert.match(summary.zone, /America\/Chicago/);
  assert.equal(summary.clockChange, null);
  assert.doesNotMatch(JSON.stringify(summary), /UTC/);
});

test('a deployment spanning the clock change offers each way of handling it', () => {
  const autumn: DeploymentDraft = { ...family, start: '2026-10-30T07:00', end: '2026-11-03T21:00' };
  const summary = describeSchedule(autumn, 'en-US');
  assert.ok(summary.clockChange);
  assert.equal(summary.clockChange.day, 'Sunday, November 1');
  assert.equal(summary.clockChange.options.start.after, '6:00 AM – 7:00 PM');
  assert.equal(summary.clockChange.options.end.before, '8:00 AM – 9:00 PM');
  assert.equal(summary.clockChange.options.cover.before, '7:00 AM – 9:00 PM');
  assert.equal(summary.clockChange.options.cover.after, '6:00 AM – 8:00 PM');
  // The choice is what the tags are told.
  assert.notEqual(draftToConfig(autumn).dailyStartTime, draftToConfig({ ...autumn, clockChangePolicy: 'end' }).dailyStartTime);
});

test('a new deployment keeps the time zone and daily hours, and nothing that belonged to the last one', () => {
  const next = nextDraft(family, BEFORE);
  assert.equal(next.timezone, family.timezone);
  assert.equal(next.dailyStart, family.dailyStart);
  assert.equal(next.useDailyTimes, true);
  assert.equal(next.devices.length, 0);
  assert.equal(next.name, '');
  assert.deepEqual(draftProblems({ ...next, devices: family.devices }, BEFORE), []);
  assert.equal(defaultDraft(BEFORE, 'America/Chicago').start, '2026-10-09T00:00');
});

test('a draft saved by the previous version comes back as the same local times', () => {
  const migrated = migrateV1Draft({
    startTime: utc('2026-10-10T12:00:00Z'), endTime: utc('2026-10-13T02:00:00Z'), timezone: 'America/Chicago',
    useDailyTimes: true, dailyStartTime: 13 * 3600, dailyEndTime: 3 * 3600, devices: [{ eui: [10, 0, 0x42, 0xe5, 0x98, 0xc0], label: 'x' }],
  });
  assert.equal(migrated.start, '2026-10-10T07:00');
  assert.equal(migrated.dailyStart, '08:00');
  assert.equal(migrated.dailyEnd, '22:00');
  // A blank date stored as null by the old version comes back blank, not as 1970.
  assert.equal(migrateV1Draft({ ...{ startTime: null, endTime: null, timezone: 'America/Chicago', useDailyTimes: false, dailyStartTime: 0, dailyEndTime: 0, devices: [] } }).start, '');
});

test('the record is named after the deployment, or the family ID its labels share', () => {
  assert.equal(recordBaseName(family), 'Family-10051');
  assert.equal(recordBaseName({ ...family, name: '' }), '10051');
  assert.equal(recordBaseName({ ...family, name: '', devices: [] }), 'tottag-deployment');
});

// --- What a tag holds --------------------------------------------------------------------------------

const previousFamily = (start: number): ExperimentDetails => parseExperimentDetails(encodeExperimentDetails({
  startTime: start, endTime: start + 3 * 86400, timezone: 'America/Chicago', useDailyTimes: false,
  dailyStartTime: 0, dailyEndTime: 0, devices: [{ eui: eui(0x0b), label: '10043_CG2' }],
}));

test('a tag holding an earlier study that may have recorded needs a say before it is replaced', () => {
  const config = draftToConfig(family);
  const earlier = previousFamily(utc('2026-10-01T12:00:00Z'));
  const check = assessSnapshot({ batteryMv: 4100, timestamp: Math.round(BEFORE), experiment: earlier }, null, BEFORE);
  assert.equal(replaceDecision(check, config, BEFORE, false, null), 'ask');
  assert.equal(replaceDecision(check, config, BEFORE, true, null), 'downloaded', 'saved from this browser');
  assert.equal(replaceDecision(check, config, BEFORE, false, earlier.experimentStartTime), 'allowed');
  assert.equal(replaceDecision(check, config, BEFORE, false, 12345), 'ask', 'permission was for a different deployment');
});

test('nothing to lose: an empty tag, an upcoming study, a cancelled one, or this same deployment', () => {
  const config = draftToConfig(family);
  const none = assessSnapshot({ batteryMv: 4100, timestamp: null, experiment: null }, null, BEFORE);
  assert.equal(replaceDecision(none, config, BEFORE, false, null), 'none');
  const upcoming = assessSnapshot({ batteryMv: 4100, timestamp: null, experiment: previousFamily(utc('2026-11-20T12:00:00Z')) }, null, BEFORE);
  assert.equal(replaceDecision(upcoming, config, BEFORE, false, null), 'none');
  const cancelledDetails = { ...previousFamily(utc('2026-10-01T12:00:00Z')), isTerminated: true };
  assert.equal(replaceDecision(assessSnapshot({ batteryMv: null, timestamp: null, experiment: cancelledDetails }, null, BEFORE), config, BEFORE, false, null), 'none');
  const same = parseExperimentDetails(encodeExperimentDetails(config));
  const during = utc('2026-10-11T12:00:00Z');
  assert.equal(replaceDecision(assessSnapshot({ batteryMv: null, timestamp: null, experiment: same }, null, during), config, during, false, null), 'same');
});

test('battery and clock read as a person would want them', () => {
  assert.equal(batteryLow(3600), true);
  assert.equal(batteryLow(3750), false);
  assert.equal(batteryLow(null), false);
  const check = assessSnapshot({ batteryMv: 4000, timestamp: Math.round(BEFORE) + 12, experiment: null }, 'fw', BEFORE);
  assert.equal(check.clockOffsetSeconds, 12);
  assert.equal(assessSnapshot({ batteryMv: 4000, timestamp: 5, experiment: null }, null, BEFORE).clockSet, false);
  assert.equal(storedFromDetails(null), null);
});

// --- Logs --------------------------------------------------------------------------------------------

test('every finding the analysis can raise has a plain-language version', () => {
  const health = readFileSync(resolve(HERE, '..', '..', 'packages', 'tottag-schema', 'src', 'health.ts'), 'utf8');
  const codes = [...health.matchAll(/code: '([a-z0-9-]+)'/g)].map((match) => match[1]!);
  assert.ok(codes.length > 10);
  const missing = codes.filter((code) => !(code in PLAIN_FINDINGS));
  assert.deepEqual(missing, [], 'add these to PLAIN_FINDINGS in app/src/features/plainLanguage.ts');
});

test('a log card names its tag and lists the other tags it was scheduled with', async () => {
  const fixtures = resolve(HERE, '..', '..', 'packages', 'tottag-schema', 'test', 'fixtures');
  const report = await buildReport({ name: 'boot.ttg', bytes: new Uint8Array(readFileSync(join(fixtures, 'boot.ttg'))), origin: 'file' }, 0);
  const summary = summariseLog(report);
  assert.ok(summary.scheduled, 'the fixture carries a details block');
  assert.ok(summary.peers.length > 0);
  assert.ok(summary.peers.every((peer) => peer.name.length > 0));
  assert.ok(summary.firstRecord !== null && summary.lastRecord !== null && summary.lastRecord >= summary.firstRecord);
});
