// The v2 reader, against real device output and against deliberately damaged input.
//
// Increment 1 was blocked for one reason: nothing in this package had ever been run against a file a
// tag actually wrote, so every parser claim was derived from the same prose the firmware was derived
// from, and agreement between the two proved nothing. The fixtures here are real — two contiguous
// runs of pages cut out of device 02's 3.9-day deployment log (Storage_Redesign.md §15.8) with their
// stream header rewritten to the reduced page count and nothing else touched.
//
//   boot.ttg      seq 0-7,       the start of the log: reset reason, boot anchor, first records.
//   watchdog.ttg  seq 1416-1425, spanning a watchdog reset that names BLETask as the stalled task.
//
// They are small (26 and 28 kB) because the point is not coverage of volume, it is that the bytes
// were produced by a device rather than by this repository.

import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import { dirname, join } from 'node:path';

import {
  detectFormat, parse, parseV2, parseExperimentDetails, recordLength, decodeResetReason,
  missingSeqs, extractPages, extractPageFrames, mergeRepairs, mergeByTimestamp,
} from '../src/log.ts';
import { crc32 } from '../src/crc32.ts';
import { NANDLOG_TIMESTAMP_TOLERANCE_MS, RESET_DIAGNOSTIC, STORAGE_TYPE, V2_STREAM_MAGIC, WATCHDOG_TASK } from '../src/constants.ts';
import { buildDiagnosticsBody, buildTestPages, buildTestStream } from './helpers.ts';

const FIXTURES = join(dirname(fileURLToPath(import.meta.url)), 'fixtures');
const boot = new Uint8Array(readFileSync(join(FIXTURES, 'boot.ttg')));
const watchdog = new Uint8Array(readFileSync(join(FIXTURES, 'watchdog.ttg')));

// --- CRC ---------------------------------------------------------------------------------------------

test('crc32 matches the published check value', () => {
  // The standard CRC-32 check vector. If this is wrong, every page in every file reads as corrupt.
  assert.equal(crc32(new TextEncoder().encode('123456789')), 0xcbf43926);
  assert.equal(crc32(new Uint8Array(0)), 0);
});

// --- Format detection ---------------------------------------------------------------------------------

test('format detection distinguishes v1 from v2', () => {
  assert.equal(detectFormat(boot), 2);
  assert.equal(detectFormat(Uint8Array.from([STORAGE_TYPE.STORAGE_TYPE_VOLTAGE, 0, 0, 0, 0])), 1);
  assert.equal(detectFormat(new Uint8Array(0)), 1);
});

// --- Real device output ----------------------------------------------------------------------------------

test('the boot fixture parses with no losses of any kind', () => {
  const { report } = parseV2(boot);
  assert.equal(report.formatVersion, 1, 'the fixture predates record framing being turned on');
  assert.equal(report.totalPages, 8);
  assert.equal(report.pagesRead, 8);
  assert.deepEqual(report.holes, []);
  assert.deepEqual(report.crcFailures, []);
  assert.deepEqual(report.shortPages, []);
  assert.deepEqual(report.rejectedRecords, []);
  assert.equal(report.truncated, false);
});

test('every page decodes exactly the record count its header advertised', () => {
  // The device counts records as it writes them and the reader counts them as it reads them. Any
  // disagreement means the two grammars have diverged, which is the failure the count exists to
  // catch and the one a reader cannot otherwise notice.
  for (const fixture of [boot, watchdog]) {
    for (const page of parseV2(fixture).report.pages) {
      assert.equal(page.decodedCount, page.recordCount, `page seq ${page.seq}`);
    }
  }
});

test('experiment details decode out of the stream header', () => {
  const { report, experimentStartTime } = parseV2(boot);
  assert.ok(report.details);
  const details = parseExperimentDetails(report.details!);
  assert.equal(details.experimentStartTime, experimentStartTime);
  assert.equal(details.experimentStartTime, 1787839200);
  assert.equal(details.numDevices, 4);
  assert.equal(details.uids.length, 4);
  assert.ok(details.experimentEndTime > details.experimentStartTime);
});

test('the boot page carries a reset reason and a time anchor', () => {
  // Both are written once per boot, immediately after storage comes up. They are also the two record
  // types this package did not know about while it was stale — and because a v1-framed payload is
  // walked by type, not knowing them means decoding nothing past the first one.
  const { records } = parseV2(boot);
  const reset = records.find((r) => r.kind === 'reset');
  assert.ok(reset && reset.kind === 'reset', 'no reset record in the first pages of a log');
  assert.equal(reset.page, 0);
  assert.deepEqual(reset.causes, ['SW Power-On']);
  assert.equal(reset.isWatchdog, false);

  const anchor = records.find((r) => r.kind === 'anchor');
  assert.ok(anchor && anchor.kind === 'anchor', 'no time anchor in the first pages of a log');
  // The first anchor of a fresh experiment has no network offset yet.
  assert.equal(anchor.offsetMs, 0);
});

test('records come back in write order, never stepping back by more than the tolerance', () => {
  // A record may carry an earlier time than the one before it by up to the tolerance, as a range does,
  // stamped at its round's start but written at its end; a larger step commits the page. A violation
  // would mean the page-commit-on-backward-step rule failed.
  const { records } = parseV2(watchdog);
  assert.ok(records.length > 1000);
  for (let i = 1; i < records.length; i += 1) {
    assert.ok(records[i]!.ms >= records[i - 1]!.ms - NANDLOG_TIMESTAMP_TOLERANCE_MS, `record ${i} steps backwards`);
  }
});

test('a range written after a later record keeps its own time, and the page overlap is not a discontinuity', () => {
  // Page 1 starts with a range stamped at its round's start, 31 ms before the voltage reading that
  // ended page 0. Page 2 starts after the clock base moved back 5.89 s, which is a discontinuity.
  const volts = [0x74, 0x0e, 0x00, 0x00];
  const range = [1, 0x0b, 0xdc, 0x05];
  const stream = buildTestPages([
    [[STORAGE_TYPE.STORAGE_TYPE_RANGES, 99_890, range], [STORAGE_TYPE.STORAGE_TYPE_VOLTAGE, 100_421, volts]],
    [[STORAGE_TYPE.STORAGE_TYPE_RANGES, 100_390, range], [STORAGE_TYPE.STORAGE_TYPE_RANGES, 100_890, range]],
    [[STORAGE_TYPE.STORAGE_TYPE_RANGES, 95_000, range]],
  ]);
  const { records, report } = parseV2(stream);
  assert.deepEqual(records.filter((record) => record.kind === 'ranges').map((record) => record.ms), [99_890, 100_390, 100_890, 95_000]);
  assert.deepEqual(report.timeDiscontinuities, [{ position: 2, seq: 2, previousLast: 100_890, thisFirst: 95_000 }]);
});

test('the watchdog fixture names the stalled task', () => {
  // This is the whole reason the diagnostic exists: the hardware says only THAT the device stopped.
  const { records } = parseV2(watchdog);
  const reset = records.find((r) => r.kind === 'reset' && r.isWatchdog);
  assert.ok(reset && reset.kind === 'reset', 'the watchdog fixture no longer contains a watchdog reset');
  assert.equal(reset.diagnostic, RESET_DIAGNOSTIC.RESET_DIAGNOSTIC_STALL_BLE);
  assert.equal(reset.diagnosticLabel, 'stalled: BLETask');
  assert.deepEqual(reset.causes, ['Watchdog']);
});

test('all eight record types are decodable, and the corpus exercises most of them', () => {
  const kinds = new Set([...parseV2(boot).records, ...parseV2(watchdog).records].map((r) => r.kind));
  for (const expected of ['voltage', 'motion', 'ranges', 'bleScan', 'reset', 'anchor', 'charging']) {
    assert.ok(kinds.has(expected as never), `no ${expected} record in either fixture`);
  }
});

test('ranging records carry plausible peer sets', () => {
  const { records } = parseV2(watchdog);
  const ranges = records.filter((r) => r.kind === 'ranges') as Array<
    Extract<(typeof records)[number], { kind: 'ranges' }>
  >;
  assert.ok(ranges.length > 100);
  for (const record of ranges) {
    assert.ok(record.ranges.size >= 1 && record.ranges.size <= 3, 'unexpected peer count in a 4-device network');
    for (const [uid, mm] of record.ranges) {
      assert.ok(uid > 0 && uid < 256);
      assert.ok(mm >= 0 && mm < 32000);
    }
  }
});

// --- Damage -----------------------------------------------------------------------------------------------

/** Flip one byte inside the payload of the given page position. */
function corruptPayload(source: Uint8Array, position: number): Uint8Array {
  const copy = source.slice();
  const view = new DataView(copy.buffer);
  let offset = 16 + view.getUint16(6, true);
  for (let i = 0; i < position; i += 1) offset += 20 + view.getUint16(offset + 12, true);
  const payloadAt = offset + 20;
  copy[payloadAt] = copy[payloadAt]! ^ 0xff;
  return copy;
}

test('a corrupted payload is reported as a CRC failure, and costs only its own page', () => {
  // The point of the per-page CRC: a bad page is one bad page. Under v1 it desynchronised the file.
  const damaged = corruptPayload(boot, 3);
  const { report } = parseV2(damaged);
  assert.equal(report.crcFailures.length, 1);
  assert.equal(report.crcFailures[0]![0], 3);
  assert.equal(report.pagesRead, 8, 'a CRC failure must not stop the walk');
  // Every other page still decoded fully.
  for (const page of report.pages) {
    if (page.position === 3) continue;
    assert.equal(page.decodedCount, page.recordCount);
  }
});

test('a truncated transfer is reported rather than silently short', () => {
  const { report } = parseV2(boot.slice(0, boot.length - 500));
  assert.equal(report.truncated, true);
  assert.ok(report.pagesRead < report.totalPages!);
});

test('missing sequence numbers name exactly what retransmission should ask for', () => {
  const damaged = corruptPayload(boot, 3);
  assert.deepEqual(missingSeqs(parseV2(damaged).report), [3]);

  // A truncated tail has no frames at all, so the missing sequence numbers are inferred from the
  // contiguity of the epoch rather than observed.
  const cut = parseV2(boot.slice(0, boot.length - 500)).report;
  const inferred = missingSeqs(cut);
  assert.ok(inferred.length > 0);
  assert.equal(inferred[0], cut.lastSeq! + 1);
});

test('a repaired page replaces the copy that did not survive', () => {
  const damaged = corruptPayload(boot, 3);
  const good = parseV2(boot);
  const originalPayloadCount = good.report.pages[3]!.recordCount;

  // Take the intact payload from the undamaged stream, as a retransmission round would.
  const repairs = extractPages(boot);
  assert.ok(repairs.has(3));

  const repaired = parseV2(damaged, undefined, new Map([[3, repairs.get(3)!]]));
  assert.deepEqual(repaired.report.crcFailures, []);
  assert.equal(repaired.report.repaired.length, 1);
  assert.equal(repaired.report.pages[3]!.decodedCount, originalPayloadCount);
  assert.deepEqual(missingSeqs(repaired.report), []);
});

test('the dispatching parse() forwards repairs, so a caller need not know the format', () => {
  // tottag.py's top-level parse() takes repairs, so this one does too. A repair loop written against
  // the dispatching entry point is the shape both tools' download paths actually use, and it silently
  // did nothing here until the parameter existed.
  const damaged = corruptPayload(boot, 3);
  const repairs = new Map([[3, extractPages(boot).get(3)!]]);

  assert.equal(parse(damaged).report.crcFailures.length, 1);
  assert.deepEqual(parse(damaged, undefined, repairs).report.crcFailures, []);
  assert.deepEqual(missingSeqs(parse(damaged, undefined, repairs).report), []);
});

test('a merged stream carries the repair, so the saved file is the repaired one', () => {
  // The point of asking for a page again is to end up holding it. A tool that repairs a download and
  // then writes the unrepaired stream reports holes on re-import that it had already fixed, and the
  // recovery survives only in whatever it happened to print at the time.
  const damaged = corruptPayload(boot, 3);
  const frames = extractPageFrames(boot);
  assert.ok(frames.has(3));

  const merged = mergeRepairs(damaged, new Map([[3, frames.get(3)!]]));
  const reread = parse(merged);
  assert.deepEqual(reread.report.crcFailures, []);
  assert.deepEqual(reread.report.holes, []);
  assert.deepEqual(missingSeqs(reread.report), []);
  // Same pages, same records, as if the transfer had never dropped one
  assert.equal(reread.report.pagesRead, parseV2(boot).report.pagesRead);
  assert.equal(reread.records.length, parseV2(boot).records.length);
  // And the merge did not disturb the pages that arrived intact
  assert.equal(reread.report.pages[2]!.payloadLength, parseV2(boot).report.pages[2]!.payloadLength);
});

test('merging is a no-op when there is nothing to repair', () => {
  // A clean download must stay byte-for-byte what the tag sent, or two tools downloading the same tag
  // stop producing the same file for no reason.
  assert.equal(mergeRepairs(boot, new Map()), boot);
  // A repair offered for a page that arrived intact is not applied either.
  assert.equal(mergeRepairs(boot, new Map([[3, extractPageFrames(boot).get(3)!]])), boot);
});

test('a truncated transfer gets its tail back from repairs', () => {
  // Nothing marks the missing pages in the stream -- they are simply absent -- so they cannot be
  // spliced over anything. They are appended, in sequence order, after the pages that did arrive.
  const full = parseV2(boot);
  const frames = extractPageFrames(boot);
  const lastSeq = full.report.lastSeq!;

  // Cut the stream after the first two pages, the way a transfer that stopped early arrives.
  let cut = 16 + new DataView(boot.buffer, boot.byteOffset, boot.byteLength).getUint16(6, true);
  for (let page = 0; page < 2; page += 1) {
    const view = new DataView(boot.buffer, boot.byteOffset + cut, 20);
    cut += 20 + view.getUint16(12, true);
  }
  const truncated = boot.subarray(0, cut);
  assert.ok(missingSeqs(parse(truncated).report).length > 0, 'the cut stream should look truncated');

  const tail = new Map([...frames].filter(([seq]) => seq > full.report.pages[1]!.seq));
  const merged = mergeRepairs(truncated, tail);
  const reread = parse(merged);
  assert.equal(reread.report.truncated, false);
  assert.equal(reread.report.lastSeq, lastSeq);
  assert.equal(reread.report.pagesRead, full.report.pagesRead);
  assert.deepEqual(missingSeqs(reread.report), []);
});

test('a corrupt page is named by its position, never by what it claims to be', () => {
  // A transfer cut mid-page leaves a partial page whose 20-byte header is read out of whatever bytes
  // happened to arrive. Believing the sequence number in it sent the repair loop chasing pages that
  // never existed -- seen in the field as a request for seq 67371030 from a ten-page log -- while the
  // pages the device really held went unasked for. Position is the only thing a damaged page cannot lie
  // about, and sequence numbers are contiguous, so every page follows from the last one that verified.
  const view = new DataView(boot.buffer, boot.byteOffset, boot.byteLength);
  const full = parseV2(boot);
  assert.ok(full.report.totalPages !== null && full.report.totalPages >= 4, 'fixture needs several pages');

  // Keep two whole pages, then cut partway through the third so its header is nonsense.
  let cut = 16 + view.getUint16(6, true);
  for (let page = 0; page < 2; page += 1) {
    cut += 20 + new DataView(boot.buffer, boot.byteOffset + cut, 20).getUint16(12, true);
  }
  const damaged = boot.slice(0, cut + 40);
  // Scribble over the partial page's header so its sequence number is plainly garbage.
  new DataView(damaged.buffer, damaged.byteOffset, damaged.byteLength).setUint32(cut, 67371030, true);
  new DataView(damaged.buffer, damaged.byteOffset, damaged.byteLength).setUint16(cut + 12, 20, true);

  const wanted = missingSeqs(parse(damaged).report);
  const secondSeq = full.report.pages[1]!.seq;
  assert.ok(!wanted.includes(67371030), 'must not ask for the sequence number a damaged header claims');
  assert.equal(wanted[0], secondSeq + 1, 'the first page wanted follows the last one that verified');
  assert.ok(
    wanted.every((seq) => seq > secondSeq && seq < secondSeq + 1 + full.report.totalPages!),
    'every page named must be plausible for this log',
  );
});

test('a fully repaired stream is the size its own header declares, however damaged the input was', () => {
  // A damaged page header -- garbage sequence number, impossible payload length -- stops the walk, so
  // everything after it has to come back as repairs. The merge rebuilds the stream from verified frames
  // in sequence order, so once every page is held the file walks cleanly end to end, with nothing of the
  // damaged frame left in it to walk off the page grid on.
  const view = new DataView(boot.buffer, boot.byteOffset, boot.byteLength);
  const headerLength = 16 + view.getUint16(6, true);
  const frames = extractPageFrames(boot);

  // Damage a page header the way the field logs show: garbage sequence number, impossible payload length.
  let off = headerLength;
  for (let page = 0; page < 2; page += 1) {
    off += 20 + new DataView(boot.buffer, boot.byteOffset + off, 20).getUint16(12, true);
  }
  const damaged = boot.slice();
  const dv = new DataView(damaged.buffer, damaged.byteOffset, damaged.byteLength);
  dv.setUint32(off, 67371030, true);
  dv.setUint16(off + 12, 19721, true);       // NANDLOG_MAX_PAGE_SIZE_BYTES is 4096

  const merged = mergeRepairs(damaged, frames);
  const mv = new DataView(merged.buffer, merged.byteOffset, merged.byteLength);
  const declared = 16 + mv.getUint16(6, true) + mv.getUint32(8, true) * 20 + mv.getUint32(12, true);
  assert.equal(merged.length, declared, 'the merged stream must match the size its own header declares');

  const reread = parse(merged).report;
  assert.equal(reread.crcFailures.length, 0, 'the damaged page must have been replaced, not stepped over');
  assert.deepEqual(missingSeqs(reread), [], 'nothing should still be missing after a full repair');
});

// The page frames of a stream, as [seq, frame] in stream order, and the bytes ahead of them.
function framesOf(stream: Uint8Array): { head: Uint8Array; frames: Array<readonly [number, Uint8Array]> } {
  const view = new DataView(stream.buffer, stream.byteOffset, stream.byteLength);
  const headerLength = 16 + view.getUint16(6, true);
  const frames: Array<readonly [number, Uint8Array]> = [];
  for (let offset = headerLength; offset + 20 <= stream.length;) {
    const end = offset + 20 + view.getUint16(offset + 12, true);
    frames.push([view.getUint32(offset, true), stream.subarray(offset, end)]);
    offset = end;
  }
  return { head: stream.subarray(0, headerLength), frames };
}

function concat(...parts: Uint8Array[]): Uint8Array {
  const out = new Uint8Array(parts.reduce((total, part) => total + part.length, 0));
  let at = 0;
  for (const part of parts) {
    out.set(part, at);
    at += part.length;
  }
  return out;
}

// A page header followed by a payload whose CRC does not check, as a damaged transfer writes one.
function phantom(seq: number): Uint8Array {
  const frame = new Uint8Array(20 + 64).fill(0x41);
  const view = new DataView(frame.buffer);
  view.setUint32(0, seq, true);
  view.setUint16(12, 64, true);
  view.setUint32(16, crc32(frame.subarray(20)) ^ 1, true);
  return frame;
}

test('a partial repair keeps the page total the device declared', () => {
  // Seen in the field: a USB transfer stopped after 25 of 72 pages, the first repair round brought back
  // only 9 of the 47 asked for, and rewriting the header to count what the merged stream held turned
  // "72 pages, 38 missing" into "34 pages, complete". The repair loop stopped, and the file saved
  // silently lacked the last 38 pages. The device's declared total is the only record of how many
  // there should be, so the merge must never shrink it.
  const { head, frames } = framesOf(boot);
  const bySeq = new Map(frames);
  const first = frames[0]![0];
  const truncated = concat(head, ...frames.slice(0, 2).map(([, frame]) => frame), frames[2]![1].subarray(0, 30));
  assert.deepEqual(missingSeqs(parse(truncated).report), [2, 3, 4, 5, 6, 7].map((n) => first + n));

  const partial = mergeRepairs(truncated, new Map([[first + 2, bySeq.get(first + 2)!], [first + 3, bySeq.get(first + 3)!]]));
  assert.equal(new DataView(partial.buffer, partial.byteOffset).getUint32(8, true), 8);
  const reread = parse(partial).report;
  assert.equal(reread.truncated, true, 'a merge still missing pages must read as incomplete');
  assert.deepEqual(missingSeqs(reread), [4, 5, 6, 7].map((n) => first + n));

  const complete = mergeRepairs(partial, new Map([...bySeq].filter(([seq]) => seq >= first + 4)));
  assert.deepEqual(complete, boot, 'once every page is held the merged stream is the original');
});

test('a phantom page in the stream is neither requested nor kept', () => {
  // Firmware that resumed a partial USB write from the wrong address sent bytes from past the end of a
  // page header, which the host read as extra pages. Naming pages by position then asked for pages it
  // already held, while the real ones pushed past the declared total went unasked for.
  const { head, frames } = framesOf(boot);
  const bySeq = new Map(frames);
  const first = frames[0]![0];
  const bodies = frames.map(([, frame]) => frame);
  const damaged = concat(head, ...bodies.slice(0, 3), phantom(0x41414141), ...bodies.slice(3, 5), phantom(first + 1), ...bodies.slice(5));

  const wanted = missingSeqs(parse(damaged).report);
  assert.deepEqual(wanted, [first + 6, first + 7], 'only the pages pushed past the declared total are missing');
  const merged = mergeRepairs(damaged, new Map(wanted.map((seq) => [seq, bySeq.get(seq)!])));
  assert.deepEqual(merged, boot, 'the phantom frames are gone and the real pages are back in order');
});

test('a page lost ahead of the first that verified is still asked for', () => {
  const { head, frames } = framesOf(boot);
  const first = frames[0]![0];
  const damaged = concat(head, phantom(1234), ...frames.slice(1).map(([, frame]) => frame));
  assert.deepEqual(missingSeqs(parse(damaged).report), [first]);
});

test('a page the device cannot read survives a merge as a hole', () => {
  // The device's own "unreadable" marker is information: a re-read should still say which page it was,
  // rather than the page silently vanishing from a file that otherwise looks repaired.
  const { head, frames } = framesOf(boot);
  const bySeq = new Map(frames);
  const first = frames[0]![0];
  const hole = new Uint8Array(20);
  const holeView = new DataView(hole.buffer);
  holeView.setUint32(0, first + 2, true);
  holeView.setUint32(4, 0xffffffff, true);
  holeView.setUint32(8, 0xffffffff, true);
  const damaged = concat(head, frames[0]![1], frames[1]![1], hole, ...frames.slice(3, 6).map(([, frame]) => frame));
  assert.deepEqual(missingSeqs(parse(damaged).report), [2, 6, 7].map((n) => first + n));

  const merged = mergeRepairs(damaged, new Map([6, 7].map((n) => [first + n, bySeq.get(first + n)!])));
  const reread = parse(merged).report;
  assert.deepEqual(reread.holes, [[2, first + 2]]);
  assert.deepEqual(missingSeqs(reread), [first + 2]);
});

test('extractPages ignores a page whose CRC does not check', () => {
  const damaged = corruptPayload(boot, 3);
  const pages = extractPages(damaged);
  assert.ok(!pages.has(3), 'a repair round must not hand back a page that is still corrupt');
  assert.ok(pages.has(4));
});

test('an unsupported stream version is refused rather than misread', () => {
  const bad = boot.slice();
  new DataView(bad.buffer).setUint16(4, 99, true);
  assert.throws(() => parseV2(bad), /unsupported v2 stream format version 99/);
});

test('a v1 stream needs an experiment start time and says so', () => {
  const v1 = Uint8Array.from([STORAGE_TYPE.STORAGE_TYPE_VOLTAGE, 0, 0, 0, 0, 0x10, 0x0f, 0, 0]);
  assert.throws(() => parse(v1), /experimentStartTime must be supplied/);
  const { records } = parse(v1, 1787839200);
  assert.equal(records.length, 1);
  assert.equal(records[0]!.kind, 'voltage');
});

// --- Record grammar --------------------------------------------------------------------------------------

test('recordLength knows every type the firmware can write', () => {
  // The table a reader of an unframed payload cannot do without. A type missing here stops the walk
  // and loses the rest of the page — which is exactly what a stale reader does at the first anchor.
  const cases: [number, number[], number][] = [
    [STORAGE_TYPE.STORAGE_TYPE_VOLTAGE, [], 9],
    [STORAGE_TYPE.STORAGE_TYPE_CHARGING_EVENT, [], 6],
    [STORAGE_TYPE.STORAGE_TYPE_MOTION, [], 6],
    [STORAGE_TYPE.STORAGE_TYPE_RANGES, [2], 12],
    [STORAGE_TYPE.STORAGE_TYPE_IMU, [7], 12],
    [STORAGE_TYPE.STORAGE_TYPE_BLE_SCAN, [3], 9],
    [STORAGE_TYPE.STORAGE_TYPE_RESET_REASON, [], 7],
    [STORAGE_TYPE.STORAGE_TYPE_TIME_ANCHOR, [], 9],
  ];
  for (const [type, body, expected] of cases) {
    const buffer = new Uint8Array(64);
    buffer[0] = type;
    body.forEach((value, index) => { buffer[5 + index] = value; });
    assert.equal(recordLength(buffer, 0), expected, `type ${type}`);
  }
  const unknown = new Uint8Array(16);
  unknown[0] = STORAGE_TYPE.STORAGE_NUM_TYPES;
  assert.equal(recordLength(unknown, 0), null);
});

test('reset status words decode both halves independently', () => {
  const watchdogWithStall = 0x040 | (RESET_DIAGNOSTIC.RESET_DIAGNOSTIC_STALL_BLE << 12);
  const decoded = decodeResetReason(watchdogWithStall);
  assert.deepEqual(decoded.causes, ['Watchdog']);
  assert.equal(decoded.diagnosticLabel, 'stalled: BLETask');
  assert.equal(decoded.isWatchdog, true);

  // Several hardware causes can latch at once, which is why the firmware stores the raw word.
  assert.deepEqual(decodeResetReason(0x006).causes, ['Power-On', 'Brown-Out']);

  // The boot marker surviving a reset is itself a diagnosis: no handler ran to overwrite it.
  const nothing = decodeResetReason(0x040 | (RESET_DIAGNOSTIC.RESET_DIAGNOSTIC_NOTHING_RECORDED << 12));
  assert.match(nothing.diagnosticLabel!, /nothing recorded a cause/);

  // A status of zero is legitimate: RSTGEN latching nothing is a real state, not a parse failure.
  assert.deepEqual(decodeResetReason(0).causes, ['Unknown (0x0000)']);
});

// --- The legacy shape -----------------------------------------------------------------------------------

test('merging by timestamp loses only same-type collisions', () => {
  // Documented rather than fixed, because downstream .pkl consumers expect the merged shape. The
  // assertion pins the size of the loss so that it cannot grow unnoticed.
  const { records } = parseV2(watchdog);
  const merged = mergeByTimestamp(records);
  const distinct = new Set(records.map((r) => r.ms)).size;
  assert.equal(merged.length, distinct, 'merged rows must equal distinct timestamps');

  const sameTypeCollisions = records.length - new Set(records.map((r) => `${r.ms}:${r.kind}`)).size;
  assert.ok(
    sameTypeCollisions <= records.length * 0.001,
    `same-type collisions are ${sameTypeCollisions} of ${records.length}; the 10 ms timestamp ` +
      'resolution should keep this near zero',
  );
});

test('the stream magic is what detection keys on', () => {
  assert.equal(new TextDecoder('latin1').decode(boot.subarray(0, 4)), V2_STREAM_MAGIC);
});

// --- Framed payloads, and the reason framing was turned on -------------------------------------------------

/**
 * Build a single-page v2 stream in either framing.
 *
 * The framed case delegates to the shared helper; the unframed case exists only so that the tests can show
 * what framing buys, by decoding the same records both ways.
 */
function buildStream(records: Array<[number, number, number[]]>, framed: boolean): Uint8Array {
  if (framed) return buildTestStream(records);
  const parts: number[] = [];
  for (const [type, ms, body] of records) {
    parts.push(type, ms & 0xff, (ms >>> 8) & 0xff, (ms >>> 16) & 0xff, (ms >>> 24) & 0xff, ...body);
  }
  const payload = Uint8Array.from(parts);
  const details = new Uint8Array(239);
  new DataView(details.buffer).setUint32(0, 1788301320, true);
  const out = new Uint8Array(16 + details.length + 20 + payload.length);
  const view = new DataView(out.buffer);
  out.set(new TextEncoder().encode('TTS1'), 0);
  view.setUint16(4, 1, true);
  view.setUint16(6, details.length, true);
  view.setUint32(8, 1, true);
  view.setUint32(12, payload.length, true);
  out.set(details, 16);
  const at = 16 + details.length;
  view.setUint32(at, 0, true);
  view.setUint32(at + 4, records.length ? records[0]![1] : 0, true);
  view.setUint32(at + 8, records.length ? records[records.length - 1]![1] : 0, true);
  view.setUint16(at + 12, payload.length, true);
  view.setUint16(at + 14, records.length, true);
  view.setUint32(at + 16, crc32(payload), true);
  out.set(payload, at + 20);
  return out;
}

/** A diagnostics body with recognisable values in the watchdog, charger and WSF counters. */
function diagnosticsBody(): number[] {
  const late = new Array(WATCHDOG_TASK.WATCHDOG_NUM_TASKS).fill(0);
  late[WATCHDOG_TASK.WATCHDOG_TASK_BLE] = 3;                    // BLETask late 3 times
  late[WATCHDOG_TASK.WATCHDOG_TASK_RANGING] = 1;
  return buildDiagnosticsBody({
    watchdog_declines: 7, watchdog_late_episodes: late,
    charger_suppressed_edges: 4321, wsf_alloc_failures: 2, wsf_largest_failed_length: 280,
    wsf_pool_high_water: [1, 2, 3, 4, 5], wsf_pool_capacity: [8, 8, 8, 8, 8], master_cycle_failures: 2,
  });
}

test('the diagnostics record decodes every counter', () => {
  const { records } = parseV2(buildStream([[STORAGE_TYPE.STORAGE_TYPE_DIAGNOSTICS, 1000, diagnosticsBody()]], true));
  assert.equal(records.length, 1);
  const diag = records[0]!;
  assert.ok(diag.kind === 'diagnostics');
  assert.equal(diag.watchdogDeclines, 7);
  assert.equal(diag.chargerSuppressedEdges, 4321);
  assert.equal(diag.wsfAllocFailures, 2);
  assert.equal(diag.wsfLargestFailedLength, 280);
  // Task names come from watchdog_task_t, not from a transcribed list, so a reordering cannot rename them
  assert.deepEqual([...diag.watchdogLate].sort(), [['BLETask', 3], ['RangingTask', 1]].sort());
  assert.deepEqual([...diag.wsfPoolPeak], [1, 2, 3, 4, 5]);
  assert.deepEqual([...diag.wsfPoolSize], [8, 8, 8, 8, 8]);
});

test('the diagnostics record decodes identically framed and unframed', () => {
  // The record grammar is independent of the framing, and the firmware can be built either way.
  const one = parseV2(buildStream([[STORAGE_TYPE.STORAGE_TYPE_DIAGNOSTICS, 1000, diagnosticsBody()]], true));
  const two = parseV2(buildStream([[STORAGE_TYPE.STORAGE_TYPE_DIAGNOSTICS, 1000, diagnosticsBody()]], false));
  assert.equal(one.report.formatVersion, 2);
  assert.equal(two.report.formatVersion, 1);
  assert.deepEqual(one.records, two.records);
});

test('an UNKNOWN record type is stepped over exactly when framed, and stops the walk when not', () => {
  // This is the whole reason framing was turned on. A reader that meets a type it has never heard of must
  // lose that record and nothing else -- and under the unframed format it cannot, because the only thing
  // that could tell it where the record ends is a length rule it does not have.
  const unknown = STORAGE_TYPE.STORAGE_NUM_TYPES + 3;
  const stream = (framed: boolean) => buildStream([
    [STORAGE_TYPE.STORAGE_TYPE_VOLTAGE, 1000, [0x10, 0x0f, 0, 0]],
    [unknown, 1500, [1, 2, 3, 4, 5, 6, 7]],
    [STORAGE_TYPE.STORAGE_TYPE_VOLTAGE, 2000, [0x20, 0x0f, 0, 0]],
  ], framed);

  const framedResult = parseV2(stream(true));
  assert.equal(framedResult.records.length, 2, 'framed: the records either side must both survive');
  assert.deepEqual(framedResult.records.map((r) => r.ms), [1000, 2000]);
  assert.equal(framedResult.report.rejectedRecords[0]?.count, 1, 'the unknown record is reported, not hidden');

  const unframedResult = parseV2(stream(false));
  assert.equal(unframedResult.records.length, 1, 'unframed: everything after the unknown type is lost');
  assert.deepEqual(unframedResult.records.map((r) => r.ms), [1000]);
});

test('a framed payload whose prefixes do not describe it is rejected whole', () => {
  // Damage rather than unfamiliarity: if the lengths do not add up to the payload, nothing in it can be
  // trusted to start on a record boundary, so guessing would invent records.
  const good = buildStream([[STORAGE_TYPE.STORAGE_TYPE_VOLTAGE, 1000, [0x10, 0x0f, 0, 0]]], true);
  const damaged = good.slice();
  // Overstate the first record's length; the CRC is recomputed so this tests framing, not the CRC
  const payloadAt = 16 + 239 + 20;
  damaged[payloadAt] = 200;
  new DataView(damaged.buffer).setUint32(payloadAt - 4, crc32(damaged.subarray(payloadAt)), true);
  assert.equal(parseV2(damaged).records.length, 0);
  assert.equal(parseV2(good).records.length, 1);
});

// --- Diagnostics -------------------------------------------------------------------------------------

test('a diagnostics record decodes every field', () => {
  const body = buildDiagnosticsBody({
    watchdog_declines: 7, master_cycle_failures: 2,
    firmware_revision: 0x0a1b2c3d, status_flags: 0x03, temperature_c: -5,
    radio_rx_ok: 123456, radio_rx_failed: 789, radio_tx_late: 1, radio_rx_arm_late: 2,
    radio_isr_over_budget: 3, radio_isr_warm_max_us: 410, radio_irq_stuck: 4, radio_wake_max_us: 1890,
    radio_wake_failures: 5, storage_records_dropped: 6, stack_free_words: [10, 20, 30, 40, 50, 0xffff],
    ble_resets: 8, nand_bad_blocks: 9,
  });
  const { records } = parseV2(buildTestStream([[STORAGE_TYPE.STORAGE_TYPE_DIAGNOSTICS, 300_000, body]]));
  const diag = records.find((r) => r.kind === 'diagnostics');
  assert.ok(diag && diag.kind === 'diagnostics');
  assert.equal(diag.watchdogDeclines, 7);
  assert.equal(diag.masterCycleFailures, 2);
  const e = diag;
  assert.equal(e.firmwareRevision, '0a1b2c3d');
  assert.equal(e.firmwareModified, false);
  assert.equal(e.tempcoAvailable, true);
  assert.equal(e.tempcoApplied, true);
  assert.equal(e.temperatureC, -5);
  assert.deepEqual([e.radioRxOk, e.radioRxFailed, e.radioTxLate, e.radioRxArmLate], [123456, 789, 1, 2]);
  assert.deepEqual([e.radioIsrOverBudget, e.radioIsrWarmMaxUs, e.radioIrqStuck, e.radioWakeMaxUs, e.radioWakeFailures],
    [3, 410, 4, 1890, 5]);
  assert.equal(e.recordsDropped, 6);
  assert.deepEqual([...e.stackFreeWords], [['TimeAlignedTask', 10], ['StorageTask', 20], ['AppTask', 30],
    ['BLETask', 40], ['RangingTask', 50], ['TimerService', null]]);
  assert.equal(e.bleResets, 8);
  assert.equal(e.nandBadBlocks, 9);
});

test('a radio abort record decodes every field', () => {
  // Written only by a diagnostic build, one per radio receive that missed its slot
  // ranging, slot 37, 9 devices, 300 us late, 466 us, 2 events, 3000 ms, a received frame, started 20000 us in, 180 us
  // after it, having woken the processor from 900 us of sleep 35 us earlier
  const body = [1, 37, 9, 0x2c, 0x01, 0xd2, 0x01, 2, 0xb8, 0x0b, 2, 0x20, 0x4e, 0xb4, 0x00, 0x84, 0x03, 0x23, 0x00];
  const { records, report } = parseV2(buildTestStream([[STORAGE_TYPE.STORAGE_TYPE_RADIO_ABORT, 300_000, body]]));
  assert.deepEqual(report.rejectedRecords, []);
  const abort = records.find((r) => r.kind === 'radioAbort');
  assert.ok(abort && abort.kind === 'radioAbort');
  assert.deepEqual([abort.phase, abort.slot, abort.scheduleSize, abort.lateUs, abort.isrElapsedUs, abort.isrEvents, abort.sinceTemperatureMs],
    ['ranging', 37, 9, 300, 466, 2, 3000]);
  assert.deepEqual([abort.trigger, abort.isrEntryUs, abort.eventToIsrUs, abort.asleepUs, abort.wakeToIsrUs], ['rxFrame', 20000, 180, 900, 35]);
  const unmeasured = parseV2(buildTestStream([[STORAGE_TYPE.STORAGE_TYPE_RADIO_ABORT, 300_000,
    [2, 3, 9, 0xf6, 0xff, 0xff, 0xff, 1, 0xff, 0xff, 3, 0xff, 0xff, 0x00, 0x80, 0xff, 0xff, 0xff, 0xff]]])).records[0]!;
  assert.ok(unmeasured.kind === 'radioAbort');
  assert.deepEqual([unmeasured.phase, unmeasured.lateUs, unmeasured.isrElapsedUs, unmeasured.sinceTemperatureMs], ['status', -10, null, null]);
  assert.deepEqual([unmeasured.trigger, unmeasured.isrEntryUs, unmeasured.eventToIsrUs, unmeasured.asleepUs, unmeasured.wakeToIsrUs],
    ['rxTimeout', null, null, null, null]);
});

test('a radio timing record decodes its counts and its frame-to-interrupt bands', () => {
  // Written once a minute by a diagnostic build: 6000 receives armed in time, 5900 after waking the processor, 12 as it
  // went to sleep, 8 us to spare at worst, 30 with under 25 us, 38-61 us from frame to interrupt, banded, 41 us wake
  const bands = [100, 900, 2000, 1800, 900, 290, 10];
  const body = packLE([[2, 6000], [2, 5900], [2, 12], [2, 8], [2, 30], [2, 38], [2, 61], ...bands.map((n) => [2, n] as const), [2, 41], [1, 1], [1, 2]]);
  const e = parseV2(buildTestStream([[STORAGE_TYPE.STORAGE_TYPE_RADIO_TIMING, 300_000, body]])).records[0]!;
  assert.ok(e.kind === 'radioTiming');
  assert.deepEqual([e.arms, e.afterSleep, e.duringSleepEntry, e.slackMinUs, e.slackUnder25Us, e.eventToIsrMinUs, e.eventToIsrMaxUs, e.wakeToIsrMaxUs],
    [6000, 5900, 12, 8, 30, 38, 61, 41]);
  assert.deepEqual([e.antenna, e.antennaChanges], [1, 2]);
  assert.deepEqual(e.eventToIsrCounts, bands);
  const empty = parseV2(buildTestStream([[STORAGE_TYPE.STORAGE_TYPE_RADIO_TIMING, 300_000,
    packLE([[2, 0], [2, 0], [2, 0], [2, 0x7fff], [2, 0], [2, 0xffff], [2, 0], ...bands.map(() => [2, 0] as const), [2, 0], [1, 0], [1, 0]])]])).records[0]!;
  assert.ok(empty.kind === 'radioTiming');
  assert.deepEqual([empty.slackMinUs, empty.eventToIsrMinUs], [null, null]);
});

/** Little-endian bytes for a record body, from [size in bytes, value] pairs; negative values are two's complement. */
function packLE(fields: ReadonlyArray<readonly [1 | 2 | 4, number]>): number[] {
  const out: number[] = [];
  for (const [size, value] of fields)
    for (let b = 0; b < size; ++b) out.push(Number((BigInt.asUintN(size * 8, BigInt(value)) >> BigInt(8 * b)) & 0xffn));
  return out;
}

test('a schedule catch record decodes every field', () => {
  // Second copy decoded one round late, receiver on 1.1 ms after the expected first copy, two undecodable frames first
  const body = packLE([[1, 1], [1, 1], [4, -1100], [2, 61], [2, 2135], [1, 2], [1, 3], [2, 30], [2, -1234], [2, 1180], [2, 61]]);
  const { records, report } = parseV2(buildTestStream([[STORAGE_TYPE.STORAGE_TYPE_SCHEDULE_CATCH, 300_000, body]]));
  assert.deepEqual(report.rejectedRecords, []);
  const c = records.find((r) => r.kind === 'scheduleCatch');
  assert.ok(c && c.kind === 'scheduleCatch');
  assert.deepEqual([c.firstCopy, c.roundsMissed, c.leadUs, c.timerToTaskUs, c.wakeUs, c.rxErrors, c.otherFrames, c.firstErrorUs, c.carrierOffsetPpm, c.wakeCorrectionUs, c.timerLatencyUs],
    [1, 1, -1100, 61, 2135, 2, 3, 30, -12.34, 1180, 61]);
  const lost = parseV2(buildTestStream([[STORAGE_TYPE.STORAGE_TYPE_SCHEDULE_CATCH, 300_000,
    packLE([[1, 0xff], [1, 6], [4, 0], [2, 0xffff], [2, 0xffff], [1, 0], [1, 0], [2, 0xffff], [2, 0], [2, -40], [2, 0xffff]])]])).records[0]!;
  assert.ok(lost.kind === 'scheduleCatch');
  assert.deepEqual([lost.firstCopy, lost.roundsMissed, lost.leadUs, lost.timerToTaskUs, lost.wakeUs, lost.firstErrorUs, lost.carrierOffsetPpm, lost.wakeCorrectionUs, lost.timerLatencyUs],
    [null, 6, null, null, null, null, null, -40, null]);
});

test('a round start record decodes every field', () => {
  const body = packLE([[2, 92], [2, 2196], [2, 2349], [1, 0x01 | 0x02 | 0x08], [1, 9], [1, 8], [2, 1037]]);
  const r = parseV2(buildTestStream([[STORAGE_TYPE.STORAGE_TYPE_ROUND_START, 300_000, body]])).records[0]!;
  assert.ok(r.kind === 'roundStart');
  assert.deepEqual([r.timerToTaskUs, r.wakeUs, r.timerToTransmitUs, r.secondCopyFailed, r.computed, r.abandoned, r.joinHeard, r.joinRelayed, r.scheduleSize, r.devicesRanged, r.timerLatencyUs],
    [92, 2196, 2349, true, true, false, true, false, 9, 8, 1037]);
});

test('a session end record decodes a collision and its counters', () => {
  // Stopped as master by a join request arriving in the status phase 41.2 ms into the round
  const body = packLE([[1, 3], [1, 11], [1, 9], [1, 3], [1, 0x83], [1, 0x49], [2, 41200], [4, 912345],
    [2, 1700], [2, 0], [2, 0], [2, 2], [2, 0], [1, 1]]);
  const e = parseV2(buildTestStream([[STORAGE_TYPE.STORAGE_TYPE_SESSION_END, 300_000, body]])).records[0]!;
  assert.ok(e.kind === 'sessionEnd');
  assert.deepEqual([e.reason, e.role, e.scheduleSize, e.sessionMs, e.roundsRanged, e.joinRequestsHeard, e.stalls],
    ['collision', 'ROLE_MASTER', 9, 912345, 1700, 2, 1]);
  assert.deepEqual(e.collision, { phase: 'RANGE_STATUS_PHASE', packet: 'SUBSCRIPTION_PACKET', source: 0x49, atUs: 41200 });
  const search = parseV2(buildTestStream([[STORAGE_TYPE.STORAGE_TYPE_SESSION_END, 300_000,
    packLE([[1, 2], [1, 10], [1, 1], [1, 0], [1, 0], [1, 0], [2, 0], [4, 3100], [2, 0], [2, 4], [2, 4], [2, 0], [2, 7], [1, 0]])]])).records[0]!;
  assert.ok(search.kind === 'sessionEnd');
  assert.deepEqual([search.reason, search.role, search.collision, search.schedulesHeard, search.joinRequestsSent, search.listenErrors],
    ['search timeout', 'ROLE_IDLE', null, 4, 4, 7]);
});

test('the build flags in a diagnostics record decode', () => {
  const body = buildDiagnosticsBody({ status_flags: 0x18 });
  const diag = parseV2(buildTestStream([[STORAGE_TYPE.STORAGE_TYPE_DIAGNOSTICS, 300_000, body]])).records[0]!;
  assert.ok(diag.kind === 'diagnostics');
  assert.deepEqual([diag.diagnosticBuild, diag.tempcoDisabled, diag.tempcoAvailable, diag.firmwareModified], [true, true, false, false]);
});
