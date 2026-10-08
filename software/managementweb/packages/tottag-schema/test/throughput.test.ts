import { test } from 'node:test';
import assert from 'node:assert/strict';

import {
  ThroughputMeter, THROUGHPUT_WARMUP_MS, THROUGHPUT_WINDOW_MS, describeRate, describeRemaining,
} from '../src/throughput.ts';

/** Drive a transfer at a given rate, optionally changing rate part-way. */
function run(meter: ThroughputMeter, opts: { rate: number; forMs: number; fromMs: number; fromBytes: number }) {
  let bytes = opts.fromBytes;
  for (let t = opts.fromMs; t <= opts.fromMs + opts.forMs; t += 100) {
    meter.record(t, bytes);
    bytes += opts.rate / 10;
  }
  return bytes;
}

test('nothing is quoted before there is evidence for it', () => {
  // A number produced from the first moments of a transfer — which include MTU exchange and the
  // connection-parameter update — is wrong in the frightening direction and then visibly halves.
  const meter = new ThroughputMeter();
  run(meter, { rate: 60_000, forMs: THROUGHPUT_WARMUP_MS - 500, fromMs: 0, fromBytes: 0 });
  const early = meter.estimate(1_000_000);
  assert.equal(early.measuring, true);
  assert.equal(early.bytesPerSecond, null);
  assert.equal(early.secondsRemaining, null);
  // Progress is still reported: the bar can move before the estimate exists.
  assert.ok(early.fraction !== null && early.fraction > 0);
});

test('a steady link is measured to within a few per cent', () => {
  const meter = new ThroughputMeter();
  run(meter, { rate: 60_000, forMs: 6_000, fromMs: 0, fromBytes: 0 });
  const estimate = meter.estimate(1_000_000);
  assert.equal(estimate.measuring, false);
  assert.ok(estimate.bytesPerSecond !== null);
  assert.ok(Math.abs(estimate.bytesPerSecond - 60_000) / 60_000 < 0.05,
    `measured ${estimate.bytesPerSecond}, expected ~60000`);
});

test('a slower host is measured as slower, not assumed to match a faster one', () => {
  // The whole reason for measuring: the constant this replaced was one reading on one link.
  const fast = new ThroughputMeter();
  run(fast, { rate: 63_000, forMs: 6_000, fromMs: 0, fromBytes: 0 });
  const slow = new ThroughputMeter();
  run(slow, { rate: 9_000, forMs: 6_000, fromMs: 0, fromBytes: 0 });

  const fastEstimate = fast.estimate(1_000_000);
  const slowEstimate = slow.estimate(1_000_000);
  assert.ok(fastEstimate.bytesPerSecond! > 6 * slowEstimate.bytesPerSecond!);
  assert.ok(slowEstimate.secondsRemaining! > 5 * fastEstimate.secondsRemaining!);
});

test('the estimate follows a link that degrades', () => {
  // A running total cannot fall, so it would keep promising the original rate right up until it was
  // wrong. The window has to track the recent past instead.
  const meter = new ThroughputMeter();
  const after = run(meter, { rate: 60_000, forMs: 5_000, fromMs: 0, fromBytes: 0 });
  const beforeDegrading = meter.estimate(2_000_000).bytesPerSecond!;
  run(meter, { rate: 6_000, forMs: THROUGHPUT_WINDOW_MS + 1_000, fromMs: 5_100, fromBytes: after });
  const afterDegrading = meter.estimate(2_000_000).bytesPerSecond!;

  assert.ok(beforeDegrading > 50_000, `was ${beforeDegrading}`);
  assert.ok(afterDegrading < 10_000, `should have fallen, got ${afterDegrading}`);
});

test('a transfer of unknown size still reports a rate, just no ETA', () => {
  const meter = new ThroughputMeter();
  run(meter, { rate: 40_000, forMs: 5_000, fromMs: 0, fromBytes: 0 });
  const estimate = meter.estimate(null);
  assert.ok(estimate.bytesPerSecond !== null);
  assert.equal(estimate.secondsRemaining, null);
  assert.equal(estimate.fraction, null);
});

test('a stalled transfer does not report a rate it no longer has', () => {
  const meter = new ThroughputMeter();
  const after = run(meter, { rate: 60_000, forMs: 5_000, fromMs: 0, fromBytes: 0 });
  // Same byte count arriving repeatedly: the link is alive but nothing is moving.
  for (let t = 5_100; t < 5_100 + THROUGHPUT_WINDOW_MS + 500; t += 100) meter.record(t, after);
  const estimate = meter.estimate(1_000_000);
  assert.equal(estimate.bytesPerSecond, null, 'no movement means no rate');
  assert.equal(estimate.measuring, true);
});

test('memory does not grow with the length of the transfer', () => {
  // A two-week deployment is minutes of samples at ten per second; keeping them all would be a leak
  // in the one place a leak is least welcome.
  const meter = new ThroughputMeter();
  run(meter, { rate: 60_000, forMs: 120_000, fromMs: 0, fromBytes: 0 });
  const retained = (meter as unknown as { samples: unknown[] }).samples.length;
  assert.ok(retained < 100, `kept ${retained} samples for a 2-minute transfer`);
});

test('the wording is plain, and says so when it does not know', () => {
  assert.equal(describeRemaining(null), 'working out how long this will take…');
  assert.equal(describeRemaining(2), 'less than 5 seconds left');
  assert.equal(describeRemaining(22), 'about 20 seconds left');
  assert.equal(describeRemaining(60), 'about 60 seconds left');
  assert.equal(describeRemaining(119), 'about 2 minutes left');
  assert.equal(describeRemaining(61 * 60), 'about 61 minutes left');
  assert.equal(describeRate(null), 'measuring…');
  assert.equal(describeRate(63_488), '62 kB/s');
  assert.equal(describeRate(500), '500 B/s');
});
