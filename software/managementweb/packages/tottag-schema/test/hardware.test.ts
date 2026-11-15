// The one-way inference from page size to accelerometer scale.

import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import { dirname, join } from 'node:path';

import { parseV2 } from '../src/log.ts';
import { accelScaleForRevision, inferHardware, partsConsistentWith } from '../src/hardware.ts';
import { BNO055_ACCEL_SCALE, PER_CHIP, SCALE_FACTOR } from '../src/constants.ts';
import { buildTestStream } from './helpers.ts';

const FIXTURES = join(dirname(fileURLToPath(import.meta.url)), 'fixtures');
const read = (name: string) => parseV2(new Uint8Array(readFileSync(join(FIXTURES, name))));

test('real device output identifies its own flash part, and therefore its IMU', () => {
  for (const name of ['boot.ttg', 'watchdog.ttg']) {
    const inference = inferHardware(read(name).report);
    assert.equal(inference.chip, 'AS5F18G04SND', name);
    assert.equal(inference.accelScale, SCALE_FACTOR.SCALE_Q8, name);
    assert.ok(!inference.revisions.includes('M'), 'revM fits the other part');
  }
});

test('the inference works without a completely full page', () => {
  // watchdog.ttg's largest page is well short of the 4064-byte capacity, and is still decisive,
  // because it only has to exceed what the SMALLER part could have held.
  const report = read('watchdog.ttg').report;
  const inference = inferHardware(report);
  assert.ok(inference.maxPayloadBytes < PER_CHIP.AS5F18G04SND.PAYLOAD_BYTES_PER_PAGE, 'no page is full');
  assert.ok(inference.maxPayloadBytes > PER_CHIP.W25N01GWZEIG.PAYLOAD_BYTES_PER_PAGE, 'but it still exceeds the small part');
  assert.equal(inference.chip, 'AS5F18G04SND');
});

test('small pages are reported as undetermined rather than assumed to be revM', () => {
  // The failure this guards: an idle tag on a 4096-byte part flushes on the time-based flush and
  // produces only small pages. Concluding revM there would scale every sample by 2.56.
  const stream = buildTestStream([[1, 0, [0, 0, 0, 0]]]);
  const inference = inferHardware(parseV2(stream).report);
  assert.equal(inference.chip, null);
  assert.equal(inference.accelScale, null, 'an undetermined scale must be null, never a default');
  assert.match(inference.reason, /Nothing excludes any of them/);
  assert.match(inference.reason, /W25N01GWZEIG, AS5F18G04SND/, 'it should name what is still possible');
  assert.match(inference.reason, /2\.56/, 'the reason should say what guessing would cost');
});

test('a known revision maps to the right scale', () => {
  assert.equal(accelScaleForRevision('M'), BNO055_ACCEL_SCALE);
  assert.equal(accelScaleForRevision('M'), 0.01);
  for (const revision of ['N', 'O', 'P'] as const) {
    assert.equal(accelScaleForRevision(revision), SCALE_FACTOR.SCALE_Q8);
    assert.equal(accelScaleForRevision(revision), 1 / 256);
  }
  // The whole reason this is not a default: the two differ by 2.56x.
  assert.ok(Math.abs(BNO055_ACCEL_SCALE / SCALE_FACTOR.SCALE_Q8 - 2.56) < 1e-9);
});

test('the two parts have distinguishable capacities', () => {
  // The inference is only possible while these differ; if a future part matched the small one's
  // capacity this test fails and the inference needs another basis.
  assert.ok(PER_CHIP.AS5F18G04SND.PAYLOAD_BYTES_PER_PAGE > PER_CHIP.W25N01GWZEIG.PAYLOAD_BYTES_PER_PAGE);
});

test('the inference is an exclusion, not a threshold', () => {
  // Stated as arithmetic so the conclusion follows from the extracted geometry rather than from a
  // number chosen to sit between the two parts. A payload larger than a part's capacity did not come
  // from that part — the firmware commits a page when the next record would not fit, so the ceiling
  // is hard.
  const small = PER_CHIP.W25N01GWZEIG.PAYLOAD_BYTES_PER_PAGE;
  const large = PER_CHIP.AS5F18G04SND.PAYLOAD_BYTES_PER_PAGE;

  // Below the smaller capacity, nothing is excluded.
  assert.deepEqual(partsConsistentWith(0).sort(), ['AS5F18G04SND', 'W25N01GWZEIG']);
  assert.deepEqual(partsConsistentWith(small).sort(), ['AS5F18G04SND', 'W25N01GWZEIG']);
  // One byte past it, the smaller part is impossible.
  assert.deepEqual(partsConsistentWith(small + 1), ['AS5F18G04SND']);
  assert.deepEqual(partsConsistentWith(large), ['AS5F18G04SND']);
  // And past every capacity, nothing is consistent — which is a corrupt page, not a new part.
  assert.deepEqual(partsConsistentWith(large + 1), []);
});

test('a payload beyond every known capacity identifies nothing', () => {
  // The failure a threshold hides: `payload > 2016` would confidently name the large part for a
  // page of 99999 bytes, which no supported part could have written and which means the page is
  // damaged. Exclusion returns an empty candidate set and the caller reports it as undetermined.
  const impossible = parseV2(buildTestStream([[1, 0, [0, 0, 0, 0]]]));
  const report = {
    ...impossible.report,
    pages: [{ ...impossible.report.pages[0]!, payloadLength: 99_999 }],
  };
  const inference = inferHardware(report);
  assert.equal(inference.chip, null);
  assert.equal(inference.accelScale, null);
});

test('every supported part has a distinct capacity, or the inference is impossible', () => {
  // The precondition the whole method rests on. If a future part matched an existing capacity this
  // fails, and the inference needs another basis rather than quietly becoming a coin toss.
  const capacities = Object.values(PER_CHIP).map((geometry) => geometry.PAYLOAD_BYTES_PER_PAGE);
  assert.equal(new Set(capacities).size, capacities.length, 'two parts share a payload capacity');
});
