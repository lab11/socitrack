// The live radio check: devices ranging in a radio test, read over Bluetooth while it runs instead of from logs.
//
// A radio test is started by a maintenance command naming every device in it. Each device restarts into the test,
// ranges with the others for the time asked, logs nothing, and restarts back to normal when it ends. While it runs,
// each device notifies its ranges every round and answers reads of its radio counters. The recorder here turns those
// two streams into the same RadioSummary a downloaded log produces, so analyseRadio() judges a live test exactly as
// it judges logs, with no second set of thresholds to drift from the first.

import {
  BLE_MAINTENANCE_RADIO_TEST_HEADER_LEN, BLE_MAINTENANCE_START_RADIO_TEST, BLE_MAINTENANCE_STOP_RADIO_TEST,
  BLE_RADIO_STATS_FLAG_TEST_RUNNING, BLE_RADIO_STATS_FLAG_TEST_WAITING, BLE_RADIO_STATS_VERSION,
  COMPRESSED_RANGE_DATUM_LENGTH, EUI_LEN, MAX_NUM_RANGING_DEVICES, MAX_VALID_RANGE_MM, NUM_XMIT_ANTENNAS,
  RADIO_STATS_LAYOUT, RADIO_TEST_MAX_SECONDS, SCHEDULE_ROLE,
} from './constants.ts';
import type { PeerBin, RadioSummary } from './radioCheck.ts';

/** How long each bin of a live recording covers, short so that verdicts move as the test goes. */
export const LIVE_BIN_MS = 15_000;
/** Most a bin's ranges are scaled up for notifications Bluetooth dropped; past this, the bin is not trusted. */
const MAX_NOTIFICATION_SCALE = 3;

// --- Commands --------------------------------------------------------------------------------------------------

/**
 * The maintenance command that starts a radio test among `euis`, which must include the device it is written to.
 *
 * Times are Unix seconds. Every device in a test should be sent the same start and end, so their clocks agree on
 * when the test began; set each device's clock first.
 */
export function encodeRadioTestStartCommand(startTime: number, endTime: number, euis: readonly Uint8Array[]): Uint8Array {
  if (!euis.length || euis.length > MAX_NUM_RANGING_DEVICES) {
    throw new Error(`A radio test needs between 1 and ${MAX_NUM_RANGING_DEVICES} devices, not ${euis.length}.`);
  }
  if (!Number.isInteger(startTime) || !Number.isInteger(endTime) || endTime <= startTime) {
    throw new Error('A radio test must end after it starts, in whole seconds.');
  }
  if (endTime - startTime > RADIO_TEST_MAX_SECONDS) {
    throw new Error(`A radio test can run for at most ${RADIO_TEST_MAX_SECONDS / 60} minutes.`);
  }
  const out = new Uint8Array(BLE_MAINTENANCE_RADIO_TEST_HEADER_LEN + euis.length * EUI_LEN);
  const view = new DataView(out.buffer);
  out[0] = BLE_MAINTENANCE_START_RADIO_TEST;
  view.setUint32(1, startTime, true);
  view.setUint32(5, endTime, true);
  out[9] = euis.length;
  euis.forEach((eui, index) => {
    if (eui.length !== EUI_LEN) throw new Error(`Device ${index + 1} has a ${eui.length}-byte address; it must be ${EUI_LEN} bytes.`);
    out.set(eui, BLE_MAINTENANCE_RADIO_TEST_HEADER_LEN + index * EUI_LEN);
  });
  return out;
}

/** The maintenance command that ends a radio test early. */
export function encodeRadioTestStopCommand(): Uint8Array {
  return Uint8Array.of(BLE_MAINTENANCE_STOP_RADIO_TEST);
}

// --- What a device streams ------------------------------------------------------------------------------------------

/** A device's radio counters since it booted, as BLE_LIVE_STATS_RADIO_CHAR reports them. */
export interface RadioStats {
  /** schedule_role_t name, or the raw value for one this package does not know. */
  readonly role: string;
  readonly scheduleSize: number;
  readonly testRunning: boolean;
  /** Restarted into a test without its device list, which the client has to send again. */
  readonly testWaiting: boolean;
  readonly testSecondsLeft: number;
  /** Rounds this device took part in, and those that produced at least one range. */
  readonly roundsScheduled: number;
  readonly roundsRanged: number;
  readonly rxOk: number;
  readonly rxFailed: number;
  readonly rxOkByAntenna: readonly number[];
  readonly rxFailedByAntenna: readonly number[];
  readonly txLate: number;
  readonly rxArmLate: number;
  readonly isrOverBudget: number;
  readonly wakeMaxUs: number;
  readonly wakeFailures: number;
  /** Antenna, from 0, the device uses for schedules, join requests and status exchanges. */
  readonly antenna: number;
  /** Times that choice has moved since boot, saturating at 255. */
  readonly antennaChanges: number;
}

const roleName = (value: number) =>
  Object.entries(SCHEDULE_ROLE as Record<string, number>).find(([, v]) => v === value)?.[0] ?? String(value);

/** Decode a read of BLE_LIVE_STATS_RADIO_CHAR. Throws on a layout this package does not know. */
export function decodeRadioStats(bytes: Uint8Array): RadioStats {
  if (bytes.length < RADIO_STATS_LAYOUT.size) {
    throw new Error(`Radio statistics are ${RADIO_STATS_LAYOUT.size} bytes; this device sent ${bytes.length}.`);
  }
  const view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
  const field = (name: string) => RADIO_STATS_LAYOUT.fields.find((f) => f.name === name)!.offset;
  const version = view.getUint8(field('version'));
  if (version !== BLE_RADIO_STATS_VERSION) {
    throw new Error(`This device reports radio statistics in layout ${version}, which this page does not read. Update the page or the device.`);
  }
  const flags = view.getUint8(field('flags'));
  const perAntenna = (name: string) =>
    Array.from({ length: NUM_XMIT_ANTENNAS }, (_, antenna) => view.getUint32(field(name) + antenna * 4, true));
  return {
    role: roleName(view.getUint8(field('role'))),
    scheduleSize: view.getUint8(field('schedule_size')),
    testRunning: (flags & BLE_RADIO_STATS_FLAG_TEST_RUNNING) !== 0,
    testWaiting: (flags & BLE_RADIO_STATS_FLAG_TEST_WAITING) !== 0,
    testSecondsLeft: view.getUint16(field('test_seconds_left'), true),
    roundsScheduled: view.getUint32(field('rounds_scheduled'), true),
    roundsRanged: view.getUint32(field('rounds_ranged'), true),
    rxOk: view.getUint32(field('rx_ok'), true),
    rxFailed: view.getUint32(field('rx_failed'), true),
    rxOkByAntenna: perAntenna('rx_ok_by_antenna'),
    rxFailedByAntenna: perAntenna('rx_failed_by_antenna'),
    txLate: view.getUint16(field('tx_late'), true),
    rxArmLate: view.getUint16(field('rx_arm_late'), true),
    isrOverBudget: view.getUint16(field('isr_over_budget'), true),
    wakeMaxUs: view.getUint16(field('wake_max_us'), true),
    wakeFailures: view.getUint16(field('wake_failures'), true),
    antenna: view.getUint8(field('antenna')),
    antennaChanges: view.getUint8(field('antenna_changes')),
  };
}

/**
 * Decode one ranges notification: a count byte, then (u8 uid, u16 mm) pairs, filtered as a RANGES record is.
 *
 * `truncated` means the notification stopped short of its own count, which happens when the Bluetooth link
 * negotiated packets too small for a large network; the ranges that did arrive are still returned.
 */
export function decodeRangeResults(bytes: Uint8Array): { ranges: Map<number, number>; truncated: boolean } {
  const ranges = new Map<number, number>();
  if (!bytes.length) return { ranges, truncated: true };
  const count = Math.min(bytes[0]!, MAX_NUM_RANGING_DEVICES);
  const view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
  let decoded = 0;
  for (; decoded < count; decoded += 1) {
    const at = 1 + decoded * COMPRESSED_RANGE_DATUM_LENGTH;
    if (at + COMPRESSED_RANGE_DATUM_LENGTH > bytes.length) break;
    const millimetres = view.getUint16(at + 1, true);
    if (millimetres < MAX_VALID_RANGE_MM) ranges.set(bytes[at]!, millimetres);
  }
  return { ranges, truncated: decoded < bytes[0]! };
}

// --- Recording a live test -----------------------------------------------------------------------------------------

export interface LiveRadioRecorderOptions {
  /** Unix seconds the test started, the zero of its experiment time. */
  readonly startTime: number;
  /** Low EUI byte of every device in the test, and what to call each. */
  readonly deploymentUids: readonly number[];
  readonly deploymentLabels: readonly string[];
  /** The device this recorder listens to. */
  readonly selfUid: number;
}

function median(values: readonly number[]): number {
  const sorted = [...values].sort((a, b) => a - b);
  const mid = sorted.length >> 1;
  return sorted.length % 2 ? sorted[mid]! : (sorted[mid - 1]! + sorted[mid]!) / 2;
}

/**
 * Everything one device has streamed during a live test, reduced to a RadioSummary on request.
 *
 * Rounds are counted from the device's own counters wherever they are available, because a notification Bluetooth
 * dropped is a round the device still ranged in; ranges per peer come from the notifications, scaled bin by bin to
 * the rounds counted. Bluetooth delivers notifications in bunches, so a bin can hold more notifications than rounds
 * and is then scaled down. Only the time between the first and last counter reads is judged. Times passed in are
 * Unix milliseconds.
 */
export class LiveRadioRecorder {
  private readonly options: LiveRadioRecorderOptions;
  private readonly startMs: number;
  private firstMs: number | null = null;
  private lastMs: number | null = null;
  private firstReadMs: number | null = null;
  private readonly notifiedRounds = new Map<number, number>();
  private readonly countedRounds = new Map<number, number>();
  private readonly peerValues = new Map<number, Map<number, number[]>>();
  private previous: { readonly ms: number; readonly stats: RadioStats } | null = null;
  private banked = { rxOk: 0, rxFailed: 0, wakeFailures: 0, rxArmLate: 0, txLate: 0 };
  private bankedOk = new Array<number>(NUM_XMIT_ANTENNAS).fill(0);
  private bankedFailed = new Array<number>(NUM_XMIT_ANTENNAS).fill(0);
  private samples = 0;
  private truncatedNotifications = 0;

  constructor(options: LiveRadioRecorderOptions) {
    this.options = options;
    this.startMs = options.startTime * 1000;
  }

  private touch(ms: number): number {
    const at = Math.max(0, ms - this.startMs);
    this.firstMs = this.firstMs === null ? at : Math.min(this.firstMs, at);
    this.lastMs = this.lastMs === null ? at : Math.max(this.lastMs, at);
    return at;
  }

  /** One round's ranges, as notified. */
  addRanges(atMs: number, ranges: ReadonlyMap<number, number>, truncated = false): void {
    const at = this.touch(atMs);
    if (truncated) this.truncatedNotifications += 1;
    if (!ranges.size) return;
    const bin = Math.floor(at / LIVE_BIN_MS);
    this.notifiedRounds.set(bin, (this.notifiedRounds.get(bin) ?? 0) + 1);
    for (const [uid, millimetres] of ranges) {
      if (!this.peerValues.has(uid)) this.peerValues.set(uid, new Map());
      const bins = this.peerValues.get(uid)!;
      if (!bins.has(bin)) bins.set(bin, []);
      bins.get(bin)!.push(millimetres);
    }
  }

  /** One read of the radio counters, which are cumulative since the device booted into the test. */
  addStats(atMs: number, stats: RadioStats): void {
    const at = this.touch(atMs);
    // A device that restarted mid-test counts from zero again, so keep what it had reached
    const restarted = this.previous !== null &&
      (stats.rxOk < this.previous.stats.rxOk || stats.roundsRanged < this.previous.stats.roundsRanged);
    if (restarted) {
      const last = this.previous!.stats;
      this.banked = {
        rxOk: this.banked.rxOk + last.rxOk, rxFailed: this.banked.rxFailed + last.rxFailed,
        wakeFailures: this.banked.wakeFailures + last.wakeFailures, rxArmLate: this.banked.rxArmLate + last.rxArmLate,
        txLate: this.banked.txLate + last.txLate,
      };
      last.rxOkByAntenna.forEach((count, antenna) => { this.bankedOk[antenna]! += count; });
      last.rxFailedByAntenna.forEach((count, antenna) => { this.bankedFailed[antenna]! += count; });
    }

    // Rounds since the last read, spread over the time between the two reads. The first read is only a starting
    // point, as nothing says when within the time before it its rounds fell.
    if (this.previous) {
      const rounds = stats.roundsRanged - (restarted ? 0 : this.previous.stats.roundsRanged);
      this.spread(rounds, this.previous.ms, at);
    } else {
      this.firstReadMs = at;
    }
    this.previous = { ms: at, stats };
    this.samples += 1;
  }

  private spread(rounds: number, fromMs: number, toMs: number): void {
    if (rounds <= 0) return;
    if (toMs <= fromMs) {
      const bin = Math.floor(toMs / LIVE_BIN_MS);
      this.countedRounds.set(bin, (this.countedRounds.get(bin) ?? 0) + rounds);
      return;
    }
    for (let bin = Math.floor(fromMs / LIVE_BIN_MS); bin * LIVE_BIN_MS < toMs; bin += 1) {
      const overlap = Math.min(toMs, (bin + 1) * LIVE_BIN_MS) - Math.max(fromMs, bin * LIVE_BIN_MS);
      if (overlap > 0) this.countedRounds.set(bin, (this.countedRounds.get(bin) ?? 0) + rounds * (overlap / (toMs - fromMs)));
    }
  }

  /** The most recent counters, for showing progress before any verdict is possible. */
  get latestStats(): RadioStats | null {
    return this.previous?.stats ?? null;
  }

  /** Notifications that stopped short of their own count, which only a too-small Bluetooth packet size causes. */
  get truncated(): number {
    return this.truncatedNotifications;
  }

  summary(): RadioSummary {
    const counted = this.samples > 0;
    const rowsByBin = [...(counted ? this.countedRounds : this.notifiedRounds)]
      .map(([bin, rounds]) => [bin, Math.round(rounds)] as const)
      .filter(([, rounds]) => rounds > 0)
      .sort((a, b) => a[0] - b[0]);

    // A bin's ranges per peer, scaled to the rounds counted in it: up for notifications that never arrived, down for
    // ones that arrived in a bunch from the bin before
    const scale = (bin: number) => {
      const notified = this.notifiedRounds.get(bin) ?? 0;
      const rounds = this.countedRounds.get(bin) ?? 0;
      return counted && notified ? Math.min(MAX_NOTIFICATION_SCALE, rounds / notified) : 1;
    };
    const peers = [...this.peerValues].map(([uid, bins]) => ({
      uid,
      bins: [...bins].sort((a, b) => a[0] - b[0]).map(([bin, values]): PeerBin => {
        const centre = median(values);
        return [bin, Math.round(values.length * scale(bin)), centre, median(values.map((value) => Math.abs(value - centre)))];
      }),
    }));

    const latest = this.previous?.stats;
    return {
      experimentStartTime: this.options.startTime,
      deploymentUids: this.options.deploymentUids,
      deploymentLabels: this.options.deploymentLabels,
      selfUid: this.options.selfUid,
      firstMs: counted ? this.firstReadMs : this.firstMs,
      lastMs: counted ? this.previous!.ms : this.lastMs,
      binMs: LIVE_BIN_MS,
      rowsByBin,
      peers,
      diagnostics: latest ? {
        rxOk: this.banked.rxOk + latest.rxOk,
        rxFailed: this.banked.rxFailed + latest.rxFailed,
        rxOkByAntenna: latest.rxOkByAntenna.map((count, antenna) => count + this.bankedOk[antenna]!),
        rxFailedByAntenna: latest.rxFailedByAntenna.map((count, antenna) => count + this.bankedFailed[antenna]!),
        wakeFailures: this.banked.wakeFailures + latest.wakeFailures,
        irqStuck: 0,
        rxArmLate: this.banked.rxArmLate + latest.rxArmLate,
        txLate: this.banked.txLate + latest.txLate,
        samples: this.samples,
      } : null,
      aborts: 0,
    };
  }
}
