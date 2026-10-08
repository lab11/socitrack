// The separation between "what wrote this log" and "what will run this deployment".

import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import { dirname, join } from 'node:path';

import { parseV2 } from '../src/log.ts';
import { analyseDeployment } from '../src/health.ts';
import { CURRENT_TARGET, LEGACY_TARGET, targetFirmware } from '../src/targetFirmware.ts';

const FIXTURES = join(dirname(fileURLToPath(import.meta.url)), 'fixtures');

test('writing defaults to current firmware', () => {
  // Every tag being configured today is about to run current firmware. Defaulting to the older
  // behaviour would only produce warnings about situations that cannot arise, which is how people
  // learn to ignore warnings.
  assert.deepEqual(targetFirmware(), CURRENT_TARGET);
  assert.equal(targetFirmware().supportsRetransmission, true);
  assert.equal(targetFirmware().writesFramedRecords, true);
});

test('reading an old log does not change what writing assumes', () => {
  // The conflation this module exists to prevent. Both fixtures predate framing and the diagnostics
  // record; having one open must not downgrade what the app asks a tag to do.
  const era = analyseDeployment(parseV2(new Uint8Array(readFileSync(join(FIXTURES, 'boot.ttg'))))).era;
  assert.equal(era.framedRecords, false, 'the fixture really is from older firmware');
  assert.notEqual(era.writesDiagnostics, 'yes');

  // No API accepts an era where a target is wanted, so this cannot be wired up by accident. The
  // assertion is that the default is unmoved by anything that was read.
  assert.equal(targetFirmware().writesFramedRecords, true);
  assert.equal(targetFirmware().debouncesChargerEdges, true);
});

test('the two profiles disagree about everything that matters', () => {
  // If they ever coincided, selecting between them would be pointless and the distinction would
  // quietly stop being maintained.
  const differing = (Object.keys(CURRENT_TARGET) as Array<keyof typeof CURRENT_TARGET>).filter(
    (key) => key !== 'label' && CURRENT_TARGET[key] !== LEGACY_TARGET[key],
  );
  assert.equal(differing.length, 4, `expected all four capabilities to differ, got ${differing.join(', ')}`);
});

test('an older target has to be chosen explicitly', () => {
  // Never inferred. A caller who knows it is talking to an old tag says so; nothing detects it,
  // because the honest failure for an old tag is the WRITE failing and saying why.
  assert.deepEqual(targetFirmware(LEGACY_TARGET), LEGACY_TARGET);
  assert.match(LEGACY_TARGET.label, /older/);
});
