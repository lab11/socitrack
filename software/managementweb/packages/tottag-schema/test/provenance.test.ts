// Two-sided provenance check over everything constants.ts exports.
//
// The half that catches a constant going missing is obvious. The half that matters more is the
// other one: nothing may be exported beyond what the ledger documents. Without it, a number typed
// into the host because it was needed sits alongside extracted firmware facts and is indistinguishable
// from one — which is precisely how a host-side guess becomes load-bearing.

import { test } from 'node:test';
import assert from 'node:assert/strict';

import * as constants from '../src/constants.ts';
import { HOST_ONLY_CONSTANTS, SNAPSHOT_BACKED } from '../src/provenance.ts';
import { readSnapshot } from '../tools/extract-constants.mjs';
import { FIRMWARE_ROOT } from '../tools/spec.mjs';
import { readdirSync, readFileSync, statSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import { dirname, join, resolve } from 'node:path';

/** Every firmware source file, concatenated — for checking that a named symbol actually exists. */
const FIRMWARE_TEXT = (() => {
  const root = resolve(dirname(fileURLToPath(import.meta.url)), '..', '..', '..', '..', FIRMWARE_ROOT);
  const parts: string[] = [];
  const walk = (directory: string) => {
    for (const entry of readdirSync(directory)) {
      const path = join(directory, entry);
      if (statSync(path).isDirectory()) walk(path);
      else if (entry.endsWith('.c') || entry.endsWith('.h')) parts.push(readFileSync(path, 'utf8'));
    }
  };
  walk(root);
  return parts.join('\n');
})();

const snapshot = readSnapshot() as Record<string, Record<string, unknown>> & { defines: Record<string, unknown> };
const EXPORTS = Object.keys(constants).filter((name) => name !== 'default');
/** Exported types and the ledger itself are not values to account for. */
const NOT_A_CONSTANT = new Set(['SNAPSHOT_BACKED', 'HOST_ONLY_CONSTANTS']);

const readPath = (path: string): unknown =>
  path.split('.').reduce<unknown>((node, key) => (node as Record<string, unknown>)?.[key], snapshot);

test('every exported constant is accounted for', () => {
  const unaccounted = EXPORTS.filter(
    (name) =>
      !NOT_A_CONSTANT.has(name) &&
      !(name in snapshot.defines) &&
      !(name in SNAPSHOT_BACKED) &&
      !(name in HOST_ONLY_CONSTANTS),
  );
  assert.deepEqual(
    unaccounted,
    [],
    'these exports are neither extracted from firmware nor documented in provenance.ts — add each ' +
      'to HOST_ONLY_CONSTANTS with a reason, or extract it',
  );
});

test('the ledger has no stale entries', () => {
  // The other direction. An entry for something no longer exported means the ledger is describing a
  // codebase that has moved on, and a stale justification reads exactly like a current one.
  const missing = [...Object.keys(HOST_ONLY_CONSTANTS), ...Object.keys(SNAPSHOT_BACKED)].filter(
    (name) => !EXPORTS.includes(name),
  );
  assert.deepEqual(missing, [], 'provenance.ts documents constants that are no longer exported');
});

test('no constant is documented twice', () => {
  const both = Object.keys(HOST_ONLY_CONSTANTS).filter((name) => name in SNAPSHOT_BACKED);
  assert.deepEqual(both, [], 'a constant cannot be both snapshot-backed and host-only');
  const shadowed = Object.keys(HOST_ONLY_CONSTANTS).filter((name) => name in snapshot.defines);
  assert.deepEqual(shadowed, [], 'a host-only constant shares a name with an extracted #define');
});

test('snapshot-backed exports equal the snapshot value at the path they claim', () => {
  for (const [name, path] of Object.entries(SNAPSHOT_BACKED)) {
    if (path.endsWith('(keys)')) continue;   // derived view, checked separately
    const exported = (constants as Record<string, unknown>)[name];
    assert.deepEqual(exported, readPath(path), `${name} does not match snapshot.${path}`);
  }
});

test('every host-only entry states a reason worth reading', () => {
  for (const [name, entry] of Object.entries(HOST_ONLY_CONSTANTS)) {
    // Length is only a proxy, and a bad one where the justification is inherently short:
    // "Bluetooth SIG 0x2A26" and "MAX_DEPLOYMENT_DAYS * 86400" are both complete. For those two
    // categories the substantive check is elsewhere — a named standard, or named inputs below.
    // The categories that remain are the ones where the reason IS the whole argument, and a
    // one-liner there means nobody wrote it down.
    const needsProse = entry.provenance === 'measured' ||
      entry.provenance === 'legacy-format' ||
      entry.provenance === 'mirrors-unextractable-source';
    assert.ok(
      entry.reason.length >= (needsProse ? 40 : 12),
      `${name}: a ${entry.provenance} reason must explain itself, got "${entry.reason}"`,
    );
    // A derivation nobody wrote down is a literal with extra steps. What makes a reason a
    // derivation is that it names its INPUTS — either an extracted constant by name, or plain
    // arithmetic over literals. Checking for an operator alone rejected "NANDLOG_FORMAT_VERSION
    // when framing is off: 1", which says exactly where it comes from.
    if (entry.provenance === 'derived') {
      // A named input counts if it is something that really exists: an extracted constant, another
      // documented host constant, or any symbol in the firmware source. NANDLOG_FORMAT_VERSION is
      // the case that forced the last of those — it is a ternary macro, so it is real but not
      // extractable as a value.
      const namesAnExtractedConstant = [...entry.reason.matchAll(/\b[A-Z][A-Z0-9_]{3,}\b/g)].some(
        (match) =>
          match[0] in snapshot.defines ||
          match[0] in HOST_ONLY_CONSTANTS ||
          FIRMWARE_TEXT.includes(match[0]),
      );
      const isPlainArithmetic = /\d[^.]*[*/+-][^.]*\d|floor\(/i.test(entry.reason);
      assert.ok(
        namesAnExtractedConstant || isPlainArithmetic,
        `${name} is marked derived but its reason names no input: "${entry.reason}"`,
      );
    }
  }
});

test('derived constants really are derived from what they claim', () => {
  // Spot-checks with real arithmetic, so a 'derived' tag cannot hide a hardcoded number that has
  // drifted from its inputs.
  assert.equal(constants.RECORD_HEADER_BYTES, 1 + 4);
  assert.equal(constants.TIMESTAMP_WRAP_SECONDS, constants.NANDLOG_NO_TIMESTAMP / 1000);
  assert.equal(
    constants.MAX_EXPERIMENT_ELAPSED_SECONDS,
    Math.floor((constants.NANDLOG_NO_TIMESTAMP - 1 - 990) / 1000),
  );
  assert.equal(constants.MAX_DEPLOYMENT_DAYS, Math.floor(constants.MAX_EXPERIMENT_ELAPSED_SECONDS / 86400));
  assert.equal(constants.MAX_DEPLOYMENT_SECONDS, constants.MAX_DEPLOYMENT_DAYS * 86400);
  assert.ok(
    Math.abs(constants.WATCHDOG_RESET_WINDOW_S - constants.WATCHDOG_RESET_TICKS * constants.WATCHDOG_MEASURED_TICK_S) < 1e-9,
  );
  assert.equal(constants.RESET_DIAGNOSTIC_MASK, 0xf);
  assert.equal(constants.RESET_STATUS_MASK, (1 << constants.RESET_DIAGNOSTIC_SHIFT) - 1);
});

test('measured constants are not quietly equal to the nominal value they replace', () => {
  // Each of these exists BECAUSE it differs from what the firmware says. If one ever matched, it
  // would mean the measurement had been overwritten with the nominal figure.
  assert.notEqual(constants.TIME_ALIGNED_INTERVAL_S, constants.BATTERY_CHECK_INTERVAL_S);
  assert.notEqual(constants.WATCHDOG_MEASURED_TICK_S, constants.WATCHDOG_TICK_S);
});
