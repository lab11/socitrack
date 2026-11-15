// Ticks on a time axis fall on round times of the study's own clock, whatever the computer's zone.

import { test } from 'node:test';
import assert from 'node:assert/strict';

import { utcOffsetSecondsAt } from '@tottag/schema';
import { formatClock, formatInstant, tickStep, timeTicks, valueTicks } from '../src/features/timeAxis.ts';

const utc = (iso: string) => Date.parse(iso) / 1000;
const CHICAGO = 'America/Chicago';
const localSecondsOfDay = (t: number, zone: string) => (((t + utcOffsetSecondsAt(zone, t)) % 86400) + 86400) % 86400;

test('the spacing is the smallest readable one that fits', () => {
  assert.equal(tickStep(3600, 6), 600);
  assert.equal(tickStep(86400, 8), 3 * 3600);
  assert.equal(tickStep(7 * 86400, 8), 86400);
  assert.equal(tickStep(30, 6), 5);
});

test('hourly ticks fall on the hour in the study\'s zone, not UTC', () => {
  // Kolkata is UTC+5:30, so its hours are on UTC half-hours: a UTC-based axis would be visibly wrong.
  const from = utc('2026-08-24T00:00:00Z');
  const ticks = timeTicks(from, from + 12 * 3600, 'Asia/Kolkata', 12, undefined, 'en-US');
  assert.ok(ticks.length >= 6);
  for (const tick of ticks) assert.equal(localSecondsOfDay(tick.t, 'Asia/Kolkata') % 3600, 0, `${tick.label} is not on the hour`);
  assert.equal(ticks[0]!.date !== null, true, 'the first tick names its date');
});

test('a date is named on the first tick and wherever the day changes', () => {
  const from = utc('2026-08-24T20:00:00Z');   // 3 PM in Chicago
  const ticks = timeTicks(from, from + 18 * 3600, CHICAGO, 9, undefined, 'en-US');
  const dated = ticks.filter((tick) => tick.date !== null);
  assert.equal(dated.length, 2);
  assert.equal(localSecondsOfDay(dated[1]!.t, CHICAGO), 0, 'the second date sits on local midnight');
});

test('daily ticks sit on local midnight on both sides of a change of the clocks', () => {
  const from = utc('2026-10-29T12:00:00Z');
  const ticks = timeTicks(from, from + 6 * 86400, CHICAGO, 8, undefined, 'en-US');
  assert.ok(ticks.length >= 5);
  for (const tick of ticks) assert.equal(localSecondsOfDay(tick.t, CHICAGO), 0);
  const gaps = ticks.slice(1).map((tick, i) => tick.t - ticks[i]!.t);
  assert.ok(gaps.includes(25 * 3600), 'the day the clocks go back is 25 hours long on the axis');
});

test('an hour the clocks repeat gets one tick, and one they skip gets none', () => {
  const back = timeTicks(utc('2026-11-01T05:00:00Z'), utc('2026-11-01T09:00:00Z'), CHICAGO, 10, undefined, 'en-US');
  const labels = back.map((tick) => tick.label);
  assert.equal(new Set(labels).size, labels.length, `a label appears twice: ${labels.join(', ')}`);
  const forward = timeTicks(utc('2027-03-14T06:00:00Z'), utc('2027-03-14T10:00:00Z'), CHICAGO, 10, undefined, 'en-US');
  assert.ok(!forward.some((tick) => tick.label.startsWith('2:')), 'there is no 2 AM on the day the clocks go forward');
  for (let i = 1; i < forward.length; i += 1) assert.ok(forward[i]!.t > forward[i - 1]!.t);
});

test('weekly ticks fall on Mondays', () => {
  const from = utc('2026-08-01T12:00:00Z');
  const ticks = timeTicks(from, from + 60 * 86400, CHICAGO, 9, undefined, 'en-US');
  assert.ok(ticks.length >= 5);
  for (const tick of ticks) assert.match(tick.label, /^Mon/);
});

test('times read on the study\'s clock, with seconds where they matter', () => {
  const t = utc('2026-08-24T12:05:30Z');
  assert.equal(formatClock(t, CHICAGO, false, 'en-US'), '7:05 AM');
  assert.equal(formatClock(t, CHICAGO, true, 'en-US'), '7:05:30 AM');
  assert.match(formatInstant(t, CHICAGO, true, 'en-US'), /Aug 24, 2026.*7:05:30/);
});

test('value ticks are round numbers covering the range', () => {
  assert.deepEqual(valueTicks(3.61, 4.18, 4), [3.8, 4]);
  assert.deepEqual(valueTicks(0, 30, 3), [0, 10, 20, 30]);
  assert.deepEqual(valueTicks(5, 5), [5]);
});
