// Drift detection between this package and firmware/src.
//
// Three distinct failures are checked, because they fail in three different ways:
//
//   1. The firmware changed a value we depend on.  -> the committed snapshot no longer matches.
//   2. The firmware renamed or removed something.  -> extraction throws, inside the extractor.
//   3. The extractor stopped extracting something. -> extraction throws, inside the extractor.
//
// (2) and (3) are no longer tested here, because they are no longer testable failures: since the
// A3EM review, `extract()` reconciles its own output against spec.mjs before returning and exits
// non-zero if anything is missing. That removes the failure mode structurally rather than watching
// for it — softening an individual lookup, which is how it nearly happened, now produces an
// incomplete snapshot that the extractor itself refuses to emit.
//
// What remains here is the half a generator cannot check: whether the COMMITTED file still agrees
// with the firmware and with the spec. A hand-edited or stale snapshot is invisible from inside
// extract(), because extract() never reads it.

import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import { dirname, resolve } from 'node:path';

import { extract, readSnapshot } from '../tools/extract-constants.mjs';
import {
  REVISIONS, CHIPS, DEFINES, PER_CHIP_DEFINES, ENUMS, STRUCTS, BLE_UUIDS, SCALE_FACTORS,
} from '../tools/spec.mjs';

/** Geometry the extractor derives rather than reads, so the per-chip reconciliation expects it. */
const DERIVED_PER_CHIP = ['PAYLOAD_BYTES_PER_PAGE', 'TOTAL_PAGES', 'LOG_CAPACITY_BYTES'];

const committed = readSnapshot();

test('committed snapshot matches the current firmware', () => {
  const current = extract();
  assert.deepEqual(
    current,
    committed,
    'firmware/src has changed since the snapshot was taken. Review the diff, then run `npm run drift:update`.',
  );
});

test('every spec entry is present in the snapshot', () => {
  // Guards against the extractor narrowing what it looks for while still succeeding.
  const missing: string[] = [];

  for (const entry of DEFINES as ReadonlyArray<{ name: string }>) {
    if (!(entry.name in committed.defines)) missing.push(`defines.${entry.name}`);
  }
  for (const chip of CHIPS) {
    assert.ok(committed.perChip[chip.key], `perChip.${chip.key} absent`);
    for (const entry of [...PER_CHIP_DEFINES.map((e: { name: string }) => e.name), ...DERIVED_PER_CHIP]) {
      if (!(entry in committed.perChip[chip.key])) missing.push(`perChip.${chip.key}.${entry}`);
    }
  }
  for (const entry of ENUMS as ReadonlyArray<{ name: string }>) {
    if (!(entry.name in committed.enums)) missing.push(`enums.${entry.name}`);
  }
  for (const entry of STRUCTS as ReadonlyArray<{ name: string }>) {
    if (!(entry.name in committed.structs)) missing.push(`structs.${entry.name}`);
  }
  for (const entry of BLE_UUIDS as ReadonlyArray<{ name: string }>) {
    if (!(entry.name in committed.uuids)) missing.push(`uuids.${entry.name}`);
  }
  for (const entry of SCALE_FACTORS as ReadonlyArray<{ name: string }>) {
    if (!(entry.name in committed.scaleFactors)) missing.push(`scaleFactors.${entry.name}`);
  }
  assert.deepEqual(missing, [], 'spec.mjs declares entries the extractor did not produce');
});

test('the snapshot contains nothing the spec did not ask for', () => {
  // The mirror of the previous test. Catches a hand-edited snapshot.
  const declared = {
    defines: new Set(DEFINES.map((e: { name: string }) => e.name)),
    perChip: new Set([...PER_CHIP_DEFINES.map((e: { name: string }) => e.name), ...DERIVED_PER_CHIP]),
    enums: new Set(ENUMS.map((e: { name: string }) => e.name)),
    structs: new Set(STRUCTS.map((e: { name: string }) => e.name)),
    uuids: new Set(BLE_UUIDS.map((e: { name: string }) => e.name)),
    scaleFactors: new Set(SCALE_FACTORS.map((e: { name: string }) => e.name)),
  };
  const extra: string[] = [];

  for (const key of Object.keys(committed.defines)) {
    if (!declared.defines.has(key)) extra.push(`defines.${key}`);
  }
  for (const chip of Object.keys(committed.perChip)) {
    for (const key of Object.keys(committed.perChip[chip])) {
      if (!declared.perChip.has(key)) extra.push(`perChip.${chip}.${key}`);
    }
  }
  for (const [group, names] of [
    ['enums', declared.enums], ['structs', declared.structs], ['uuids', declared.uuids],
    ['scaleFactors', declared.scaleFactors],
  ] as const) {
    for (const key of Object.keys(committed[group])) {
      if (!names.has(key)) extra.push(`${group}.${key}`);
    }
  }

  assert.deepEqual(extra, [], 'snapshot holds values spec.mjs does not declare; it may have been hand-edited');
});

test('board revision list and IDs match boards/revisions.h', () => {
  assert.deepEqual(Object.keys(committed.revisionIds), [...REVISIONS]);
  // Ordering matters: the firmware gates behaviour on `REVISION_ID < REVISION_N` and similar.
  const ids = REVISIONS.map((r: string) => committed.revisionIds[r]);
  for (let i = 1; i < ids.length; i += 1) {
    assert.ok(ids[i]! > ids[i - 1]!, `revision IDs are not ascending at ${REVISIONS[i]}`);
  }
});

test('tests run from source, never from compiled output', () => {
  // From the A3EM dashboard, which hit this: deleting four modules and two test files left their
  // compiled output in place, so the suite reported an unchanged 438 passing — on code that no
  // longer existed, with deleted modules still importable. The honest count was 418.
  //
  // This project runs `node --experimental-strip-types --test test/*.test.ts` and builds with
  // `--noEmit`, so a deleted file is gone immediately. Verified by deleting one and watching the
  // count drop. Asserted here so that switching to a compiled test run is a deliberate act that
  // fails this test first, rather than a change that silently inflates every green run afterwards.
  const packages = ['..', '../../../app'];
  for (const location of packages) {
    const manifest = JSON.parse(
      readFileSync(resolve(dirname(fileURLToPath(import.meta.url)), location, 'package.json'), 'utf8'),
    ) as { name: string; scripts?: Record<string, string> };
    const script = manifest.scripts?.test;
    assert.ok(script, `${manifest.name} has no test script`);
    assert.match(script, /test\/.*\.test\.ts/, `${manifest.name} should run tests from .ts source`);
    assert.doesNotMatch(script, /\bdist\b|\bbuild\b|\bout\b/, `${manifest.name} runs tests from compiled output`);
  }
});
