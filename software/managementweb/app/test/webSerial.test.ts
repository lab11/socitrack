// The Web Serial adapter against an emulated tag.
//
// The fake below answers the same one-byte commands as UsbCdcTask in firmware/src/tasks/usb_task.c,
// and deliberately splits every reply into odd-sized chunks, because a real CDC port delivers bytes
// in whatever pieces the host stack likes and the adapter must not care.

import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';
import { USB_PID, USB_VID, encodeExperimentDetails, formatEui, parseExperimentDetails } from '@tottag/schema';
import { webSerialTransport } from '../src/adapters/webSerial.ts';

const FIXTURES = join(dirname(fileURLToPath(import.meta.url)), '../../packages/tottag-schema/test/fixtures');
const fixture = new Uint8Array(readFileSync(join(FIXTURES, 'boot.ttg')));

/** Emulates UsbCdcTask: one-byte commands, some with payloads, replies in odd-sized chunks. */
function fakeTag(uid: number[], opts: { legacy?: boolean; stream?: Uint8Array } = {}) {
  let controller!: ReadableStreamDefaultController<Uint8Array>;
  let inbox: number[] = [];
  let stored = new Uint8Array(239);
  const writes: number[][] = [];
  let isOpen = false;
  const send = (bytes: Uint8Array) => {
    for (let i = 0; i < bytes.length; i += 37) controller.enqueue(bytes.slice(i, i + 37));
  };
  const process = () => {
    for (;;) {
      const c = inbox[0];
      if (c === undefined) return;
      const need = ({ 0x14: 5, 0x01: 240, 0x04: 9 } as Record<number, number>)[c] ?? (c === 0x06 ? 2 + (inbox[1] ?? 0) * 4 : 1);
      if (inbox.length < need) return;
      const cmd = inbox.splice(0, need);
      writes.push(cmd);
      if (c === 0x15 && !opts.legacy) send(Uint8Array.from([...uid, 10]));
      if (c === 0x20) send(new TextEncoder().encode('Sep 30 2026\nP\n'));
      if (c === 0x10) send(Uint8Array.of(0x10, 0x0e));
      if (c === 0x11) send(Uint8Array.of(1, 0, 0, 0));
      if (c === 0x01) stored = Uint8Array.from(cmd.slice(1));
      if (c === 0x13) send(Uint8Array.from([239, 0, ...stored]));
      if (c === 0x03) send(Uint8Array.from([...uid, 10, 239, 0, ...(opts.stream ?? fixture)]));
    }
  };
  const port = {
    readable: null as ReadableStream<Uint8Array> | null,
    writable: null as WritableStream<Uint8Array> | null,
    getInfo: () => ({ usbVendorId: USB_VID, usbProductId: USB_PID }),
    async open() {
      if (isOpen) throw new DOMException('already open', 'InvalidStateError');
      isOpen = true;
      port.readable = new ReadableStream({ start(c) { controller = c; } });
      port.writable = new WritableStream({ write(chunk) { inbox.push(...chunk); setTimeout(process, 1); } });
    },
    async close() { isOpen = false; inbox = []; },
  };
  return { port, writes, get stored() { return stored; }, get isOpen() { return isOpen; } };
}

function install(ports: ReturnType<typeof fakeTag>[], pick = 0) {
  Object.defineProperty(globalThis.navigator, 'serial', {
    configurable: true,
    value: { requestPort: async () => ports[pick]!.port, getPorts: async () => ports.map((p) => p.port) },
  });
}

const UID = [0x11, 0x22, 0x33, 0x44, 0x55, 0x66];
const config = {
  startTime: 1_900_000_000, endTime: 1_900_600_000, timezone: 'UTC', useDailyTimes: false, dailyStartTime: 0, dailyEndTime: 0,
  devices: [{ eui: Uint8Array.from(UID), label: 'a' }, { eui: Uint8Array.of(1, 2, 3, 4, 5, 6), label: 'b' }],
};

test('identifies, writes a deployment and reads it back', async () => {
  const tag = fakeTag(UID);
  install([tag]);
  const connection = await webSerialTransport.requestDevice();
  assert.ok(connection);
  assert.equal(connection.identity.transport, 'serial');
  assert.equal(formatEui(connection.identity.eui), '66:55:44:33:22:11');
  assert.equal(connection.identity.handle, 'usb:66:55:44:33:22:11');
  assert.equal(connection.identity.firmware, 'Sep 30 2026');
  assert.equal(connection.identity.hardware, 'P');
  const snapshot = await connection.readSnapshot();
  assert.equal(snapshot.batteryMv, 0x0e10);
  assert.equal(snapshot.timestamp, 1);
  await connection.writeDeployment(config);
  const readback = await connection.readExperiment();
  assert.deepEqual(tag.stored, encodeExperimentDetails(config));
  assert.deepEqual(readback, parseExperimentDetails(encodeExperimentDetails(config)));
  assert.equal(tag.writes.at(-3)![0], 0x14);   // clock set before the deployment
  await connection.disconnect();
  assert.equal(tag.isOpen, false);
});

test('downloads the stream verbatim and requests retransmission in one request', async () => {
  const tag = fakeTag(UID);
  install([tag]);
  const connection = (await webSerialTransport.requestDevice())!;
  const phases: string[] = [];
  const bytes = await connection.downloadLog(null, (p) => phases.push(p.phase));
  assert.deepEqual(bytes, fixture);
  assert.equal(phases.at(-1), 'done');
  await connection.retransmitPages(Array.from({ length: 300 }, (_, i) => i), () => {});
  const request = tag.writes.find((w) => w[0] === 0x06)!;
  assert.equal(request[1], 255);
  await connection.disconnect();
});

test('a date-limited download declared a page short still arrives whole, and leaves nothing for the next reply', async () => {
  // Firmware before nandlog 8a88cad leaves the last page out of a date-limited download's declared total
  const short = fixture.slice();
  const view = new DataView(short.buffer);
  let at = 16 + view.getUint16(6, true);
  let last = 0;
  for (let page = 0; page < view.getUint32(8, true); page += 1) {
    last = view.getUint16(at + 12, true);
    at += 20 + last;
  }
  view.setUint32(12, view.getUint32(12, true) - last, true);
  const tag = fakeTag(UID, { stream: short });
  install([tag]);
  const connection = (await webSerialTransport.requestDevice())!;
  const bytes = await connection.downloadLog({ start: 1, end: 2 }, () => {});
  assert.deepEqual(bytes, short);
  const snapshot = await connection.readSnapshot();
  assert.equal(snapshot.batteryMv, 0x0e10);
  await connection.disconnect();
});

test('reconnects by EUI across several granted ports, and skips ones it cannot identify', async () => {
  const legacy = fakeTag([9, 9, 9, 9, 9, 9], { legacy: true });
  const other = fakeTag([1, 2, 3, 4, 5, 6]);
  const wanted = fakeTag(UID);
  install([legacy, other, wanted]);
  const connection = await webSerialTransport.connect('usb:66:55:44:33:22:11');
  assert.ok(connection);
  assert.equal(formatEui(connection.identity.eui), '66:55:44:33:22:11');
  assert.equal(legacy.isOpen, false);
  assert.equal(other.isOpen, false);
  await connection.disconnect();
  assert.equal(await webSerialTransport.connect('usb:AA:AA:AA:AA:AA:AA'), null);
});

test('a tag on firmware without the UID command is refused, not guessed at', async () => {
  const legacy = fakeTag(UID, { legacy: true });
  install([legacy]);
  await assert.rejects(webSerialTransport.requestDevice(), /did not report its ID/);
  assert.equal(legacy.isOpen, false);
});
