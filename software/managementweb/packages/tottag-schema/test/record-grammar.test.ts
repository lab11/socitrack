// Record-grammar parity between the firmware and this package.
//
// The record grammar exists in three places: `stored_record_length()` in the firmware (it walks a
// page in `recover_time_anchor()`), `recordLength()` here, and `_record_length()` in the Python
// tool. The constant drift check cannot see any of them, because a function is not a value.
//
// A length disagreement is not a one-record error. The reader advances by the length it computed,
// so a wrong length desynchronises every record after it in the page. That makes this the highest-
// consequence drift in the project and, until now, the only uncovered one.
//
// These tests compare BEHAVIOUR, not text: the snapshot's grammar is used to predict a length, and
// `recordLength()` is called on a synthetic record to see whether it agrees. Both implementations
// could be rewritten in any style and this would still hold them together.

import { test } from 'node:test';
import assert from 'node:assert/strict';

import { readSnapshot } from '../tools/extract-constants.mjs';
import { recordLength } from '../src/log.ts';
import { DIAGNOSTICS_LAYOUT, RECORD_HEADER_BYTES, STORAGE_TYPE } from '../src/constants.ts';

const grammar = readSnapshot().recordGrammar as Record<
  string,
  { kind: 'fixed'; bytes: number } | { kind: 'counted'; base: number; countOffset: number; scale: number; guardBytes: number }
>;

/** The length the firmware would compute, from the extracted grammar. */
function firmwareLength(name: string, countByte: number): number {
  const rule = grammar[name]!;
  return rule.kind === 'fixed' ? rule.bytes : rule.base + countByte * rule.scale;
}

/** A record of `name` whose count byte, if it has one, is `countByte`. Padded to be in bounds. */
function synthesise(name: string, countByte: number): { data: Uint8Array; offset: number } {
  const rule = grammar[name]!;
  const length = firmwareLength(name, countByte);
  // Offset deliberately non-zero: every rule indexes relative to the record start, and an
  // implementation that confused that with the buffer start would pass at offset 0 only.
  const offset = 7;
  const data = new Uint8Array(offset + Math.max(length, RECORD_HEADER_BYTES + 1) + 4);
  data[offset] = STORAGE_TYPE[name as keyof typeof STORAGE_TYPE];
  if (rule.kind === 'counted') data[offset + rule.countOffset] = countByte;
  return { data, offset };
}

test('every record type the firmware can write has a length rule', () => {
  // The case that motivated this: types 7 and 8 were added to the firmware and this package kept
  // reading with a grammar that stopped at 6, which silently truncated every page containing one.
  const missing = Object.keys(STORAGE_TYPE)
    .filter((name) => name !== 'STORAGE_TYPE_SHUTDOWN' && name !== 'STORAGE_NUM_TYPES')
    .filter((name) => !(name in grammar));
  assert.deepEqual(missing, [], 'storage_data_type_t has members stored_record_length() does not handle');
});

test('the reader computes the same length as the firmware, for every type', () => {
  for (const name of Object.keys(grammar)) {
    const rule = grammar[name]!;
    // For counted records, sweep the count byte: 0, 1, a typical value, and the byte's maximum.
    const counts = rule.kind === 'fixed' ? [0] : [0, 1, 4, 255];
    for (const count of counts) {
      const { data, offset } = synthesise(name, count);
      assert.equal(
        recordLength(data, offset),
        firmwareLength(name, count),
        `${name} with count byte ${count}`,
      );
    }
  }
});

test('the reader refuses a record whose count byte lies past the end of the buffer', () => {
  // The firmware guards this as `(offset + guardBytes) <= length`, returning 0. The reader returns
  // null. Both mean "stop"; what matters is that neither reads past the end and invents a length.
  for (const [name, rule] of Object.entries(grammar)) {
    if (rule.kind !== 'counted') continue;
    const data = new Uint8Array(rule.guardBytes - 1);
    data[0] = STORAGE_TYPE[name as keyof typeof STORAGE_TYPE];
    assert.equal(recordLength(data, 0), null, `${name} should not compute a length from a truncated buffer`);
  }
});

test('the reader reports no length for a type the firmware does not define', () => {
  // The firmware's default arm returns 0. A reader that guessed here would resynchronise onto
  // payload bytes, which is exactly how the v1 parser fabricated records.
  const unknown = new Uint8Array(32);
  unknown[0] = STORAGE_TYPE.STORAGE_NUM_TYPES;   // one past the highest real type
  assert.equal(recordLength(unknown, 0), null);
  unknown[0] = 0xff;
  assert.equal(recordLength(unknown, 0), null);
});

test('the IMU length byte counts itself and the others do not', () => {
  // The single most error-prone rule in the format, and the one the Python tool documents with a
  // comment because it has caught people out. Pinned as an arithmetic relationship rather than a
  // literal, so it survives any change to the header size.
  const imu = grammar.STORAGE_TYPE_IMU!;
  const bleScan = grammar.STORAGE_TYPE_BLE_SCAN!;
  assert.equal(imu.kind, 'counted');
  assert.equal(bleScan.kind, 'counted');
  if (imu.kind !== 'counted' || bleScan.kind !== 'counted') return;

  // BLE_SCAN: header + count byte + count payload bytes.
  assert.equal(bleScan.base, RECORD_HEADER_BYTES + 1);
  // IMU: header + count payload bytes, because the count byte is inside its own count.
  assert.equal(imu.base, RECORD_HEADER_BYTES);
  assert.equal(bleScan.base - imu.base, 1);
});

test('the diagnostics record is sized from the struct, not a literal', () => {
  // Adding a counter to storage_diagnostics_t changes this length. If the two sides disagreed, the
  // reader would step short and desynchronise the rest of the page.
  const rule = grammar.STORAGE_TYPE_DIAGNOSTICS!;
  assert.equal(rule.kind, 'fixed');
  if (rule.kind !== 'fixed') return;
  assert.equal(rule.bytes, RECORD_HEADER_BYTES + DIAGNOSTICS_LAYOUT.size);
});
