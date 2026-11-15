// Thinning and the study's clock over a span: the two things every chart rests on.

import { test } from 'node:test';
import assert from 'node:assert/strict';

import { breakAtGaps, seriesFrom, seriesRange, thinSeries, type Series } from '../src/series.ts';
import { dayNumberToDate, localClock, localDayStart, utcOffsetSecondsAt } from '../src/timezone.ts';

const utc = (iso: string) => Date.parse(iso) / 1000;
const series = (points: Array<[number, number]>): Series => seriesFrom(points.map((p) => p[0]), points.map((p) => p[1]));

test('a series is put in time order, keeping equal times in the order given', () => {
  const s = series([[3, 30], [1, 10], [2, 20], [2, 21]]);
  assert.deepEqual([...s.t], [1, 2, 2, 3]);
  assert.deepEqual([...s.v], [10, 20, 21, 30]);
});

test('thinning keeps every extreme, and keeps them in the order they happened', () => {
  // A flat 3.9 V with one brief dip to 3.2 V and one spike to 4.1 V, 100 000 points.
  const times: number[] = [];
  const values: number[] = [];
  for (let i = 0; i < 100_000; i += 1) {
    times.push(i);
    values.push(i === 31_337 ? 3.2 : i === 77_777 ? 4.1 : 3.9);
  }
  const thin = thinSeries(seriesFrom(times, values), 0, 99_999, 200);
  assert.ok(thin.t.length <= 4 * 200 + 2, `kept ${thin.t.length} points`);
  const range = seriesRange(thin)!;
  assert.ok(Math.abs(range[0] - 3.2) < 1e-6, 'the dip survives');
  assert.ok(Math.abs(range[1] - 4.1) < 1e-6, 'the spike survives');
  for (let i = 1; i < thin.t.length; i += 1) assert.ok(thin.t[i]! >= thin.t[i - 1]!, 'time order is kept');
  assert.ok([...thin.t].includes(31_337) && [...thin.t].includes(77_777), 'extremes stay at the times they happened');
});

test('thinning keeps breaks exactly where they were', () => {
  const times: number[] = [];
  const values: number[] = [];
  for (let i = 0; i < 10_000; i += 1) {
    times.push(i);
    values.push(i === 5_000 ? Number.NaN : Math.sin(i / 50));
  }
  const thin = thinSeries(seriesFrom(times, values), 0, 9_999, 50);
  const breaks = [...thin.v].map((value, i) => (Number.isNaN(value) ? thin.t[i] : null)).filter((x) => x !== null);
  assert.deepEqual(breaks, [5_000]);
});

test('a window that holds few points is returned whole, with one neighbor either side', () => {
  const s = series([[0, 1], [10, 2], [20, 3], [30, 4], [40, 5]]);
  const thin = thinSeries(s, 15, 25, 100);
  assert.deepEqual([...thin.t], [10, 20, 30], 'the neighbors let the line run to the edges of the window');
});

test('a break is inserted where points are further apart than the data allows', () => {
  const s = breakAtGaps(series([[0, 1], [300, 2], [5000, 3], [5300, 4]]), 900);
  const values = [...s.v];
  assert.equal(values.filter(Number.isNaN).length, 1);
  assert.ok(Number.isNaN(values[2]!));
  assert.equal(s.t[2], 300 + 900, 'the break sits where the data stopped');
});

test('a series with nothing in it has no range', () => {
  assert.equal(seriesRange(series([])), null);
  assert.equal(seriesRange(series([[0, Number.NaN]])), null);
});

test('the local clock agrees with Intl across a change of the clocks, and is fast', () => {
  const zone = 'America/Chicago';
  const from = utc('2026-10-30T00:00:00Z');
  const to = utc('2026-11-05T00:00:00Z');   // the clocks go back on Sunday, November 1
  const clock = localClock(zone, from, to);
  for (let t = from; t < to; t += 1799) assert.equal(clock.offsetAt(t), utcOffsetSecondsAt(zone, t), `at ${t}`);
  const started = performance.now();
  let sum = 0;
  for (let i = 0; i < 1_000_000; i += 1) sum += clock.dayNumber(from + i * 0.5);
  assert.ok(sum > 0);
  assert.ok(performance.now() - started < 1500, 'a million rounds must be placed in well under a second or two');
});

test('a local day runs from local midnight to local midnight', () => {
  const zone = 'America/Chicago';
  const clock = localClock(zone, utc('2026-10-31T00:00:00Z'), utc('2026-11-03T00:00:00Z'));
  // 11:30 PM on Oct 31 in Chicago is 04:30 UTC on Nov 1: still October 31 locally.
  const lateEvening = utc('2026-11-01T04:30:00Z');
  assert.equal(dayNumberToDate(clock.dayNumber(lateEvening)), '2026-10-31');
  assert.equal(dayNumberToDate(clock.dayNumber(lateEvening + 3600)), '2026-11-01');
  const day = clock.dayNumber(lateEvening + 3600);
  assert.equal(localDayStart(day, zone), utc('2026-11-01T05:00:00Z'));
  assert.equal(localDayStart(day + 1, zone) - localDayStart(day, zone), 25 * 3600, 'the day the clocks go back is 25 hours long');
});
