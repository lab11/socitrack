// Web Bluetooth. One of the adapters allowed to touch a browser API.
//
// Everything protocol-shaped — what the bytes mean, how a stream is framed, what a valid deployment
// is — lives in @tottag/schema. This file is the wire: GATT handles, notification plumbing, and the
// reassembly of a chunked stream. It makes no decisions about content.
//
// Two constraints from the API shape the design and are worth stating, because neither is obvious
// and both changed what the UI can offer:
//
//   1. There is NO WAY TO ENUMERATE NEARBY DEVICES. `requestDevice()` opens a browser-controlled
//      chooser and returns the one the user clicks. A ten-tag deployment therefore costs ten
//      dialogs on first use — the page cannot scan, list, and connect to them itself the way the
//      Python tool does. `getDevices()` returns previously-granted devices with no dialog, which is
//      what makes the second and later runs cheap, so pairing is worth doing once and reusing.
//
//   2. THE MAC ADDRESS IS NEVER EXPOSED. `device.id` is an opaque per-origin string. The tag's real
//      EUI has to come from GATT System ID (0x2A23), which the firmware populates in
//      `bluetooth_init()` as [uid0, uid1, uid2, 0xFE, 0xFF, uid3, uid4, uid5]. The Python tool split
//      the BLE address instead, which is why that approach does not port.

import {
  BLE_MAINTENANCE_DELETE_EXPERIMENT, BLE_MAINTENANCE_DOWNLOAD_LOG, BLE_MAINTENANCE_MAX_SEQS_PER_WRITE,
  BLE_MAINTENANCE_PACKET_COMPLETE, BLE_MAINTENANCE_RETRANSMIT_PAGES, BLE_MAINTENANCE_SET_LOG_DOWNLOAD_DATES,
  NANDLOG_MAX_RETRANSMIT_PAGES,
  BLE_UUID, EUI_LEN, GATT_FIRMWARE_REVISION_UUID, GATT_HARDWARE_REVISION_UUID, GATT_SYSTEM_ID_UUID,
  STREAM_HEADER_LAYOUT, SYSTEM_ID_EUI_OFFSETS, ThroughputMeter, WIRE_PAGE_LAYOUT,
  encodeNewExperimentCommand, encodeRadioTestStartCommand, encodeRadioTestStopCommand, decodeRadioStats, decodeRangeResults,
  parseExperimentDetails,
  type DeploymentConfig, type ExperimentDetails, type RadioStats,
} from '@tottag/schema';
import type {
  DownloadProgress, LiveRadioLink, TagConnection, TagIdentity, TagSnapshot, TagTransport,
} from '../ports/tagTransport.ts';

const DEVICE_INFO_SERVICE = 0x180a;
const LIVE_STATS = BLE_UUID.BLE_LIVE_STATS_SERVICE_ID.uuid;
const MAINTENANCE = BLE_UUID.BLE_MAINTENANCE_SERVICE_ID.uuid;

/** Long enough to cover a retry inside the stack, short enough that a dead tag is not a hang. */
const OPERATION_TIMEOUT_MS = 10_000;
/** No notification for this long mid-transfer means the transfer has died. */
const STREAM_IDLE_TIMEOUT_MS = 20_000;

const bluetooth = (): Bluetooth | undefined => (navigator as Navigator & { bluetooth?: Bluetooth }).bluetooth;

/**
 * Devices the chooser has handed us during this page session, by their opaque id.
 *
 * A `BluetoothDevice` from `requestDevice()` stays usable for as long as the page lives, so
 * reconnecting to one costs nothing. Without this cache every reconnect went through
 * `navigator.bluetooth.getDevices()`, which several Chrome versions gate behind
 * chrome://flags/#enable-web-bluetooth-new-permissions-backend — so a tag the user had JUST picked
 * came back as "could not reach that tag", which is both wrong and impossible to act on.
 */
const sessionDevices = new Map<string, BluetoothDevice>();

function withTimeout<T>(promise: Promise<T>, ms: number, what: string): Promise<T> {
  return new Promise<T>((resolve, reject) => {
    const timer = setTimeout(() => reject(new Error(`${what} timed out after ${ms / 1000}s`)), ms);
    promise.then(
      (value) => { clearTimeout(timer); resolve(value); },
      (error) => { clearTimeout(timer); reject(error instanceof Error ? error : new Error(String(error))); },
    );
  });
}

/**
 * Copy into a buffer the Bluetooth API will accept.
 *
 * `Uint8Array` from the schema package is typed over `ArrayBufferLike`, which since TypeScript 5.7
 * is not assignable to `BufferSource`. Copying rather than casting also means the caller keeps
 * ownership of its own bytes, which matters for the encoder whose output goes into the manifest.
 */
const asBufferSource = (bytes: Uint8Array): Uint8Array<ArrayBuffer> => {
  const copy = new Uint8Array(new ArrayBuffer(bytes.length));
  copy.set(bytes);
  return copy;
};

const decodeAscii = (view: DataView): string =>
  new TextDecoder().decode(new Uint8Array(view.buffer, view.byteOffset, view.byteLength)).replace(/\0+$/, '');

class BluetoothTag implements TagConnection {
  readonly identity: TagIdentity;
  readonly liveRadio?: LiveRadioLink;
  private readonly device: BluetoothDevice;
  private readonly server: BluetoothRemoteGATTServer;
  private readonly chars: Map<string, BluetoothRemoteGATTCharacteristic>;

  constructor(
    device: BluetoothDevice,
    server: BluetoothRemoteGATTServer,
    chars: Map<string, BluetoothRemoteGATTCharacteristic>,
    identity: TagIdentity,
  ) {
    this.device = device;
    this.server = server;
    this.chars = chars;
    this.identity = identity;
    // Firmware older than the radio test has no radio statistics characteristic
    if (chars.has(BLE_UUID.BLE_LIVE_STATS_RADIO_CHAR.uuid)) this.liveRadio = this.makeLiveRadio();
  }

  private makeLiveRadio(): LiveRadioLink {
    const command = () => this.char(BLE_UUID.BLE_MAINTENANCE_COMMAND_CHAR.uuid);
    return {
      startRadioTest: async (startTime, endTime, euis) => {
        try {
          await withTimeout(command().writeValueWithResponse(asBufferSource(encodeRadioTestStartCommand(startTime, endTime, euis))),
            OPERATION_TIMEOUT_MS, 'Starting the radio test');
        } catch (error) {
          // The firmware answers a test it cannot run with an ATT error, which the browser reports only generically
          if (error instanceof DOMException) {
            throw new Error('The device refused the test. Its clock may not be set, or its firmware may be too old to run one.');
          }
          throw error;
        }
      },
      stopRadioTest: async () => {
        await withTimeout(command().writeValueWithResponse(asBufferSource(encodeRadioTestStopCommand())),
          OPERATION_TIMEOUT_MS, 'Stopping the radio test');
      },
      readRadioStats: async (): Promise<RadioStats> => {
        const value = await withTimeout(this.char(BLE_UUID.BLE_LIVE_STATS_RADIO_CHAR.uuid).readValue(),
          OPERATION_TIMEOUT_MS, 'Reading the radio counters');
        return decodeRadioStats(new Uint8Array(value.buffer, value.byteOffset, value.byteLength));
      },
      watchRanges: async (onRanges) => {
        const ranges = this.char(BLE_UUID.BLE_LIVE_STATS_RANGING_CHAR.uuid);
        const onValue = (event: Event) => {
          const value = (event.target as BluetoothRemoteGATTCharacteristic).value;
          if (!value) return;
          const decoded = decodeRangeResults(new Uint8Array(value.buffer, value.byteOffset, value.byteLength));
          onRanges(decoded.ranges, decoded.truncated);
        };
        ranges.addEventListener('characteristicvaluechanged', onValue);
        await withTimeout(ranges.startNotifications(), OPERATION_TIMEOUT_MS, 'Subscribing to ranges');
        return async () => {
          ranges.removeEventListener('characteristicvaluechanged', onValue);
          try { await ranges.stopNotifications(); } catch { /* already gone */ }
        };
      },
      onDisconnect: (listener) => {
        const once = () => { this.device.removeEventListener('gattserverdisconnected', once); listener(); };
        this.device.addEventListener('gattserverdisconnected', once);
        return () => this.device.removeEventListener('gattserverdisconnected', once);
      },
    };
  }

  private char(uuid: string): BluetoothRemoteGATTCharacteristic {
    const found = this.chars.get(uuid);
    if (!found) throw new Error(`This TotTag does not expose the characteristic ${uuid}`);
    return found;
  }

  isConnected(): boolean {
    return this.server.connected;
  }

  async readSnapshot(): Promise<TagSnapshot> {
    const batteryMv = await this.readU16(BLE_UUID.BLE_LIVE_STATS_BATTERY_CHAR.uuid);
    const timestamp = await this.readU32(BLE_UUID.BLE_LIVE_STATS_TIMESTAMP_CHAR.uuid);
    let experiment: ExperimentDetails | null = null;
    try {
      experiment = await this.readExperiment();
    } catch {
      // A tag with no deployment yet is normal, not an error worth surfacing here.
    }
    return { batteryMv, timestamp, experiment };
  }

  private async readU16(uuid: string): Promise<number | null> {
    try {
      const value = await withTimeout(this.char(uuid).readValue(), OPERATION_TIMEOUT_MS, 'Reading from the TotTag');
      return value.byteLength >= 2 ? value.getUint16(0, true) : null;
    } catch { return null; }
  }

  private async readU32(uuid: string): Promise<number | null> {
    try {
      const value = await withTimeout(this.char(uuid).readValue(), OPERATION_TIMEOUT_MS, 'Reading from the TotTag');
      return value.byteLength >= 4 ? value.getUint32(0, true) : null;
    } catch { return null; }
  }

  async syncClock(): Promise<void> {
    const now = new Uint8Array(4);
    new DataView(now.buffer).setUint32(0, Math.round(Date.now() / 1000), true);
    await withTimeout(
      this.char(BLE_UUID.BLE_LIVE_STATS_TIMESTAMP_CHAR.uuid).writeValueWithResponse(now),
      OPERATION_TIMEOUT_MS,
      "Setting the TotTag's clock",
    );
  }

  async writeDeployment(config: DeploymentConfig): Promise<void> {
    // Clock first, always. The device stores an absolute start time and decides at boot whether the
    // deployment is active by comparing it against its own RTC; a tag with a wrong clock either
    // sleeps through the study or runs immediately.
    await this.syncClock();
    await withTimeout(
      this.char(BLE_UUID.BLE_MAINTENANCE_COMMAND_CHAR.uuid).writeValueWithResponse(asBufferSource(encodeNewExperimentCommand(config))),
      OPERATION_TIMEOUT_MS,
      'Writing the deployment',
    );
  }

  async readExperiment(): Promise<ExperimentDetails> {
    const value = await withTimeout(
      this.char(BLE_UUID.BLE_MAINTENANCE_EXPERIMENT_CHAR.uuid).readValue(),
      OPERATION_TIMEOUT_MS,
      'Reading the deployment back',
    );
    return parseExperimentDetails(new Uint8Array(value.buffer, value.byteOffset, value.byteLength));
  }

  async cancelDeployment(): Promise<void> {
    await this.syncClock();
    await withTimeout(
      this.char(BLE_UUID.BLE_MAINTENANCE_COMMAND_CHAR.uuid)
        .writeValueWithResponse(Uint8Array.of(BLE_MAINTENANCE_DELETE_EXPERIMENT)),
      OPERATION_TIMEOUT_MS,
      'Cancelling the deployment',
    );
  }

  async locate(seconds: number): Promise<void> {
    const payload = new Uint8Array(4);
    new DataView(payload.buffer).setUint32(0, seconds, true);
    await withTimeout(
      this.char(BLE_UUID.BLE_LIVE_STATS_FINDMYTOTTAG_CHAR.uuid).writeValueWithResponse(payload),
      OPERATION_TIMEOUT_MS,
      'Sounding the buzzer',
    );
  }

  async downloadLog(
    range: { start: number; end: number } | null,
    onProgress: (progress: DownloadProgress) => void,
    signal?: AbortSignal,
  ): Promise<Uint8Array> {
    const command = this.char(BLE_UUID.BLE_MAINTENANCE_COMMAND_CHAR.uuid);
    const dates = new Uint8Array(9);
    const view = new DataView(dates.buffer);
    dates[0] = BLE_MAINTENANCE_SET_LOG_DOWNLOAD_DATES;
    view.setUint32(1, range ? range.start : 0, true);
    view.setUint32(5, range ? range.end : 0, true);
    await command.writeValueWithResponse(dates);
    return this.collectStream(
      () => command.writeValueWithResponse(Uint8Array.of(BLE_MAINTENANCE_DOWNLOAD_LOG)),
      onProgress,
      signal,
    );
  }

  async retransmitPages(seqs: readonly number[], onProgress: (progress: DownloadProgress) => void): Promise<Uint8Array> {
    const command = this.char(BLE_UUID.BLE_MAINTENANCE_COMMAND_CHAR.uuid);
    // The device accumulates at most NANDLOG_MAX_RETRANSMIT_PAGES sequence numbers ACROSS writes and
    // silently discards the rest, so a longer list loses its tail with nothing reported. The caller
    // keeps the remainder for a later round rather than letting it disappear here.
    seqs = seqs.slice(0, NANDLOG_MAX_RETRANSMIT_PAGES);
    // The firmware reads at most BLE_MAINTENANCE_MAX_SEQS_PER_WRITE from one write and SILENTLY
    // IGNORES the rest — `MIN(pValue[1], ...)` with no error returned — so the list has to be
    // chunked at exactly that size or the tail is lost without anything saying so.
    for (let index = 0; index < seqs.length; index += BLE_MAINTENANCE_MAX_SEQS_PER_WRITE) {
      const chunk = seqs.slice(index, index + BLE_MAINTENANCE_MAX_SEQS_PER_WRITE);
      const payload = new Uint8Array(2 + chunk.length * 4);
      const view = new DataView(payload.buffer);
      payload[0] = BLE_MAINTENANCE_RETRANSMIT_PAGES;
      payload[1] = chunk.length;
      chunk.forEach((seq, position) => view.setUint32(2 + position * 4, seq, true));
      await command.writeValueWithResponse(payload);
    }
    return this.collectStream(
      () => command.writeValueWithResponse(Uint8Array.of(BLE_MAINTENANCE_DOWNLOAD_LOG)),
      onProgress,
    );
  }

  /**
   * Subscribe, kick the transfer off, and reassemble the chunks into the stream.
   *
   * The device self-drives: each sent notification raises ATTS_HANDLE_VALUE_CNF on its side and
   * triggers the next chunk, so the host only listens. What the host must do is know the shape:
   *
   *   1. one meaningless byte, sent purely to start the chain
   *   2. the 16-byte stream header — magic, version, details length, page count, payload bytes
   *   3. the experiment details, in their own notification (255 bytes together would exceed the
   *      244-byte ATT payload, so the firmware splits them), OMITTED when details_length is 0,
   *      which is how a retransmission round is signalled
   *   4. page frames, chunked at MTU-3 and not aligned to anything
   *   5. a single 0xFF byte
   *
   * Step 5 is ambiguous on its own — a final data chunk could be one byte of 0xFF — so completion is
   * taken from the frames themselves: every page the header announced, each at the payload length
   * its own frame gives. Ending on the marker alone would truncate a file roughly once in every 256
   * unlucky pages.
   */
  private async collectStream(
    start: () => Promise<void>,
    onProgress: (progress: DownloadProgress) => void,
    signal?: AbortSignal,
  ): Promise<Uint8Array> {
    const data = this.char(BLE_UUID.BLE_MAINTENANCE_DATA_CHAR.uuid);
    let stream = new Uint8Array(4096);
    let received = 0;
    let sawKickoff = false;
    let header: { detailsLength: number; totalPages: number; totalPayloadBytes: number } | null = null;
    let expectedBody: number | null = null;

    // The page frames are followed as they arrive: where the next one starts, how many have been seen,
    // and once the last is placed, how long the whole stream is. The header's payload total only sizes
    // the progress bar, because firmware before nandlog 8a88cad declares a date-limited download one
    // page short, and stopping there would cut that page off.
    const payloadLengthAt = WIRE_PAGE_LAYOUT.fields.find((field) => field.name === 'payload_length')?.offset;
    if (payloadLengthAt === undefined) throw new Error("nandlog_wire_page_t has no field 'payload_length'");
    let nextFrame = 0;
    let framesSeen = 0;
    let streamLength: number | null = null;
    const append = (bytes: Uint8Array) => {
      if (received + bytes.length > stream.length) {
        const bigger = new Uint8Array(Math.max(stream.length * 2, received + bytes.length));
        bigger.set(stream.subarray(0, received));
        stream = bigger;
      }
      stream.set(bytes, received);
      received += bytes.length;
    };

    // Measured per transfer. A constant cannot describe both an old laptop's radio and a new one's,
    // and the difference between them is larger than the margin a constant would claim.
    const meter = new ThroughputMeter();
    const report = (phase: DownloadProgress['phase']) => {
      const total = expectedBody === null ? null : Math.max(expectedBody, received);
      meter.record(performance.now(), received);
      const estimate = meter.estimate(total);
      onProgress({
        phase,
        bytesReceived: received,
        bytesExpected: total,
        pagesExpected: header?.totalPages ?? null,
        bytesPerSecond: estimate.bytesPerSecond,
        secondsRemaining: estimate.secondsRemaining,
      });
    };

    let settle: ((value: Uint8Array) => void) | null = null;
    let fail: ((error: Error) => void) | null = null;
    let idleTimer: ReturnType<typeof setTimeout> | null = null;

    const armIdle = () => {
      if (idleTimer) clearTimeout(idleTimer);
      idleTimer = setTimeout(
        () => fail?.(new Error('The TotTag stopped sending. Move it closer to the computer and try again.')),
        STREAM_IDLE_TIMEOUT_MS,
      );
    };

    const onValue = (event: Event) => {
      const value = (event.target as BluetoothRemoteGATTCharacteristic).value;
      if (!value) return;
      armIdle();
      const bytes = new Uint8Array(value.buffer, value.byteOffset, value.byteLength);

      if (!sawKickoff) { sawKickoff = true; report('header'); return; }
      append(bytes);

      if (!header) {
        if (received < STREAM_HEADER_LAYOUT.size) return;
        const view = new DataView(stream.buffer, 0, received);
        // Offsets from the extracted struct, so a field moving in the firmware moves here.
        const at = (name: string): number => {
          const field = STREAM_HEADER_LAYOUT.fields.find((candidate) => candidate.name === name);
          if (!field) throw new Error(`nandlog_stream_header_t has no field '${name}'`);
          return field.offset;
        };
        header = {
          detailsLength: view.getUint16(at('details_length'), true),
          totalPages: view.getUint32(at('total_pages'), true),
          totalPayloadBytes: view.getUint32(at('total_payload_bytes'), true),
        };
        // Everything after the header: the details blob, then one frame per page plus its payload.
        expectedBody =
          STREAM_HEADER_LAYOUT.size + header.detailsLength +
          header.totalPages * WIRE_PAGE_LAYOUT.size + header.totalPayloadBytes;
        nextFrame = STREAM_HEADER_LAYOUT.size + header.detailsLength;
        if (header.totalPages === 0) streamLength = nextFrame;
      }

      const view = new DataView(stream.buffer, 0, received);
      while (streamLength === null && received >= nextFrame + WIRE_PAGE_LAYOUT.size) {
        nextFrame += WIRE_PAGE_LAYOUT.size + view.getUint16(nextFrame + payloadLengthAt, true);
        framesSeen += 1;
        if (framesSeen === header.totalPages) streamLength = nextFrame;
      }
      report('pages');

      // Hand back exactly the stream, which is byte-for-byte what a .ttg on disk holds; the 0xFF that
      // ends the transfer, if it has already landed, is past its end
      if (streamLength !== null && received >= streamLength) settle?.(stream.slice(0, streamLength));
    };

    data.addEventListener('characteristicvaluechanged', onValue);
    await data.startNotifications();
    onProgress({ phase: 'starting', bytesReceived: 0, bytesExpected: null, pagesExpected: null, bytesPerSecond: null, secondsRemaining: null });

    try {
      return await new Promise<Uint8Array>((resolve, reject) => {
        settle = resolve;
        fail = reject;
        signal?.addEventListener('abort', () => reject(new Error('Download cancelled')), { once: true });
        this.device.addEventListener('gattserverdisconnected', () =>
          reject(new Error('The TotTag disconnected part-way through. Reconnect to resume.')), { once: true });
        armIdle();
        start().catch(reject);
      });
    } finally {
      if (idleTimer) clearTimeout(idleTimer);
      data.removeEventListener('characteristicvaluechanged', onValue);
      try { await data.stopNotifications(); } catch { /* already gone */ }
    }
  }

  async disconnect(): Promise<void> {
    if (this.server.connected) this.server.disconnect();
  }
}

async function openDevice(device: BluetoothDevice): Promise<TagConnection> {
  if (!device.gatt) throw new Error('This device does not support GATT');
  const server = await withTimeout(device.gatt.connect(), OPERATION_TIMEOUT_MS, 'Connecting');
  const chars = new Map<string, BluetoothRemoteGATTCharacteristic>();

  for (const [service, uuids] of [
    [LIVE_STATS, [
      BLE_UUID.BLE_LIVE_STATS_BATTERY_CHAR.uuid,
      BLE_UUID.BLE_LIVE_STATS_TIMESTAMP_CHAR.uuid,
      BLE_UUID.BLE_LIVE_STATS_FINDMYTOTTAG_CHAR.uuid,
      BLE_UUID.BLE_LIVE_STATS_RANGING_CHAR.uuid,
      BLE_UUID.BLE_LIVE_STATS_RADIO_CHAR.uuid,
    ]],
    [MAINTENANCE, [
      BLE_UUID.BLE_MAINTENANCE_EXPERIMENT_CHAR.uuid,
      BLE_UUID.BLE_MAINTENANCE_COMMAND_CHAR.uuid,
      BLE_UUID.BLE_MAINTENANCE_DATA_CHAR.uuid,
    ]],
  ] as const) {
    const handle = await server.getPrimaryService(service);
    for (const uuid of uuids) {
      try { chars.set(uuid, await handle.getCharacteristic(uuid)); } catch { /* optional on some builds */ }
    }
  }

  // Identity comes from the Device Information Service, because Web Bluetooth never exposes a MAC.
  let eui = new Uint8Array(EUI_LEN);
  let firmware: string | null = null;
  let hardware: string | null = null;
  try {
    const info = await server.getPrimaryService(DEVICE_INFO_SERVICE);
    const systemId = await (await info.getCharacteristic(GATT_SYSTEM_ID_UUID)).readValue();
    // Packed by bluetooth_init() as [uid0, uid1, uid2, 0xFE, 0xFF, uid3, uid4, uid5].
    eui = Uint8Array.from(SYSTEM_ID_EUI_OFFSETS, (offset) => systemId.getUint8(offset));
    try { firmware = decodeAscii(await (await info.getCharacteristic(GATT_FIRMWARE_REVISION_UUID)).readValue()); } catch { /* optional */ }
    try { hardware = decodeAscii(await (await info.getCharacteristic(GATT_HARDWARE_REVISION_UUID)).readValue()); } catch { /* optional */ }
  } catch {
    throw new Error('This device did not report a System ID, so it cannot be identified as a TotTag.');
  }

  return new BluetoothTag(device, server, chars, { handle: device.id, eui, firmware, hardware, transport: 'bluetooth' });
}

export const webBluetoothTransport: TagTransport = {
  id: 'bluetooth',
  label: 'TotTag over Bluetooth',

  isAvailable: () => bluetooth() !== undefined,

  unavailableReason() {
    if (bluetooth()) return null;
    return 'This browser cannot talk to TotTags. Chrome, Edge or Opera on a computer can; Safari and Firefox do not implement Web Bluetooth at all.';
  },

  reconnectAcrossReloadsSupported() {
    const api = bluetooth() as (Bluetooth & { getDevices?: unknown }) | undefined;
    return typeof api?.getDevices === 'function';
  },

  async requestDevice(): Promise<TagConnection | null> {
    const api = bluetooth();
    if (!api) throw new Error(webBluetoothTransport.unavailableReason() ?? 'Bluetooth unavailable');
    try {
      const device = await api.requestDevice({
        // PREFIX, not an exact name. Firmware now advertises "TotTag-XX" where XX is the short UID,
        // so that a chooser — which shows the advertised name and nothing else — can tell tags
        // apart. An exact `name` filter would match none of them. Older firmware advertises plain
        // "TotTag", which the same prefix still matches, so both work.
        filters: [{ namePrefix: 'TotTag' }],
        optionalServices: [LIVE_STATS, MAINTENANCE, DEVICE_INFO_SERVICE],
      });
      sessionDevices.set(device.id, device);
      return await openDevice(device);
    } catch (error) {
      if (error instanceof DOMException && error.name === 'NotFoundError') return null;   // dismissed
      throw error;
    }
  },

  async connect(handle: string): Promise<TagConnection | null> {
    // Session cache first: a device the chooser already returned needs no permission lookup at all.
    let device = sessionDevices.get(handle);
    if (!device) {
      const api = bluetooth() as (Bluetooth & { getDevices?: () => Promise<BluetoothDevice[]> }) | undefined;
      if (!api?.getDevices) return null;
      device = (await api.getDevices()).find((candidate) => candidate.id === handle);
      if (device) sessionDevices.set(handle, device);
    }
    if (!device) return null;
    try {
      return await openDevice(device);
    } catch {
      return null;   // out of range, asleep, or the grant has lapsed
    }
  },
};
