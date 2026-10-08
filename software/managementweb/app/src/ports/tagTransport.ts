// Talking to a tag, independent of how.
//
// Two implementations: Web Bluetooth for a tag on its charger, and Web Serial for a tag plugged
// into this computer over USB, which puts it into USB maintenance mode. Either can identify,
// configure and download a tag, so everything that uses a tag goes through this interface and
// neither knows nor cares which one it has.

import type { DeploymentConfig, ExperimentDetails, RadioStats } from '@tottag/schema';

export interface TagIdentity {
  /** Stable within this browser profile. NOT the MAC address — Web Bluetooth never exposes one. */
  readonly handle: string;
  /** Full 6-byte EUI, least-significant byte first: from GATT System ID, or asked for over USB. */
  readonly eui: Uint8Array;
  /** Which transport `handle` belongs to, and so which one reconnects to it. */
  readonly transport: TagTransport['id'];
  readonly firmware: string | null;
  readonly hardware: string | null;
}

export interface TagSnapshot {
  readonly batteryMv: number | null;
  readonly timestamp: number | null;
  readonly experiment: ExperimentDetails | null;
}

export interface DownloadProgress {
  readonly phase: 'starting' | 'header' | 'pages' | 'done';
  readonly bytesReceived: number;
  /** Null until the stream header arrives and says how much there is. */
  readonly bytesExpected: number | null;
  readonly pagesExpected: number | null;
  /**
   * Measured from this transfer, not from a constant.
   *
   * Null while warming up. Both are null early on by design: a rate computed from the first moments
   * of a connection — which include MTU exchange and a connection-parameter update — is wrong in
   * the alarming direction and then visibly halves, which is how a progress bar earns distrust.
   */
  readonly bytesPerSecond: number | null;
  readonly secondsRemaining: number | null;
}

/**
 * What a live radio test needs from a tag. Only Bluetooth provides it: a tag on USB is in maintenance mode and
 * cannot range, so a Web Serial connection leaves `liveRadio` undefined.
 */
export interface LiveRadioLink {
  /**
   * Start a radio test among `euis`, this tag included, from `startTime` to `endTime` (Unix seconds). The tag
   * restarts into the test, so this connection drops a moment later. Rejects if the tag refuses the test.
   */
  startRadioTest(startTime: number, endTime: number, euis: readonly Uint8Array[]): Promise<void>;
  /** End the test early; the tag restarts into whatever it would otherwise be doing. */
  stopRadioTest(): Promise<void>;
  readRadioStats(): Promise<RadioStats>;
  /** Deliver every ranges notification until the returned function is called. */
  watchRanges(onRanges: (ranges: ReadonlyMap<number, number>, truncated: boolean) => void): Promise<() => Promise<void>>;
  /** Call `listener` once if the connection drops; the returned function cancels that. */
  onDisconnect(listener: () => void): () => void;
}

export interface TagConnection {
  readonly identity: TagIdentity;
  /** Present only when this transport and this tag's firmware can run a live radio test. */
  readonly liveRadio?: LiveRadioLink;
  isConnected(): boolean;
  readSnapshot(): Promise<TagSnapshot>;
  /** Set the tag's clock to now. Always done before writing a deployment. */
  syncClock(): Promise<void>;
  writeDeployment(config: DeploymentConfig): Promise<void>;
  /** Read the stored deployment back, for confirming what the device actually took. */
  readExperiment(): Promise<ExperimentDetails>;
  cancelDeployment(): Promise<void>;
  /** Sound the buzzer so a tag can be found in a bag. */
  locate(seconds: number): Promise<void>;
  /**
   * Pull the log. `range` is Unix seconds; omit for everything.
   *
   * Returns the offload stream verbatim — exactly the bytes a `.ttg` file holds — so the same
   * parser reads a live download and a file from disk, and neither path can drift from the other.
   */
  downloadLog(
    range: { start: number; end: number } | null,
    onProgress: (progress: DownloadProgress) => void,
    signal?: AbortSignal,
  ): Promise<Uint8Array>;
  /** Ask for specific pages again, by sequence number, after a CRC failure or a hole. */
  retransmitPages(seqs: readonly number[], onProgress: (progress: DownloadProgress) => void): Promise<Uint8Array>;
  disconnect(): Promise<void>;
}

export interface TagTransport {
  readonly id: 'bluetooth' | 'serial';
  readonly label: string;
  isAvailable(): boolean;
  unavailableReason(): string | null;

  /**
   * Show the chooser and connect to whatever is picked. Null when the chooser was dismissed.
   *
   * Returns a LIVE connection rather than just an identity, so a caller that is about to do
   * something does not pay for a second connection. The caller owns it and must disconnect.
   * Identifying at all costs a connection, because Web Bluetooth never exposes a MAC address — the
   * EUI has to be read from GATT System ID.
   */
  requestDevice(): Promise<TagConnection | null>;

  /**
   * Reconnect to a device already granted to this origin, with no chooser.
   *
   * Devices chosen earlier IN THIS PAGE SESSION always work. Across a reload it depends on
   * `navigator.bluetooth.getDevices()`, which several Chrome versions gate behind a flag — see
   * `reconnectAcrossReloadsSupported()`. Returns null when the tag cannot be reached.
   */
  connect(handle: string): Promise<TagConnection | null>;

  /** Whether a tag added before a page reload can be reconnected without the chooser. */
  reconnectAcrossReloadsSupported(): boolean;
}
