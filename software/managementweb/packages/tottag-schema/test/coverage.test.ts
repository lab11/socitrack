// When a tag was meant to record, against when it did, laid out by hour on the study's calendar.

import { test } from 'node:test';
import assert from 'node:assert/strict';

import {
  coverageGrid, isScheduledAt, missedStretches, overlapSeconds, scheduledIntervals, subtractIntervals,
} from '../src/coverage.ts';
import type { ExperimentDetails } from '../src/log.ts';

const utc = (iso: string) => Date.parse(iso) / 1000;
const START = utc('2026-09-01T12:00:00Z');   // 7 AM in Chicago
const details = (overrides: Partial<ExperimentDetails> = {}): ExperimentDetails => ({
  experimentStartTime: START, experimentEndTime: START + 3 * 86400,
  dailyStartTime: 12 * 3600, dailyEndTime: 3 * 3600, useDailyTimes: true,   // 7 AM to 10 PM Chicago, as UTC
  numDevices: 2, uids: [], labels: [], isTerminated: false, ...overrides,
});

test('the schedule follows the firmware\'s own test, including a window past midnight on the tag\'s clock', () => {
  const d = details();
  assert.equal(isScheduledAt(d, utc('2026-09-01T15:00:00Z')), true, '10 AM Chicago');
  assert.equal(isScheduledAt(d, utc('2026-09-02T02:00:00Z')), true, '9 PM Chicago, which is past midnight UTC');
  assert.equal(isScheduledAt(d, utc('2026-09-02T08:00:00Z')), false, '3 AM Chicago');
  assert.equal(isScheduledAt(d, START - 1), false, 'before the deployment');
  assert.equal(isScheduledAt(details({ useDailyTimes: false }), utc('2026-09-02T08:00:00Z')), true);
  assert.equal(isScheduledAt(details({ dailyStartTime: 5, dailyEndTime: 5 }), utc('2026-09-02T08:00:00Z')), true,
    'equal start and end never stops the tag');
  const plain = details({ dailyStartTime: 13 * 3600, dailyEndTime: 23 * 3600 });
  assert.equal(isScheduledAt(plain, utc('2026-09-01T14:00:00Z')), true);
  assert.equal(isScheduledAt(plain, utc('2026-09-01T23:30:00Z')), false);
});

test('scheduled stretches are each day\'s window, clipped to the deployment and the span asked about', () => {
  const stretches = scheduledIntervals(details(), START, START + 2 * 86400);
  assert.deepEqual(stretches, [
    START, utc('2026-09-02T03:00:00Z'),
    utc('2026-09-02T12:00:00Z'), utc('2026-09-03T03:00:00Z'),
  ]);
  assert.deepEqual(scheduledIntervals(details({ useDailyTimes: false }), START - 100, START + 100), [START, START + 100]);
  assert.deepEqual(scheduledIntervals(details(), START - 200, START - 100), []);
});

test('stretches subtract and overlap as intervals should', () => {
  assert.deepEqual(subtractIntervals([0, 100], [10, 20, 50, 60]), [0, 10, 20, 50, 60, 100]);
  assert.deepEqual(subtractIntervals([0, 100, 200, 300], [90, 210]), [0, 90, 210, 300]);
  assert.deepEqual(subtractIntervals([0, 100], [0, 100]), []);
  assert.equal(overlapSeconds([0, 100, 200, 300], 50, 250), 100);
});

test('time that was scheduled but neither recorded nor on the charger is what is reported missing', () => {
  const d = details();
  const day1End = utc('2026-09-02T03:00:00Z');
  // Recorded all day except a 2-hour gap at midday and a 5-minute blip; on the charger overnight.
  const recording = [START, START + 5 * 3600, START + 7 * 3600, START + 10 * 3600, START + 10 * 3600 + 300, day1End];
  const charging = [day1End + 600, utc('2026-09-02T12:00:00Z')];
  const missed = missedStretches(d, START, day1End, recording, charging, 600);
  assert.deepEqual(missed, [START + 5 * 3600, START + 7 * 3600], 'the 5-minute blip is under the threshold');
});

test('the coverage grid lays hours out on the study\'s calendar and says what each one was', () => {
  const d = details();
  const day1End = utc('2026-09-02T03:00:00Z');          // 10 PM Chicago
  const recording = [START, START + 3600, START + 3 * 3600, day1End];
  const charging = [day1End + 60, utc('2026-09-02T12:00:00Z')];
  const rounds: Array<[number, number]> = [];
  for (let minute = START; minute < START + 3600; minute += 60) rounds.push([minute, 120]);     // 7-8 AM: always in range
  const days = coverageGrid({
    timezone: 'America/Chicago', details: d, from: START, to: utc('2026-09-02T14:00:00Z'), knownUntil: utc('2026-09-02T14:00:00Z'),
    recording, charging, roundsByMinute: rounds, roundsPerSecond: 2,
  });
  assert.deepEqual(days.map((day) => day.date), ['2026-09-01', '2026-09-02']);
  const [first, second] = days;
  assert.equal(first!.cells.length, 24);
  assert.equal(first!.cells[6]!.state, 'outside', '6 AM, before the deployment');
  assert.equal(first!.cells[7]!.state, 'ranging');
  assert.equal(first!.cells[7]!.share, 1);
  assert.equal(first!.cells[8]!.state, 'missed', 'scheduled, not recording, not charging');
  assert.equal(first!.cells[11]!.state, 'alone', 'recording with nobody in range is not a fault');
  assert.equal(first!.cells[23]!.state, 'charging');
  assert.equal(second!.cells[3]!.state, 'charging');
  assert.equal(second!.cells[8]!.state, 'missed', '8 AM on the second day, before the logs end, was not recorded');
  assert.equal(second!.cells[9]!.state, 'after', 'past the end of the logs nothing can be said');
});

test('an hour the clocks skip is kept as an empty column, and an hour they repeat is one longer cell', () => {
  const spring = coverageGrid({
    timezone: 'America/Chicago', details: null, from: utc('2027-03-14T06:00:00Z'), to: utc('2027-03-14T10:00:00Z'),
    knownUntil: Number.POSITIVE_INFINITY, recording: [], charging: [], roundsByMinute: [], roundsPerSecond: 2,
  });
  assert.equal(spring[0]!.cells.length, 24);
  assert.equal(spring[0]!.cells[2]!.end - spring[0]!.cells[2]!.start, 0, 'there is no 2 AM that day');
  const fall = coverageGrid({
    timezone: 'America/Chicago', details: null, from: utc('2026-11-01T05:00:00Z'), to: utc('2026-11-01T10:00:00Z'),
    knownUntil: Number.POSITIVE_INFINITY, recording: [], charging: [], roundsByMinute: [], roundsPerSecond: 2,
  });
  assert.equal(fall[0]!.cells[1]!.end - fall[0]!.cells[1]!.start, 7200, '1 AM happens twice');
});
