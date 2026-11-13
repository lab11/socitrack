// Wall-clock conversion and what a tag is told for its daily window.
//
// These are the only places a deployment's hours are translated between the local clock people use
// and the tag's own clock, so every case a study actually meets is pinned here: an ordinary week, a
// week the clocks go back, a week they go forward, and a half-typed date.

import { test } from 'node:test';
import assert from 'node:assert/strict';

import {
  clockChanges, deviceDailyWindow, formatSpan, formatTimeOfDay, isValidTimezone, localDailyWindow,
  parseTimeOfDay, timeOfDayInput, unixToWallClock, utcOffsetSecondsAt, wallClockToUnix,
} from '../src/timezone.ts';

const CHICAGO = 'America/Chicago';
const utc = (iso: string) => Date.parse(iso) / 1000;
const hours = (h: number, m = 0) => h * 3600 + m * 60;

test('a local reading converts to the instant it names, and back', () => {
  assert.equal(wallClockToUnix('2026-10-10T07:00', CHICAGO), utc('2026-10-10T12:00:00Z'));
  assert.equal(unixToWallClock(utc('2026-10-10T12:00:00Z'), CHICAGO), '2026-10-10T07:00');
  assert.equal(wallClockToUnix('2026-12-10T07:00', CHICAGO), utc('2026-12-10T13:00:00Z'), 'standard time is an hour further from UTC');
  assert.equal(wallClockToUnix('2026-10-10T07:00', 'Asia/Kolkata'), utc('2026-10-10T01:30:00Z'), 'half-hour zones work');
});

test('the time zone chosen decides the instant, not the computer the page runs on', () => {
  const chicago = wallClockToUnix('2026-10-10T07:00', CHICAGO);
  const newYork = wallClockToUnix('2026-10-10T07:00', 'America/New_York');
  assert.equal(chicago - newYork, 3600);
});

test('a half-typed or impossible date is not a date', () => {
  // The first digit typed into the year segment arrives as year 0002.
  assert.ok(Number.isNaN(wallClockToUnix('0002-10-10T07:00', CHICAGO)));
  assert.ok(Number.isNaN(wallClockToUnix('', CHICAGO)));
  assert.ok(Number.isNaN(wallClockToUnix('2026-02-31T07:00', CHICAGO)));
  assert.ok(Number.isNaN(wallClockToUnix('2026-10-10T07:00', 'Central')), 'an unknown zone cannot place a time');
  assert.ok(Number.isNaN(parseTimeOfDay('')));
  assert.equal(parseTimeOfDay('07:30'), hours(7, 30));
  assert.equal(timeOfDayInput(hours(25)), '01:00');
});

test('a time skipped when the clocks go forward lands an hour on; a repeated one takes the first', () => {
  // 2:30 AM on 8 March 2026 never happens in Chicago.
  assert.equal(unixToWallClock(wallClockToUnix('2026-03-08T02:30', CHICAGO), CHICAGO), '2026-03-08T03:30');
  // 1:30 AM on 1 November 2026 happens twice; the first is still daylight time.
  assert.equal(wallClockToUnix('2026-11-01T01:30', CHICAGO), utc('2026-11-01T06:30:00Z'));
});

test('zone names are checked', () => {
  assert.ok(isValidTimezone(CHICAGO));
  assert.ok(!isValidTimezone('Central'));
  assert.ok(!isValidTimezone(''));
});

test('the clock change inside a deployment is found to the minute', () => {
  const changes = clockChanges(CHICAGO, utc('2026-10-30T12:00:00Z'), utc('2026-11-03T12:00:00Z'));
  assert.equal(changes.length, 1);
  assert.equal(changes[0]!.at, utc('2026-11-01T07:00:00Z'));
  assert.equal(changes[0]!.offsetBefore, -5 * 3600);
  assert.equal(changes[0]!.offsetAfter, -6 * 3600);
  assert.deepEqual(clockChanges(CHICAGO, utc('2026-10-10T12:00:00Z'), utc('2026-10-13T02:00:00Z')), []);
  assert.deepEqual(clockChanges('UTC', utc('2026-10-30T12:00:00Z'), utc('2026-11-03T12:00:00Z')), []);
});

test('an ordinary week: the tag is told the local hours on its own clock', () => {
  const start = utc('2026-10-10T12:00:00Z');
  const end = utc('2026-10-13T02:00:00Z');
  const window = deviceDailyWindow(hours(7), hours(20), CHICAGO, start, end, 'start');
  assert.deepEqual(localDailyWindow(window, CHICAGO, start), { start: hours(7), end: hours(20) });
  assert.equal(window.start, hours(12));
  assert.equal(window.end, hours(1), 'a window that crosses midnight on the tag clock wraps rather than going past a day');
  assert.equal(utcOffsetSecondsAt(CHICAGO, start), -5 * 3600);
});

test('the week the clocks go back: each choice keeps the hours it promises', () => {
  const start = utc('2026-10-30T12:00:00Z');
  const end = utc('2026-11-03T12:00:00Z');
  const before = utc('2026-10-31T12:00:00Z');
  const after = utc('2026-11-02T12:00:00Z');
  const asked = { start: hours(7), end: hours(20) };

  const keepStart = deviceDailyWindow(asked.start, asked.end, CHICAGO, start, end, 'start');
  assert.deepEqual(localDailyWindow(keepStart, CHICAGO, before), asked);
  assert.deepEqual(localDailyWindow(keepStart, CHICAGO, after), { start: hours(6), end: hours(19) });

  const keepEnd = deviceDailyWindow(asked.start, asked.end, CHICAGO, start, end, 'end');
  assert.deepEqual(localDailyWindow(keepEnd, CHICAGO, before), { start: hours(8), end: hours(21) });
  assert.deepEqual(localDailyWindow(keepEnd, CHICAGO, after), asked);

  const cover = deviceDailyWindow(asked.start, asked.end, CHICAGO, start, end, 'cover');
  assert.deepEqual(localDailyWindow(cover, CHICAGO, before), { start: hours(7), end: hours(21) });
  assert.deepEqual(localDailyWindow(cover, CHICAGO, after), { start: hours(6), end: hours(20) });
});

test('the week the clocks go forward: covering widens the other end', () => {
  const start = utc('2026-03-06T12:00:00Z');
  const end = utc('2026-03-10T12:00:00Z');
  const cover = deviceDailyWindow(hours(7), hours(20), CHICAGO, start, end, 'cover');
  assert.deepEqual(localDailyWindow(cover, CHICAGO, utc('2026-03-07T12:00:00Z')), { start: hours(6), end: hours(20) });
  assert.deepEqual(localDailyWindow(cover, CHICAGO, utc('2026-03-09T12:00:00Z')), { start: hours(7), end: hours(21) });
});

test('people read times and spans in words', () => {
  assert.equal(formatTimeOfDay(hours(7), 'en-US'), '7:00 AM');
  assert.equal(formatTimeOfDay(hours(20, 30), 'en-US'), '8:30 PM');
  assert.equal(formatSpan(2 * 86400 + 14 * 3600), '2 days, 14 hours');
  assert.equal(formatSpan(3600), '1 hour');
  assert.equal(formatSpan(45 * 60), '45 minutes');
});
