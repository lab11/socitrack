// Web Serial: a TotTag plugged in over USB. One of the adapters allowed to touch a browser API, and
// the other one besides webBluetooth.ts that talks to a tag.
//
// A tag that finds a USB host at boot enters USB maintenance mode and serves a CDC command loop
// (`UsbCdcTask` in firmware/src/tasks/usb_task.c) in place of its GATT services. Every command is
// one byte, some followed by a payload, and only some answer. Nothing is framed, so each read here
// asks for exactly as many bytes as that command's reply holds.
//
// Two things differ from Bluetooth and shape this file:
//
//   1. A serial port carries NO IDENTITY. `SerialPort.getInfo()` gives only the USB vendor and
//      product ids, which every TotTag shares, and the USB serial-number string is a fixed model
//      name. The EUI has to be asked for with USB_GET_UID_COMMAND, so identifying a port means
//      opening it. Firmware older than that command stays silent, and such a tag cannot be put in
//      a deployment over USB, since the schedule has to name every tag by its EUI.
//
//   2. `getPorts()` returns every port already granted to this origin with no dialog, so a tag
//      plugged in again after a reload is found by opening each granted port and asking which
//      tag it is. That is why the handle is the EUI itself: a port object does not survive being
//      unplugged, and the EUI is the only thing that stays the same.

import {
  EUI_LEN, NANDLOG_MAX_RETRANSMIT_PAGES, STREAM_HEADER_LAYOUT, USB_COMMAND, USB_PID, USB_VID,
  V2_STREAM_MAGIC, WIRE_PAGE_LAYOUT, ThroughputMeter,
  encodeExperimentDetails, formatEui, parseExperimentDetails,
  type DeploymentConfig, type ExperimentDetails,
} from '@tottag/schema';
import type { DownloadProgress, TagConnection, TagIdentity, TagSnapshot, TagTransport } from '../ports/tagTransport.ts';

// The parts of the Web Serial API used here. The DOM library does not declare them, and they are
// small enough that pulling in a types package for them is not worth it.
interface SerialPortInfo { usbVendorId?: number; usbProductId?: number }
interface SerialPort {
  readonly readable: ReadableStream<Uint8Array> | null;
  readonly writable: WritableStream<Uint8Array> | null;
  open(options: { baudRate: number }): Promise<void>;
  close(): Promise<void>;
  getInfo(): SerialPortInfo;
}
interface Serial {
  requestPort(options?: { filters?: SerialPortInfo[] }): Promise<SerialPort>;
  getPorts(): Promise<SerialPort[]>;
}

const serial = (): Serial | undefined => (navigator as Navigator & { serial?: Serial }).serial;

/** Ignored by a CDC device, but `open()` insists on one. */
const BAUD_RATE = 115_200;
/** Long enough for a flash write on the tag, short enough that a silent tag is not a hang. */
const OPERATION_TIMEOUT_MS = 3_000;
/** How long an unidentified port gets to answer. Older firmware never will. */
const IDENTIFY_TIMEOUT_MS = 1_000;
/** No data for this long mid-transfer means the transfer has died. */
const STREAM_IDLE_TIMEOUT_MS = 20_000;
/** The firmware reads the retransmission count as one byte, and each request replaces the last. */
const MAX_SEQS_PER_REQUEST = Math.min(255, NANDLOG_MAX_RETRANSMIT_PAGES);
/** The download reply opens with the tag's UID and a newline, then the details length. */
const DOWNLOAD_PREAMBLE_BYTES = EUI_LEN + 1 + 2;

const NEWLINE = 0x0a;
const TOTTAG_FILTER: SerialPortInfo = { usbVendorId: USB_VID, usbProductId: USB_PID };

/** Tag handle to the port it was last found on, for this page session. */
const sessionPorts = new Map<string, SerialPort>();

const handleFor = (eui: Uint8Array): string => `usb:${formatEui(eui)}`;

/**
 * An open port, with its incoming bytes buffered so they can be read by count.
 *
 * The reader runs for as long as the port is open. Bytes that arrive when nothing is waiting are
 * kept, because a reply can land before the code that wants it has started reading.
 */
class SerialLink {
  private readonly port: SerialPort;
  private readonly reader: ReadableStreamDefaultReader<Uint8Array>;
  private readonly writer: WritableStreamDefaultWriter<Uint8Array>;
  private chunks: Uint8Array[] = [];
  private buffered = 0;
  private wake: (() => void) | null = null;
  private closedError: Error | null = null;
  private readonly pumping: Promise<void>;

  private constructor(port: SerialPort) {
    this.port = port;
    if (!port.readable || !port.writable) throw new Error('The USB port opened without a data channel');
    this.reader = port.readable.getReader();
    this.writer = port.writable.getWriter();
    this.pumping = this.pump();
  }

  static async open(port: SerialPort): Promise<SerialLink> {
    await port.open({ baudRate: BAUD_RATE });
    return new SerialLink(port);
  }

  get isOpen(): boolean {
    return this.closedError === null;
  }

  private async pump(): Promise<void> {
    try {
      for (;;) {
        const { value, done } = await this.reader.read();
        if (done) break;
        if (value?.length) {
          this.chunks.push(value);
          this.buffered += value.length;
          this.wake?.();
        }
      }
      this.closedError = new Error('The TotTag was unplugged.');
    } catch (error) {
      this.closedError = error instanceof Error ? error : new Error(String(error));
    }
    this.wake?.();
  }

  /** Drop anything already received, so a reply cannot be confused with leftovers from the last one. */
  discardInput(): void {
    this.chunks = [];
    this.buffered = 0;
  }

  async write(bytes: Uint8Array): Promise<void> {
    if (this.closedError) throw this.closedError;
    const copy = new Uint8Array(bytes.length);
    copy.set(bytes);
    await this.writer.write(copy);
  }

  /** Resolve once at least `count` bytes are buffered, or reject after `ms` with nothing new. */
  private async waitFor(count: number, ms: number, what: string): Promise<void> {
    while (this.buffered < count) {
      if (this.closedError) throw this.closedError;
      const before = this.buffered;
      await new Promise<void>((resolve) => {
        const timer = setTimeout(resolve, ms);
        this.wake = () => { clearTimeout(timer); resolve(); };
      });
      this.wake = null;
      if (this.buffered === before && !this.closedError) throw new Error(`${what} timed out after ${ms / 1000}s`);
    }
  }

  private take(count: number): Uint8Array {
    const out = new Uint8Array(count);
    let filled = 0;
    while (filled < count) {
      const chunk = this.chunks[0]!;
      const used = Math.min(chunk.length, count - filled);
      out.set(chunk.subarray(0, used), filled);
      filled += used;
      if (used === chunk.length) this.chunks.shift();
      else this.chunks[0] = chunk.subarray(used);
    }
    this.buffered -= count;
    return out;
  }

  async read(count: number, ms: number, what: string): Promise<Uint8Array> {
    await this.waitFor(count, ms, what);
    return this.take(count);
  }

  /** Whatever has arrived, at least one byte and at most `max`. */
  async readSome(max: number, ms: number, what: string): Promise<Uint8Array> {
    await this.waitFor(1, ms, what);
    return this.take(Math.min(max, this.buffered));
  }

  async readLine(ms: number, what: string): Promise<string> {
    const bytes: number[] = [];
    for (;;) {
      const [byte] = await this.read(1, ms, what);
      if (byte === NEWLINE) return new TextDecoder().decode(Uint8Array.from(bytes)).replace(/\0+$/, '');
      bytes.push(byte!);
    }
  }

  async close(): Promise<void> {
    try { await this.reader.cancel(); } catch { /* already closed */ }
    await this.pumping;
    this.reader.releaseLock();
    this.writer.releaseLock();
    try { await this.port.close(); } catch { /* unplugged */ }
  }
}

const u32 = (value: number): Uint8Array => {
  const out = new Uint8Array(4);
  new DataView(out.buffer).setUint32(0, value, true);
  return out;
};

const join = (...parts: Uint8Array[]): Uint8Array => {
  const out = new Uint8Array(parts.reduce((sum, part) => sum + part.length, 0));
  let offset = 0;
  for (const part of parts) { out.set(part, offset); offset += part.length; }
  return out;
};

/** The tag's EUI, or null when it does not answer USB_GET_UID_COMMAND. */
async function readEui(link: SerialLink): Promise<Uint8Array | null> {
  link.discardInput();
  await link.write(Uint8Array.of(USB_COMMAND.USB_GET_UID_COMMAND));
  try {
    const reply = await link.read(EUI_LEN + 1, IDENTIFY_TIMEOUT_MS, 'Identifying the TotTag');
    return reply[EUI_LEN] === NEWLINE ? reply.slice(0, EUI_LEN) : null;
  } catch {
    return null;
  }
}

class SerialTag implements TagConnection {
  readonly identity: TagIdentity;
  private readonly link: SerialLink;

  constructor(link: SerialLink, identity: TagIdentity) {
    this.link = link;
    this.identity = identity;
  }

  isConnected(): boolean {
    return this.link.isOpen;
  }

  async readSnapshot(): Promise<TagSnapshot> {
    const batteryMv = await this.query(USB_COMMAND.USB_VOLTAGE_COMMAND, 2, 'Reading the battery')
      .then((bytes) => new DataView(bytes.buffer).getUint16(0, true), () => null);
    const timestamp = await this.query(USB_COMMAND.USB_GET_TIMESTAMP_COMMAND, 4, "Reading the TotTag's clock")
      .then((bytes) => new DataView(bytes.buffer).getUint32(0, true), () => null);
    let experiment: ExperimentDetails | null = null;
    try {
      experiment = await this.readExperiment();
    } catch {
      // A tag with no deployment yet is normal, not an error worth surfacing here.
    }
    return { batteryMv, timestamp, experiment };
  }

  private async query(command: number, replyBytes: number, what: string): Promise<Uint8Array> {
    this.link.discardInput();
    await this.link.write(Uint8Array.of(command));
    return this.link.read(replyBytes, OPERATION_TIMEOUT_MS, what);
  }

  async syncClock(): Promise<void> {
    // One write, so the timestamp arrives in the same USB packet as its command byte.
    await this.link.write(join(Uint8Array.of(USB_COMMAND.USB_SET_TIMESTAMP_COMMAND), u32(Math.round(Date.now() / 1000))));
  }

  async writeDeployment(config: DeploymentConfig): Promise<void> {
    // Clock first, always, for the same reason as over Bluetooth: the tag compares the stored start
    // time against its own RTC. Neither command answers, so success is only known from the readback
    // that the caller always does next.
    await this.syncClock();
    await this.link.write(join(Uint8Array.of(USB_COMMAND.USB_NEW_EXPERIMENT_COMMAND), encodeExperimentDetails(config)));
  }

  async readExperiment(): Promise<ExperimentDetails> {
    const length = new DataView((await this.query(USB_COMMAND.USB_GET_EXPERIMENT_COMMAND, 2, 'Reading the deployment back')).buffer)
      .getUint16(0, true);
    return parseExperimentDetails(await this.link.read(length, OPERATION_TIMEOUT_MS, 'Reading the deployment back'));
  }

  async cancelDeployment(): Promise<void> {
    await this.syncClock();
    await this.link.write(Uint8Array.of(USB_COMMAND.USB_DELETE_EXPERIMENT_COMMAND));
  }

  async locate(_seconds: number): Promise<void> {
    // The USB command takes no duration; the firmware sounds the buzzer for a fixed ten seconds.
    await this.link.write(Uint8Array.of(USB_COMMAND.USB_FIND_MY_TOTTAG_COMMAND));
  }

  async downloadLog(
    range: { start: number; end: number } | null,
    onProgress: (progress: DownloadProgress) => void,
    signal?: AbortSignal,
  ): Promise<Uint8Array> {
    this.link.discardInput();
    await this.link.write(join(
      Uint8Array.of(USB_COMMAND.USB_SET_LOG_DL_DATES_COMMAND),
      u32(range ? range.start : 0),
      u32(range ? range.end : 0),
    ));
    return this.collectStream(onProgress, signal);
  }

  async retransmitPages(seqs: readonly number[], onProgress: (progress: DownloadProgress) => void): Promise<Uint8Array> {
    // Each request REPLACES the device's list rather than adding to it, so everything asked for in
    // one round has to go in one request. The caller keeps whatever does not fit for a later round.
    seqs = seqs.slice(0, MAX_SEQS_PER_REQUEST);
    const request = new Uint8Array(2 + seqs.length * 4);
    const view = new DataView(request.buffer);
    request[0] = USB_COMMAND.USB_RETRANSMIT_PAGES_COMMAND;
    request[1] = seqs.length;
    seqs.forEach((seq, position) => view.setUint32(2 + position * 4, seq, true));
    this.link.discardInput();
    await this.link.write(request);
    return this.collectStream(onProgress);
  }

  /**
   * Start a transfer and read the stream it produces.
   *
   * Over USB the reply is the tag's UID and a newline, a u16 details length, and then exactly the
   * stream Bluetooth delivers: the header, the details unless this is a repair round, and the page
   * frames. There is no completion marker, so the end is the byte count the header promised.
   */
  private async collectStream(onProgress: (progress: DownloadProgress) => void, signal?: AbortSignal): Promise<Uint8Array> {
    onProgress({ phase: 'starting', bytesReceived: 0, bytesExpected: null, pagesExpected: null, bytesPerSecond: null, secondsRemaining: null });
    await this.link.write(Uint8Array.of(USB_COMMAND.USB_DOWNLOAD_LOG_COMMAND));
    const idle = 'The TotTag stopped sending. Check the USB cable and try again.';
    await this.link.read(DOWNLOAD_PREAMBLE_BYTES, STREAM_IDLE_TIMEOUT_MS, idle);

    const header = await this.link.read(STREAM_HEADER_LAYOUT.size, STREAM_IDLE_TIMEOUT_MS, idle);
    if (new TextDecoder().decode(header.subarray(0, 4)) !== V2_STREAM_MAGIC) {
      throw new Error("This TotTag's firmware sends a log format that USB downloads here do not read. Update the firmware.");
    }
    const at = (name: string): number => {
      const field = STREAM_HEADER_LAYOUT.fields.find((candidate) => candidate.name === name);
      if (!field) throw new Error(`nandlog_stream_header_t has no field '${name}'`);
      return field.offset;
    };
    const view = new DataView(header.buffer);
    const totalPages = view.getUint32(at('total_pages'), true);
    const detailsLength = view.getUint16(at('details_length'), true);
    // The declared total only sizes the progress bar. Firmware before nandlog 8a88cad declares a
    // date-limited download one page short, so each page is read by the length in its own frame
    const expected =
      STREAM_HEADER_LAYOUT.size + detailsLength + totalPages * WIRE_PAGE_LAYOUT.size + view.getUint32(at('total_payload_bytes'), true);
    const payloadLengthAt = WIRE_PAGE_LAYOUT.fields.find((field) => field.name === 'payload_length')?.offset;
    if (payloadLengthAt === undefined) throw new Error("nandlog_wire_page_t has no field 'payload_length'");

    const parts: Uint8Array[] = [header];
    let received = header.length;
    const meter = new ThroughputMeter();
    const report = (phase: DownloadProgress['phase']) => {
      meter.record(performance.now(), received);
      const estimate = meter.estimate(Math.max(expected, received));
      onProgress({
        phase, bytesReceived: received, bytesExpected: Math.max(expected, received), pagesExpected: totalPages,
        bytesPerSecond: estimate.bytesPerSecond, secondsRemaining: estimate.secondsRemaining,
      });
    };
    const take = async (count: number) => {
      for (let left = count; left > 0;) {
        if (signal?.aborted) throw new Error('Download cancelled');
        const chunk = await this.link.readSome(left, STREAM_IDLE_TIMEOUT_MS, idle);
        parts.push(chunk);
        received += chunk.length;
        left -= chunk.length;
        report('pages');
      }
    };

    report('pages');
    await take(detailsLength);
    for (let page = 0; page < totalPages; page += 1) {
      const frame = await this.link.read(WIRE_PAGE_LAYOUT.size, STREAM_IDLE_TIMEOUT_MS, idle);
      parts.push(frame);
      received += frame.length;
      await take(new DataView(frame.buffer, frame.byteOffset, frame.byteLength).getUint16(payloadLengthAt, true));
    }
    report('done');
    return join(...parts);
  }

  async disconnect(): Promise<void> {
    await this.link.close();
  }
}

/**
 * Open a port and find out which tag is on it.
 *
 * Null when the port cannot be opened (another tab or program has it) or the tag does not report
 * its EUI; in both cases the port is left closed.
 */
async function openPort(port: SerialPort): Promise<SerialTag | null> {
  let link: SerialLink;
  try {
    link = await SerialLink.open(port);
  } catch {
    return null;
  }
  try {
    const eui = await readEui(link);
    if (!eui) { await link.close(); return null; }
    let firmware: string | null = null;
    let hardware: string | null = null;
    try {
      link.discardInput();
      await link.write(Uint8Array.of(USB_COMMAND.USB_VERSION_COMMAND));
      firmware = await link.readLine(OPERATION_TIMEOUT_MS, 'Reading the firmware version');
      hardware = await link.readLine(OPERATION_TIMEOUT_MS, 'Reading the hardware revision');
    } catch { /* optional, as over Bluetooth */ }
    const handle = handleFor(eui);
    sessionPorts.set(handle, port);
    return new SerialTag(link, { handle, eui, firmware, hardware, transport: 'serial' });
  } catch (error) {
    await link.close();
    throw error;
  }
}

export const webSerialTransport: TagTransport = {
  id: 'serial',
  label: 'TotTag over USB',

  isAvailable: () => serial() !== undefined,

  unavailableReason() {
    if (serial()) return null;
    return 'This browser cannot talk to TotTags over USB. Chrome, Edge or Opera on a computer can; Safari and Firefox do not implement Web Serial.';
  },

  reconnectAcrossReloadsSupported() {
    return typeof serial()?.getPorts === 'function';
  },

  async requestDevice(): Promise<TagConnection | null> {
    const api = serial();
    if (!api) throw new Error(webSerialTransport.unavailableReason() ?? 'USB unavailable');
    let port: SerialPort;
    try {
      port = await api.requestPort({ filters: [TOTTAG_FILTER] });
    } catch (error) {
      if (error instanceof DOMException && error.name === 'NotFoundError') return null;   // dismissed
      throw error;
    }
    const connection = await openPort(port);
    if (!connection) {
      throw new Error(
        'That TotTag did not report its ID over USB. Close any other program or tab using it, or ' +
        'update its firmware — older firmware cannot be identified, and so cannot be scheduled, over USB.',
      );
    }
    return connection;
  },

  async connect(handle: string): Promise<TagConnection | null> {
    const api = serial();
    if (!api) return null;
    // The port this tag was last seen on first; if it has been unplugged and plugged back in, it is
    // a new port, so every other granted TotTag port gets asked which tag it is.
    const remembered = sessionPorts.get(handle);
    const candidates = [
      ...(remembered ? [remembered] : []),
      ...(await api.getPorts()).filter((port) => {
        const info = port.getInfo();
        return port !== remembered && info.usbVendorId === USB_VID && info.usbProductId === USB_PID;
      }),
    ];
    for (const port of candidates) {
      const connection = await openPort(port).catch(() => null);
      if (connection?.identity.handle === handle) return connection;
      await connection?.disconnect();
    }
    return null;
  },
};
