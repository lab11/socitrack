// The committed fixture that test_radio_check.py replays through the Python dashboard's radio check must be this
// package's current answers. When a rule here changes, this fails until `npm run parity:update` rewrites the
// fixture, and then the Python test fails until radio_check.py is brought into line.

import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import { dirname, join } from 'node:path';

import { buildRadioParityFixture } from './radioParityFixture.ts';

const FIXTURE = join(dirname(fileURLToPath(import.meta.url)), 'fixtures', 'radio-check-parity.json');

test('the Python parity fixture holds this package\'s current radio-check answers', () => {
  const committed = JSON.parse(readFileSync(FIXTURE, 'utf8'));
  const current = JSON.parse(JSON.stringify(buildRadioParityFixture()));
  assert.deepEqual(committed, current, 'radio check answers changed: run `npm run parity:update`, then make test_radio_check.py pass');
});

test('the parity scenarios reach every verdict and every kind of reason, so the Python side is held to all of them', () => {
  const fixture = buildRadioParityFixture();
  const devices = [...fixture.analysis.flatMap((scenario) => scenario.expected.devices), ...fixture.live.expected.analysis.devices];
  for (const verdict of ['pass', 'check', 'fail', 'missing']) {
    assert.ok(devices.some((device) => device.verdict === verdict), `no scenario produces a ${verdict}`);
  }
  const reasons = devices.flatMap((device) => device.reasons);
  for (const pattern of [
    /^Ranged in only/, /^Ranged in \d+% of rounds, where/, /receives failed against/, /^Antenna \d failed .* suspect\.$/,
    /^Antenna \d failed .* on the others\.$/, /^Ranged to a typical peer in only/, /^Ranged to a typical peer in \d+% of rounds, against/,
    /mm long on every link/, /mm short on every link/, /^Its distances spread/, /^The radio needed resetting/, /^Lost \d+ rounds/,
  ]) {
    assert.ok(reasons.some((reason) => pattern.test(reason)), `no scenario produces a reason matching ${pattern}`);
  }
  const notes = fixture.analysis.flatMap((scenario) => scenario.expected.notes);
  for (const pattern of [/no log loaded/, /less than a minute/, /fewer than three devices/, /do not let each device's distance offset/, /^No positions entered/]) {
    assert.ok(notes.some((note) => pattern.test(note)), `no scenario produces a note matching ${pattern}`);
  }
  assert.ok(fixture.live.expected.summaries.some((summary) => summary.truncated > 0), 'the live scenario has no cut-short notification');
});
