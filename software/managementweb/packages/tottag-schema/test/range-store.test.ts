// Every ranging round, kept compactly; windows onto it; and daily time together on the study's calendar.

import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import { dirname, join } from 'node:path';

import { STORAGE_TYPE } from '../src/constants.ts';
import { analyseDeployment } from '../src/health.ts';
import { parse, parseV2, type ParseResult } from '../src/log.ts';
import { ROUNDS_PER_MINUTE } from '../src/radioCheck.ts';
import { buildRangeStore, dailyRangingStats, NO_RANGE, rangeStoreBytes, rangeStoreSpan, rangeWindow } from '../src/rangeStore.ts';
import { parseEventLog } from '../src/events.ts';
import { buildTestPages, buildTestStream } from './helpers.ts';

const HERE = dirname(fileURLToPath(import.meta.url));
const START = 1788301320;   // the experiment start buildTestStream writes: 2026-09-01 22:22 UTC
type Raw = [number, number, number[]];
const ranges = (ms: number, peers: Array<[number, number]>): Raw =>
  [STORAGE_TYPE.STORAGE_TYPE_RANGES, ms, [peers.length, ...peers.flatMap(([uid, mm]) => [uid, mm & 0xff, mm >>> 8])]];
const parsed = (raw: Raw[]): ParseResult => {
  const pages: Raw[][] = [];
  for (let i = 0; i < raw.length; i += 1000) pages.push(raw.slice(i, i + 1000));
  return parseV2(pages.length > 1 ? buildTestPages(pages) : buildTestStream(raw));
};

test('the store keeps one column per peer and a gap marker where a peer was not ranged', () => {
  const store = buildRangeStore(parsed([ranges(0, [[0x10, 1200], [0x20, 800]]), ranges(500, [[0x20, 850]]), ranges(1000, [[0x10, 1300]])]));
  assert.deepEqual(store.peers, [0x10, 0x20]);
  assert.deepEqual([...store.ms], [0, 500, 1000]);
  assert.deepEqual([...store.mm[0]!], [1200, NO_RANGE, 1300]);
  assert.deepEqual([...store.mm[1]!], [800, 850, NO_RANGE]);
  assert.equal(rangeStoreBytes(store), 3 * (4 + 2 * 2), 'four bytes a round, and two per peer');
  assert.deepEqual(rangeStoreSpan(store), [START, START + 1]);
});

test('rounds written out of order after a clock correction are put in order', () => {
  const store = buildRangeStore(parsed([ranges(2000, [[0x10, 1]]), ranges(1000, [[0x10, 2]]), ranges(3000, [[0x10, 3]])]));
  assert.deepEqual([...store.ms], [1000, 2000, 3000]);
  assert.deepEqual([...store.mm[0]!], [2, 1, 3]);
});

test('a window holds every round in it, a neighbor either side, and breaks where rounds stop', () => {
  const raw: Raw[] = [];
  for (let ms = 0; ms < 60_000; ms += 500) if (ms < 20_000 || ms >= 30_000) raw.push(ranges(ms, [[0x10, 1000 + ms / 500]]));
  const store = buildRangeStore(parsed(raw));
  const window = rangeWindow(store, 0x10, START + 10, START + 40, 10_000);
  const t = [...window.t];
  assert.equal(t[0], START + 9.5, 'the round before the window');
  assert.equal(t[t.length - 1], START + 40.5, 'the round after it');
  const breaks = [...window.v].filter(Number.isNaN).length;
  assert.equal(breaks, 1, 'the ten seconds with no rounds are a break, not a line');
  assert.equal(window.t.length, 21 + 22 + 1);
  assert.equal(rangeWindow(store, 0x99, START, START + 60, 100).t.length, 0, 'a peer it never ranged to has no data');
});

test('a wide window is thinned but keeps the closest approach', () => {
  const raw: Raw[] = [];
  for (let i = 0; i < 20_000; i += 1) raw.push(ranges(i * 500, [[0x10, i === 7_777 ? 150 : 3000]]));
  const window = rangeWindow(buildRangeStore(parsed(raw)), 0x10, START, START + 10_000, 300);
  assert.ok(window.t.length <= 4 * 300 + 2, `kept ${window.t.length}`);
  assert.ok([...window.v].includes(150));
});

test('daily statistics count rounds on the study\'s own calendar, two rounds a second', () => {
  // START is 5:22 PM in Chicago on Sep 1. Rounds run for two hours, to 7:22 PM: all of them on September 1
  // in Chicago, though those after 7:00 PM (midnight UTC) are on September 2 in UTC.
  const raw: Raw[] = [];
  for (let i = 0; i < 2 * 3600 * 2; i += 1) raw.push(ranges(i * 500, [[0x10, i % 2 ? 500 : 2500], [0x20, 4000]]));
  // Then one minute exactly on the next local day, in range of 0x10 only.
  const nextDay = (24 * 3600 - 17 * 3600 - 22 * 60) * 1000 + 3600_000;   // 1 AM Sep 2 Chicago
  for (let i = 0; i < ROUNDS_PER_MINUTE; i += 1) raw.push(ranges(nextDay + i * 500, [[0x10, 900]]));
  const store = buildRangeStore(parsed(raw));

  const rows = dailyRangingStats(store, { timezone: 'America/Chicago', peers: [0x10, 0x20], touchingMm: 1000, cutoffMm: 9144 });
  assert.deepEqual(rows.map((row) => row.date), ['2026-09-01', '2026-09-02']);
  const [first, second] = rows;
  assert.equal(first!.peers[0]!.minutesInRange, 120);
  assert.equal(first!.peers[0]!.minutesTouching, 60, 'half the rounds were at 500 mm');
  assert.equal(first!.peers[0]!.meanMm, 1500);
  assert.equal(first!.peers[1]!.minutesInRange, 120);
  assert.equal(first!.peers[1]!.minutesTouching, 0);
  assert.equal(first!.any!.minutesInRange, 120);
  assert.equal(first!.any!.minutesTouching, 60);
  assert.equal(first!.all!.minutesInRange, 120);
  assert.equal(first!.all!.minutesTouching, 0, 'never touching both at once');
  assert.equal(first!.any!.meanMm, (1500 + 4000) / 2, 'the mean is over every distance counted');
  assert.equal(second!.peers[0]!.minutesInRange, 1, 'one minute of rounds is one minute, not two');
  assert.equal(second!.peers[1]!.minutesInRange, 0);
  assert.equal(second!.peers[1]!.meanMm, null);
  assert.equal(second!.all!.minutesInRange, 0);
  assert.equal(rows[1]!.dayStart, Date.parse('2026-09-02T05:00:00Z') / 1000);

  // The same rounds on UTC's calendar split differently, which is the mistake this exists to avoid.
  const utc = dailyRangingStats(store, { timezone: 'UTC', peers: [0x10], touchingMm: 1000, cutoffMm: 9144 });
  assert.equal(utc[0]!.date, '2026-09-01');
  assert.ok(utc[0]!.peers[0]!.minutesInRange < 120);
  assert.equal(utc[0]!.any, null, 'with one peer there is no "any" or "all"');
});

test('distances beyond the cutoff are not counted as in range', () => {
  const store = buildRangeStore(parsed([ranges(0, [[0x10, 5000]]), ranges(500, [[0x10, 15000]])]));
  const [row] = dailyRangingStats(store, { timezone: 'UTC', peers: [0x10], touchingMm: 0, cutoffMm: 9144 });
  assert.equal(row!.peers[0]!.rounds, 1);
  assert.equal(row!.peers[0]!.meanMm, 5000);
});

test('a peer the log never ranged to has a row of zeros, not an error', () => {
  const store = buildRangeStore(parsed([ranges(0, [[0x10, 5000]])]));
  const [row] = dailyRangingStats(store, { timezone: 'UTC', peers: [0x55], touchingMm: 900, cutoffMm: 9144 });
  assert.equal(row!.peers[0]!.minutesInRange, 0);
  assert.deepEqual(dailyRangingStats(buildRangeStore(parsed([])), { timezone: 'UTC', peers: [1], touchingMm: 1, cutoffMm: 2 }), []);
});

test('on a real log, the store holds exactly the rounds the analysis counted', () => {
  const result = parse(new Uint8Array(readFileSync(join(HERE, 'fixtures', 'boot.ttg'))));
  const store = buildRangeStore(result);
  const health = analyseDeployment(result);
  for (const [peer, count] of health.rangingPeers) {
    const column = store.mm[store.peers.indexOf(peer)]!;
    assert.equal(column.filter((value) => value !== NO_RANGE).length, count, `peer ${peer}`);
  }
  const rows = dailyRangingStats(store, { timezone: 'America/Chicago', peers: [...health.rangingPeers.keys()], touchingMm: 1000, cutoffMm: 32000 });
  const minutes = rows.reduce((sum, row) => sum + row.peers[0]!.minutesInRange, 0);
  assert.equal(minutes, health.rangingPeers.get(store.peers[0]!)! / ROUNDS_PER_MINUTE);
});

// --- Noted events ---------------------------------------------------------------------------------

test('noted events are read on the study\'s clock, in time order, skipping what is not an event', () => {
  const text = '﻿Date Time Event\r\n2026-09-02 17:30:05 Lunch\r\n\r\n2026-09-02 07:00:00 Arrived at daycare\nnot a line\n2026-09-02 18:00:00\n2026-02-31 10:00:00 impossible\n';
  const log = parseEventLog(text, 'America/Chicago');
  assert.deepEqual(log.events.map((e) => e.message), ['Arrived at daycare', 'Lunch', '']);
  assert.equal(log.events[0]!.t, Date.parse('2026-09-02T12:00:00Z') / 1000);
  assert.equal(log.events[1]!.t, Date.parse('2026-09-02T22:30:05Z') / 1000, 'seconds are kept');
  assert.deepEqual(log.skipped, [1, 5, 7]);
  assert.equal(log.events[1]!.line, 2);
});
