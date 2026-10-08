// Stream construction shared by the reader and analysis tests.
//
// Kept out of the test files themselves so that both can build the same bytes: a test that asserts on a
// stream one of them constructed differently is testing the constructor, not the reader.

import { crc32 } from '../src/crc32.ts';
import { DIAGNOSTICS_LAYOUT, STORAGE_DIAGNOSTIC_STACK_UNMONITORED } from '../src/constants.ts';

/**
 * Build a single-page v2 framed stream from `[type, ms, body]` records.
 *
 * Framed means each record is prefixed with a uint16 giving the length of its BODY, matching `nandlog`'s
 * `[data length][type][timestamp][data]` layout. The page header's time bounds and record count are filled
 * from the records, and the payload CRC is computed, so the result parses as a well-formed page.
 */
export function buildTestStream(records: Array<[number, number, number[]]>, detailsBlock?: Uint8Array): Uint8Array {
  return buildTestPages([records], detailsBlock);
}

/**
 * Build a v2 framed stream of consecutive pages, as buildTestStream() builds one. Each page's header
 * advertises its earliest and latest record, as `nandlog` does, whatever order they were written in.
 */
export function buildTestPages(pages: Array<Array<[number, number, number[]]>>, detailsBlock?: Uint8Array): Uint8Array {
  const payloads = pages.map((records) => {
    const parts: number[] = [];
    for (const [type, ms, body] of records) {
      parts.push(body.length & 0xff, (body.length >>> 8) & 0xff);
      parts.push(type, ms & 0xff, (ms >>> 8) & 0xff, (ms >>> 16) & 0xff, (ms >>> 24) & 0xff, ...body);
    }
    return Uint8Array.from(parts);
  });
  const details = detailsBlock ?? new Uint8Array(239);
  if (!detailsBlock) new DataView(details.buffer).setUint32(0, 1788301320, true);

  const payloadBytes = payloads.reduce((sum, payload) => sum + payload.length, 0);
  const out = new Uint8Array(16 + details.length + 20 * pages.length + payloadBytes);
  const view = new DataView(out.buffer);
  out.set(new TextEncoder().encode('TTS1'), 0);
  view.setUint16(4, 2, true);                     // framed
  view.setUint16(6, details.length, true);
  view.setUint32(8, pages.length, true);
  view.setUint32(12, payloadBytes, true);
  out.set(details, 16);
  let at = 16 + details.length;
  pages.forEach((records, seq) => {
    const payload = payloads[seq]!;
    const times = records.map((record) => record[1]);
    view.setUint32(at, seq, true);
    view.setUint32(at + 4, times.length ? Math.min(...times) : 0, true);
    view.setUint32(at + 8, times.length ? Math.max(...times) : 0, true);
    view.setUint16(at + 12, payload.length, true);
    view.setUint16(at + 14, records.length, true);
    view.setUint32(at + 16, crc32(payload), true);
    out.set(payload, at + 20);
    at += 20 + payload.length;
  });
  return out;
}

/**
 * A full-size diagnostics payload with the named fields set, at the offsets of the firmware's own struct.
 *
 * Fields not named are zero, except the stack headroom entries, which default to "not monitored" so that a
 * synthetic record does not read as every task having run out of stack. Array fields take an array.
 */
export function buildDiagnosticsBody(values: Record<string, number | number[]> = {}): number[] {
  const body = new Uint8Array(DIAGNOSTICS_LAYOUT.size);
  const view = new DataView(body.buffer);
  for (const field of DIAGNOSTICS_LAYOUT.fields) {
    const given = values[field.name] ?? (field.name === 'stack_free_words' ? STORAGE_DIAGNOSTIC_STACK_UNMONITORED : 0);
    const count = field.size / field.elementSize;
    for (let index = 0; index < count; index += 1) {
      const value = Array.isArray(given) ? (given[index] ?? 0) : given;
      const at = field.offset + index * field.elementSize;
      if (field.elementSize === 1) view.setUint8(at, value & 0xff);
      else if (field.elementSize === 2) view.setUint16(at, value, true);
      else view.setUint32(at, value >>> 0, true);
    }
  }
  return [...body];
}

/**
 * An experiment_details_t block selecting the given devices, as the deployment tool writes it: start and end
 * time, no daily hours, then each device's EUI (low byte first, which is the short ID ranges use) and label.
 */
export function buildDetails(devices: ReadonlyArray<{ uid: number; label: string }>, startTime = 1788301320): Uint8Array {
  const details = new Uint8Array(239);
  const view = new DataView(details.buffer);
  view.setUint32(0, startTime, true);
  view.setUint32(4, startTime + 86400, true);
  details[17] = devices.length;
  devices.forEach((device, i) => {
    details.set([device.uid, 0x00, 0x42, 0xe5, 0x98, 0xc0], 18 + i * 6);
    details.set(new TextEncoder().encode(device.label), 18 + 60 + i * 16);
  });
  return details;
}
