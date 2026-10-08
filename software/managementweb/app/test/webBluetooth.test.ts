// The Bluetooth adapter's log transfer, against a stand-in for a tag's GATT server.
//
// A tag answers a download with one byte to start the chain, the stream header, the deployment details in
// their own notification, the page frames cut at the MTU without regard to where pages fall, and a single
// 0xFF. Firmware before nandlog 8a88cad declares a date-limited download one page short in that header, so
// the transfer has to end where the frames say it does, not where the header's total does.

import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';
import { BLE_MAINTENANCE_DOWNLOAD_LOG, BLE_UUID, GATT_SYSTEM_ID_UUID } from '@tottag/schema';
import { webBluetoothTransport } from '../src/adapters/webBluetooth.ts';

const FIXTURES = join(dirname(fileURLToPath(import.meta.url)), '../../packages/tottag-schema/test/fixtures');
const fixture = new Uint8Array(readFileSync(join(FIXTURES, 'boot.ttg')));

/** The fixture with its header's payload total short by the last page, as older firmware declares a date-limited download. */
function declaredOnePageShort(stream: Uint8Array): Uint8Array {
  const short = stream.slice();
  const view = new DataView(short.buffer);
  let at = 16 + view.getUint16(6, true);
  let last = 0;
  for (let page = 0; page < view.getUint32(8, true); page += 1) {
    last = view.getUint16(at + 12, true);
    at += 20 + last;
  }
  view.setUint32(12, view.getUint32(12, true) - last, true);
  return short;
}

function install(stream: Uint8Array) {
  const listeners = new Set<(event: Event) => void>();
  const data = {
    value: null as DataView | null,
    addEventListener: (_type: string, listener: (event: Event) => void) => { listeners.add(listener); },
    removeEventListener: (_type: string, listener: (event: Event) => void) => { listeners.delete(listener); },
    startNotifications: async () => data,
    stopNotifications: async () => data,
  };
  const notify = (bytes: Uint8Array) => {
    data.value = new DataView(bytes.slice().buffer);
    for (const listener of [...listeners]) listener({ target: data } as unknown as Event);
  };
  const command = {
    writeValueWithResponse: async (value: Uint8Array) => {
      if (value[0] !== BLE_MAINTENANCE_DOWNLOAD_LOG) return;
      setTimeout(() => {
        const details = 16 + new DataView(stream.buffer, stream.byteOffset).getUint16(6, true);
        notify(Uint8Array.of(0));
        notify(stream.subarray(0, 16));
        notify(stream.subarray(16, details));
        for (let at = details; at < stream.length; at += 244) notify(stream.subarray(at, Math.min(stream.length, at + 244)));
        notify(Uint8Array.of(0xff));
      }, 1);
    },
  };
  const characteristics = new Map<string, unknown>([
    [BLE_UUID.BLE_MAINTENANCE_COMMAND_CHAR.uuid, command],
    [BLE_UUID.BLE_MAINTENANCE_DATA_CHAR.uuid, data],
  ]);
  const service = {
    getCharacteristic: async (uuid: string) => {
      const found = characteristics.get(uuid);
      if (!found) throw new Error('not exposed');
      return found;
    },
  };
  const info = {
    getCharacteristic: async (uuid: string | number) => {
      if (uuid !== GATT_SYSTEM_ID_UUID) throw new Error('not exposed');
      return { readValue: async () => new DataView(Uint8Array.of(0xae, 0x11, 0x22, 0xfe, 0xff, 0x33, 0x44, 0x55).buffer) };
    },
  };
  const server = { connected: true, getPrimaryService: async (uuid: string | number) => (uuid === 0x180a ? info : service), disconnect() {} };
  const device = { id: 'fake', gatt: { connect: async () => server }, addEventListener() {}, removeEventListener() {} };
  Object.defineProperty(globalThis.navigator, 'bluetooth', { configurable: true, value: { requestDevice: async () => device } });
}

test('a download arrives verbatim and stops at the end of its last page, before the closing 0xFF', async () => {
  install(fixture);
  const connection = (await webBluetoothTransport.requestDevice())!;
  const phases: string[] = [];
  const bytes = await connection.downloadLog(null, (progress) => phases.push(progress.phase));
  assert.deepEqual(bytes, fixture);
  assert.equal(phases.at(-1), 'pages');
  await connection.disconnect();
});

test('a date-limited download declared a page short still arrives whole', async () => {
  const short = declaredOnePageShort(fixture);
  install(short);
  const connection = (await webBluetoothTransport.requestDevice())!;
  const bytes = await connection.downloadLog({ start: 1, end: 2 }, () => {});
  assert.deepEqual(bytes, short);
  await connection.disconnect();
});
