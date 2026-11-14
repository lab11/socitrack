// The review page's analysis: what the log worker answers, how each restart is explained, how tags are
// named, and what the time-together export contains. Run against the real fixtures under node:test,
// where there is no Worker and the same code runs in place.

import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import { dirname, join, resolve } from 'node:path';

import { parseExperimentDetails, ROUNDS_PER_MINUTE, type DailyRow, type RebootEvent } from '@tottag/schema';
import { buildReport } from '../src/features/deployment.ts';
import { explainRestart } from '../src/features/plainLanguage.ts';
import { logName, logTag, tagRef } from '../src/features/tagNames.ts';
import { tableLines, timeTogetherCsv, timeTogetherFilename } from '../src/features/timeTogether.ts';
import { releaseLogs, requestDailyStats, requestRangeWindow } from '../src/lib/parseAsync.ts';
import type { LoadedLog } from '../src/ports/logSource.ts';

const HERE = dirname(fileURLToPath(import.meta.url));
const FIXTURES = resolve(HERE, '..', '..', 'packages', 'tottag-schema', 'test', 'fixtures');
const loadFixture = (name: string): LoadedLog => ({ name, bytes: new Uint8Array(readFileSync(join(FIXTURES, name))), origin: 'file' });

const boot = await buildReport(loadFixture('boot.ttg'), 0);
const watchdog = await buildReport(loadFixture('watchdog.ttg'), 1);
const scheduled = parseExperimentDetails(boot.report!.details!);

test('a parsed log carries its charts and names the peers whose rounds are kept', () => {
  assert.ok(boot.charts);
  assert.equal(boot.charts!.start, 1787839200);
  assert.deepEqual([...boot.rangePeers].sort((a, b) => a - b), [...boot.health!.rangingPeers.keys()].sort((a, b) => a - b));
  assert.ok(boot.rangeSpan && boot.rangeSpan[1] > boot.rangeSpan[0]);
});

test('daily figures and windows come back for a kept log, and stop once it is let go', async () => {
  const peer = boot.rangePeers[0]!;
  const rows = await requestDailyStats(boot.id, { timezone: 'America/Chicago', peers: [peer], touchingMm: 1000, cutoffMm: 32000 });
  assert.ok(rows && rows.length === 1);
  assert.equal(rows![0]!.peers[0]!.minutesInRange, boot.health!.rangingPeers.get(peer)! / ROUNDS_PER_MINUTE);

  const [from, to] = boot.rangeSpan!;
  const window = await requestRangeWindow(boot.id, peer, from, from + 30, 10_000);
  assert.ok(window && window.t.length >= 50 && window.t.length <= 64, `30 s at two rounds a second, got ${window?.t.length}`);
  const overview = await requestRangeWindow(boot.id, peer, from, to, 100);
  assert.ok(overview && overview.t.length <= 4 * 100 + 2);

  releaseLogs([boot.id]);
  assert.equal(await requestDailyStats(boot.id, { timezone: 'UTC', peers: [peer], touchingMm: 1, cutoffMm: 2 }), null,
    'a log let go of says so rather than answering with nothing');
});

test('every restart in the fixtures is explained in plain words', () => {
  const graceful = explainRestart(boot.health!.reboots[0]!);
  assert.equal(graceful.tone, 'ok');
  assert.doesNotMatch(graceful.title, /stuck/);
  const stall = explainRestart(watchdog.health!.reboots[0]!);
  assert.equal(stall.tone, 'warn');
  assert.match(stall.title, /stuck|stopped/i);
});

test('a restart caused by the charger is called that, whatever else it was', () => {
  const base: RebootEvent = {
    ms: 0, kind: 'graceful', causes: ['SW Power-On'], diagnosticLabel: null, silenceSeconds: 2, outageSeconds: 3600 * 9,
    shape: null, chargerTransition: true,
  };
  const story = explainRestart(base);
  assert.match(story.title, /charger/);
  assert.match(story.detail, /off for about 9 hours/);
  assert.match(explainRestart({ ...base, chargerTransition: false, kind: 'other', causes: ['Brown-Out'] }).title, /Battery dipped/);
  assert.match(explainRestart({ ...base, chargerTransition: false, kind: 'other', causes: ['Power-On'] }).title, /Powered on/);
  const fault = explainRestart({ ...base, chargerTransition: false, kind: 'watchdog', shape: 'fault', diagnosticLabel: 'hard fault', silenceSeconds: 40 });
  assert.match(fault.detail, /hard fault/);
  assert.match(fault.detail, /About 40 seconds of recording/);
});

test('tags are named with the last two parts of their address and their label', () => {
  const tag = tagRef(0x02, scheduled);
  assert.equal(tag.name, 'TotTag 02:02');
  assert.equal(tag.display, '02 (TotTag 02:02)');
  assert.match(tagRef(0x99, scheduled).display, /not in this deployment/);
  assert.equal(logTag(boot, scheduled)?.uid, 0x02);
  assert.equal(logName(watchdog, scheduled), 'watchdog.ttg', 'a log that cannot say which tag wrote it keeps its file name');
});

test('the time-together export has one line per day and tag, with the settings that made it', () => {
  const figures = (rounds: number, touching: number, meanMm: number | null) => ({
    rounds, touchingRounds: touching, minutesInRange: rounds / 120, minutesTouching: touching / 120, meanMm,
  });
  const rows: DailyRow[] = [{
    date: '2026-09-01', dayStart: 0,
    peers: [figures(240, 60, 914.4), figures(0, 0, null)],
    any: figures(240, 60, 914.4), all: figures(0, 0, null),
  }];
  const table = { tag: 'Child, A (TotTag 02:02)', peers: ['Mother', 'Father'], rows, unit: 'ft' as const, touching: 3, cutoff: 30, timezone: 'America/Chicago' };
  assert.deepEqual(tableLines(table).map((line) => line.with), ['Mother', 'Father', 'Either one', 'Both at once']);
  const csv = timeTogetherCsv(table).trimEnd().split('\n');
  assert.equal(csv.length, 5);
  assert.equal(csv[0], 'date,tag,with,minutes_in_range,minutes_within_3_ft,mean_distance_ft,rounds_in_range,rounds_within_touching,cutoff_ft,touching_ft,time_zone');
  assert.equal(csv[1], '2026-09-01,"Child, A (TotTag 02:02)",Mother,2.0,0.5,3.00,240,60,30,3,America/Chicago');
  assert.equal(csv[2], '2026-09-01,"Child, A (TotTag 02:02)",Father,0.0,0.0,,0,0,30,3,America/Chicago', 'no distances is blank, not zero');
  assert.equal(timeTogetherFilename('Child, A (TotTag 02:02)', '2026-09-01'), 'time-together-Child-A-2026-09-01.csv');
});

// --- What happened during a deployment ------------------------------------------------------------

import { BATTERY_EVENT, STORAGE_TYPE } from '@tottag/schema';
import { buildTestPages } from '../../packages/tottag-schema/test/helpers.ts';
import { deploymentStory } from '../src/features/deploymentStory.ts';
import { groupDeployments } from '../src/features/studyClock.ts';

const STORY_START = Date.parse('2026-09-01T12:00:00Z') / 1000;   // 7 AM in Chicago
const HOUR = 3600;

/** A two-tag deployment recording 7 AM to 10 PM Chicago, built record by record. */
function synthetic(self: number, other: number, options: { gap?: [number, number]; stopAt?: number }) {
  const details = new Uint8Array(239);
  const view = new DataView(details.buffer);
  view.setUint32(0, STORY_START, true);
  view.setUint32(4, STORY_START + 2 * 86400, true);
  view.setUint32(8, 12 * HOUR, true);   // 12:00 UTC is 7 AM in Chicago
  view.setUint32(12, 3 * HOUR, true);   // 03:00 UTC is 10 PM
  details[16] = 1;
  details[17] = 2;
  details.set([0x0a, 0x02, 0x42, 0xe5, 0x98, 0xc0], 18);
  details.set([0x0b, 0x02, 0x42, 0xe5, 0x98, 0xc0], 24);
  const u32 = (x: number) => [x & 0xff, (x >> 8) & 0xff, (x >> 16) & 0xff, (x >>> 24) & 0xff];
  const records: Array<[number, number, number[]]> = [];
  const end = options.stopAt ?? 15 * HOUR;
  for (let s = 0; s < end; s += 60) {
    if (options.gap && s >= options.gap[0] && s < options.gap[1]) continue;
    if (s % 300 === 0) records.push([STORAGE_TYPE.STORAGE_TYPE_VOLTAGE, s * 1000, u32(4000)]);
    records.push([STORAGE_TYPE.STORAGE_TYPE_RANGES, s * 1000 + 10, [1, other, 0xe8, 0x03]]);
  }
  records.push([STORAGE_TYPE.STORAGE_TYPE_CHARGING_EVENT, end * 1000, [BATTERY_EVENT.BATTERY_PLUGGED]]);
  records.push([STORAGE_TYPE.STORAGE_TYPE_CHARGING_EVENT, (end + 8 * HOUR) * 1000, [BATTERY_EVENT.BATTERY_UNPLUGGED]]);
  records.push([STORAGE_TYPE.STORAGE_TYPE_VOLTAGE, (end + 8 * HOUR) * 1000 + 10, u32(4150)]);
  const pages: Array<typeof records> = [];
  for (let i = 0; i < records.length; i += 500) pages.push(records.slice(i, i + 500));
  return buildTestPages(pages, details);
}

test('the story of a deployment reports missed hours and time on the charger', async () => {
  const a = await buildReport({ name: 'a.ttg', bytes: synthetic(0x0a, 0x0b, { gap: [4 * HOUR, 6 * HOUR] }), origin: 'file' }, 50);
  const b = await buildReport({ name: 'b.ttg', bytes: synthetic(0x0b, 0x0a, { stopAt: 15 * HOUR }), origin: 'file' }, 51);
  const [deployment] = groupDeployments([a, b], [], 'America/Chicago').deployments;
  const story = deploymentStory(deployment!)!;
  assert.equal(story.tags.length, 2);
  const tagA = story.tags.find((tag) => tag.tag?.uid === 0x0a)!;
  assert.equal(tagA.missed.length, 2, 'one stretch is missed');
  assert.ok(Math.abs(tagA.missed[0]! - (STORY_START + 4 * HOUR - 60)) < 1, 'from the last record before the gap');
  assert.equal(tagA.missed[1], STORY_START + 6 * HOUR, 'to the first record after it');
  assert.ok(Math.abs(tagA.secondsMissed - 2 * HOUR) <= 120, `two hours missed, got ${tagA.secondsMissed}`);
  const kinds = tagA.events.map((event) => event.kind);
  assert.ok(kinds.includes('missed'));
  assert.ok(kinds.includes('charging'));
  const charging = tagA.events.find((event) => event.kind === 'charging')!;
  assert.match(charging.title, /On the charger for 8 hours/);
  assert.equal(story.verdict.tone, 'warn');
  assert.ok(story.verdict.findings.some((finding) => /not recording for 2 hours/.test(finding)), story.verdict.findings.join(' | '));
  // Events from every tag, in time order, each named.
  for (let i = 1; i < story.events.length; i += 1) assert.ok(story.events[i]!.t >= story.events[i - 1]!.t);
  assert.ok(story.events.every((event) => /TotTag 02:0[AB]/.test(event.tag)));
  releaseLogs([a.id, b.id]);
});

test('a tag whose log ends hours before the others is said to have stopped early', async () => {
  const a = await buildReport({ name: 'a.ttg', bytes: synthetic(0x0a, 0x0b, {}), origin: 'file' }, 60);
  const b = await buildReport({ name: 'b.ttg', bytes: synthetic(0x0b, 0x0a, { stopAt: 9 * HOUR }), origin: 'file' }, 61);
  const story = deploymentStory(groupDeployments([a, b], [], 'America/Chicago').deployments[0]!)!;
  const tagB = story.tags.find((tag) => tag.tag?.uid === 0x0b)!;
  const stopped = tagB.events.find((event) => event.kind === 'stopped');
  assert.ok(stopped, 'tag B stopped early');
  assert.match(stopped!.title, /before the other tags/);
  releaseLogs([a.id, b.id]);
});

test('the fixtures tell a story with their restarts explained and the clock correction placed', () => {
  const [deployment] = groupDeployments([boot, watchdog], [], 'America/Chicago').deployments;
  const story = deploymentStory(deployment!)!;
  const kinds = story.events.map((event) => event.kind);
  assert.equal(kinds.filter((kind) => kind === 'restart').length, 2);
  assert.equal(kinds.filter((kind) => kind === 'clock').length, 1);
  assert.equal(story.verdict.tone, 'warn', 'the watchdog restart is worth a look');
  assert.ok(story.verdict.findings.some((finding) => /restarted unexpectedly once/.test(finding)));
});

// --- What the charts draw ---------------------------------------------------------------------------

import { clockMarkers, cutoffIn, DEFAULT_SETTINGS, noteMarkers, rangeMinuteSeries, restartMarkers, spansOf, touchingIn } from '../src/features/chartData.ts';

test('distance per minute comes from the radio summary, broken at missing minutes and beyond the cutoff', () => {
  const radio = {
    ...boot.radio!, experimentStartTime: 1000, binMs: 60_000,
    peers: [{ uid: 7, bins: [[0, 120, 914.4, 10], [1, 120, 9144 * 2, 10], [2, 120, 1828.8, 10], [5, 60, 304.8, 5]] as const }],
  };
  const series = rangeMinuteSeries(radio, 7, 30, 'ft');
  const values = [...series.v].map((v) => (Number.isNaN(v) ? 'break' : Math.round(v * 10) / 10));
  assert.deepEqual(values, [3, 'break', 6, 'break', 1, 'break']);
  assert.equal(series.t[0], 1030, 'each point sits in the middle of its minute');
  assert.equal(rangeMinuteSeries(radio, 99, 30, 'ft').t.length, 0);
});

test('restarts, clock corrections and notes become markers at the times they happened', () => {
  const start = boot.charts!.start;
  const restarts = restartMarkers(boot.health!, start);
  assert.equal(restarts.length, 1);
  assert.equal(restarts[0]!.t, start + boot.health!.reboots[0]!.ms / 1000);
  assert.equal(restarts[0]!.kind, 'restart');
  assert.equal(restartMarkers(watchdog.health!, watchdog.charts!.start)[0]!.kind, 'restart-warn');
  assert.match(clockMarkers(boot.health!, start)[0]!.label, /Clock corrected by 4\.1 s/);
  assert.deepEqual(noteMarkers([{ t: 100, message: '', line: 1 }], -30), [{ t: 70, kind: 'note', label: 'Noted event' }]);
  assert.deepEqual(spansOf(Float64Array.from([1, 2, 3, 4]), 'charging'), [{ from: 1, to: 2, kind: 'charging' }, { from: 3, to: 4, kind: 'charging' }]);
});

test('switching units shows the same distances in the new unit, and switching back returns exactly what was typed', () => {
  const metric = { ...DEFAULT_SETTINGS, unit: 'm' as const };
  assert.equal(cutoffIn(DEFAULT_SETTINGS), 30);
  assert.equal(cutoffIn(metric), 9.1);
  assert.equal(touchingIn(metric), 0.9);
  assert.equal(cutoffIn({ ...metric, unit: 'ft' }), 30, 'no drift from a round trip');
});
