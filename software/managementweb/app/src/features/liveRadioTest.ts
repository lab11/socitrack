// Running a live radio test: the devices picked, the commands that start and stop it, and keeping up with each device
// through the restart into the test and any dropped connection after. Pure orchestration over TagConnection, so it
// runs against fake tags in tests; the page subscribes and renders.

import { LiveRadioRecorder, type RadioStats } from '@tottag/schema';
import type { TagConnection } from '../ports/tagTransport.ts';
import { shortId, type RadioDeployment } from './radioCheck.ts';

export type LiveDeviceStatus =
  | 'ready'          // connected, no test yet
  | 'starting'       // being sent the start command
  | 'restarting'     // restarting into the test, to be reconnected
  | 'waiting'        // in the test but lost its device list across the restart, which is being sent again
  | 'running'        // in the test and streaming
  | 'reconnecting'   // connection dropped mid-test
  | 'finished'
  | 'failed';

export interface LiveDevice {
  readonly handle: string;
  readonly eui: Uint8Array;
  readonly uid: number;
  readonly label: string;
  readonly status: LiveDeviceStatus;
  readonly message: string | null;
  readonly stats: RadioStats | null;
  /** Rounds ranged per second between the last two counter reads. */
  readonly roundsPerSecond: number | null;
  /** Range notifications that arrived cut short by a small Bluetooth packet size. */
  readonly truncated: number;
}

export interface LiveTestState {
  readonly phase: 'setup' | 'starting' | 'running' | 'finished';
  /** Unix seconds. */
  readonly startTime: number | null;
  readonly endTime: number | null;
  readonly devices: readonly LiveDevice[];
}

export interface LiveTestDependencies {
  /** Reconnect to a device already picked in this page session, or null if it cannot be reached yet. */
  readonly reconnect: (handle: string) => Promise<TagConnection | null>;
  readonly now?: () => number;
  readonly schedule?: (callback: () => void, ms: number) => () => void;
  readonly pollMs?: number;
  readonly reconnectMs?: number;
}

/** A device restarting into the test is unreachable for a couple of seconds; give it that before the first try. */
const RESTART_SETTLE_MS = 2_500;
/** How long after the start a device still outside the test is taken to be on its way in, rather than to have refused. */
const START_GRACE_MS = 30_000;

interface Entry {
  readonly handle: string;
  readonly eui: Uint8Array;
  readonly uid: number;
  readonly label: string;
  status: LiveDeviceStatus;
  message: string | null;
  connection: TagConnection | null;
  recorder: LiveRadioRecorder | null;
  previous: { ms: number; stats: RadioStats } | null;
  roundsPerSecond: number | null;
  /** Bumped whenever a connection is taken up or let go, so callbacks from an older one are ignored. */
  generation: number;
  cancel: Array<() => void>;
  unwatch: (() => Promise<void>) | null;
}

export class LiveRadioTest {
  private readonly deps: Required<LiveTestDependencies>;
  private readonly entries: Entry[] = [];
  private readonly listeners = new Set<() => void>();
  private phase: LiveTestState['phase'] = 'setup';
  private startTime: number | null = null;
  private endTime: number | null = null;
  private euis: Uint8Array[] = [];

  constructor(deps: LiveTestDependencies) {
    this.deps = {
      now: () => Date.now(),
      schedule: (callback, ms) => { const timer = setTimeout(callback, ms); return () => clearTimeout(timer); },
      pollMs: 5_000,
      reconnectMs: 2_000,
      ...deps,
    };
  }

  subscribe(listener: () => void): () => void {
    this.listeners.add(listener);
    return () => this.listeners.delete(listener);
  }

  private changed(): void {
    for (const listener of this.listeners) listener();
  }

  state(): LiveTestState {
    return {
      phase: this.phase,
      startTime: this.startTime,
      endTime: this.endTime,
      devices: this.entries.map((entry) => ({
        handle: entry.handle, eui: entry.eui, uid: entry.uid, label: entry.label, status: entry.status,
        message: entry.message, stats: entry.previous?.stats ?? null, roundsPerSecond: entry.roundsPerSecond,
        truncated: entry.recorder?.truncated ?? 0,
      })),
    };
  }

  /** Add a device picked in the chooser. Returns why it cannot take part, or null. */
  add(connection: TagConnection, label?: string): string | null {
    if (this.phase !== 'setup') return 'A test is already under way.';
    if (!connection.liveRadio) return `${shortId(connection.identity.eui[0] ?? 0)} cannot run a live test: it needs newer firmware, and a Bluetooth connection.`;
    const uid = connection.identity.eui[0] ?? 0;
    const existing = this.entries.find((entry) => entry.uid === uid);
    if (existing) {
      // Picked twice: keep the newer connection
      void existing.connection?.disconnect();
      existing.connection = connection;
      this.changed();
      return null;
    }
    this.entries.push({
      handle: connection.identity.handle, eui: connection.identity.eui, uid, label: label?.trim() || shortId(uid),
      status: 'ready', message: null, connection, recorder: null, previous: null, roundsPerSecond: null,
      generation: 0, cancel: [], unwatch: null,
    });
    this.changed();
    return null;
  }

  remove(handle: string): void {
    const index = this.entries.findIndex((entry) => entry.handle === handle);
    if (index < 0 || this.phase !== 'setup') return;
    void this.entries[index]!.connection?.disconnect();
    this.entries.splice(index, 1);
    this.changed();
  }

  /** Start every device on the same test, then follow each one into it. */
  async start(durationSeconds: number): Promise<void> {
    if (this.phase !== 'setup' || this.entries.length < 2) return;
    this.phase = 'starting';
    this.startTime = Math.floor(this.deps.now() / 1000);
    this.endTime = this.startTime + durationSeconds;
    this.euis = this.entries.map((entry) => entry.eui);
    this.changed();

    for (const entry of this.entries) {
      const link = entry.connection?.liveRadio;
      if (!entry.connection || !link) {
        this.fail(entry, 'Not connected when the test started.');
        continue;
      }
      entry.status = 'starting';
      this.changed();
      try {
        // Clock first, so every device agrees on when the test began
        await entry.connection.syncClock();
        await link.startRadioTest(this.startTime, this.endTime, this.euis);
      } catch (error) {
        this.fail(entry, error instanceof Error ? error.message : String(error));
        continue;
      }
      entry.recorder = new LiveRadioRecorder({
        startTime: this.startTime,
        deploymentUids: this.entries.map((other) => other.uid),
        deploymentLabels: this.entries.map((other) => other.label),
        selfUid: entry.uid,
      });
      this.letGo(entry, 'restarting');
      this.reconnectLater(entry, RESTART_SETTLE_MS);
    }
    this.phase = 'running';
    this.changed();
    this.deps.schedule(() => this.finish(), (this.endTime - this.startTime) * 1000 + this.deps.pollMs);
  }

  /** End the test early: each device restarts into whatever it would otherwise be doing. */
  async stop(): Promise<void> {
    if (this.phase !== 'running' && this.phase !== 'starting') return;
    const connected = this.entries.filter((entry) => entry.connection?.liveRadio);
    this.phase = 'finished';
    for (const entry of connected) {
      try { await entry.connection!.liveRadio!.stopRadioTest(); } catch { /* restarting anyway, or out of reach */ }
    }
    for (const entry of this.entries) this.letGo(entry, entry.status === 'failed' ? 'failed' : 'finished');
    this.changed();
  }

  /** Release every connection, for when the page goes away. */
  dispose(): void {
    for (const entry of this.entries) {
      this.letGo(entry, entry.status);
      void entry.connection?.disconnect();
    }
    this.listeners.clear();
  }

  /** The test as the radio check judges a deployment: each device's recording standing in for its log. */
  deployment(): RadioDeployment | null {
    if (this.startTime === null) return null;
    const logs = new Map(this.entries.filter((entry) => entry.recorder)
      .map((entry) => [entry.uid, { name: entry.label, summary: entry.recorder!.summary() }] as const));
    return {
      key: `live-${this.startTime}`,
      startTime: this.startTime,
      devices: this.entries.map((entry) => ({ uid: entry.uid, label: entry.label })),
      logs,
      unmatched: [],
    };
  }

  // --- Following one device ---------------------------------------------------------------------------------------

  private fail(entry: Entry, message: string): void {
    this.letGo(entry, 'failed');
    entry.message = message;
    this.changed();
  }

  private letGo(entry: Entry, status: LiveDeviceStatus): void {
    entry.generation += 1;
    for (const cancel of entry.cancel.splice(0)) cancel();
    const unwatch = entry.unwatch;
    entry.unwatch = null;
    if (unwatch) void unwatch();
    if (status === 'finished' || status === 'failed') void entry.connection?.disconnect();
    entry.connection = status === 'ready' ? entry.connection : null;
    entry.status = status;
  }

  private testOver(): boolean {
    return this.phase === 'finished' || (this.endTime !== null && this.deps.now() >= this.endTime * 1000);
  }

  private reconnectLater(entry: Entry, ms: number): void {
    const generation = entry.generation;
    entry.cancel.push(this.deps.schedule(() => { void this.tryReconnect(entry, generation); }, ms));
  }

  private async tryReconnect(entry: Entry, generation: number): Promise<void> {
    if (generation !== entry.generation || this.testOver()) return;
    let connection: TagConnection | null = null;
    try { connection = await this.deps.reconnect(entry.handle); } catch { connection = null; }
    if (generation !== entry.generation || this.testOver()) {
      void connection?.disconnect();
      return;
    }
    if (!connection?.liveRadio) {
      void connection?.disconnect();
      this.reconnectLater(entry, this.deps.reconnectMs);
      return;
    }
    await this.attach(entry, connection);
  }

  private async attach(entry: Entry, connection: TagConnection): Promise<void> {
    const link = connection.liveRadio!;
    entry.generation += 1;
    const generation = entry.generation;
    entry.connection = connection;
    entry.cancel.push(link.onDisconnect(() => {
      if (generation !== entry.generation || this.testOver()) return;
      this.letGo(entry, 'reconnecting');
      this.changed();
      this.reconnectLater(entry, this.deps.reconnectMs);
    }));
    try {
      entry.unwatch = await link.watchRanges((ranges, truncated) => {
        if (generation === entry.generation) entry.recorder?.addRanges(this.deps.now(), ranges, truncated);
      });
    } catch {
      // Ranges still count from the device's own counter; only distances go missing
    }
    await this.poll(entry, generation);
  }

  private async poll(entry: Entry, generation: number): Promise<void> {
    if (generation !== entry.generation) return;
    if (this.testOver()) {
      this.letGo(entry, 'finished');
      this.changed();
      return;
    }
    const link = entry.connection?.liveRadio;
    if (!link) return;
    try {
      const stats = await link.readRadioStats();
      if (generation !== entry.generation) return;
      const now = this.deps.now();
      if (stats.testWaiting) {
        // Restarted without its device list: send the same test again, which it takes without restarting
        entry.status = 'waiting';
        await link.startRadioTest(this.startTime!, this.endTime!, this.euis);
      } else if (!stats.testRunning) {
        // Reached before its restart into the test, which a device flushing a deployment's log can be slow to make
        if (!entry.previous && now < (this.startTime! * 1000) + START_GRACE_MS) {
          const stale = entry.connection;
          this.letGo(entry, 'restarting');
          void stale?.disconnect();
          this.changed();
          this.reconnectLater(entry, this.deps.reconnectMs);
          return;
        }
        this.fail(entry, 'This device is not running the test. It may have restarted out of it, or refused it.');
        return;
      } else {
        if (entry.previous && now > entry.previous.ms) {
          const rounds = stats.roundsRanged - entry.previous.stats.roundsRanged;
          entry.roundsPerSecond = rounds >= 0 ? rounds / ((now - entry.previous.ms) / 1000) : null;
        }
        entry.previous = { ms: now, stats };
        entry.recorder?.addStats(now, stats);
        entry.status = 'running';
        entry.message = null;
      }
      this.changed();
    } catch {
      // A read that fails on a live connection is retried at the next poll; a dropped one is reconnected
    }
    if (generation === entry.generation) entry.cancel.push(this.deps.schedule(() => { void this.poll(entry, generation); }, this.deps.pollMs));
  }

  private finish(): void {
    if (this.phase !== 'running') return;
    this.phase = 'finished';
    for (const entry of this.entries) this.letGo(entry, entry.status === 'failed' ? 'failed' : 'finished');
    this.changed();
  }
}
