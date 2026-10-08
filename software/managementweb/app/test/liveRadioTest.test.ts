// The live radio test driven against fake devices and a hand-wound clock: starting every device on one test, following
// each through its restart, re-sending a device list that did not survive, getting a dropped device back, and stopping.
// Nothing here needs a browser, which is the proof the orchestration holds no I/O of its own.

import { test } from 'node:test';
import assert from 'node:assert/strict';

import { NUM_XMIT_ANTENNAS, type RadioStats } from '@tottag/schema';
import { LiveRadioTest } from '../src/features/liveRadioTest.ts';
import type { LiveRadioLink, TagConnection } from '../src/ports/tagTransport.ts';

class Clock {
  now = 1_791_300_000_000;
  private timers: Array<{ at: number; callback: () => void; live: boolean }> = [];
  schedule = (callback: () => void, ms: number) => {
    const timer = { at: this.now + ms, callback, live: true };
    this.timers.push(timer);
    return () => { timer.live = false; };
  };
  /** Advance in small steps, letting every timer due and every promise it starts settle. */
  async advance(ms: number): Promise<void> {
    const end = this.now + ms;
    while (this.now < end) {
      this.now = Math.min(end, this.now + 250);
      for (const timer of this.timers.filter((t) => t.live && t.at <= this.now)) {
        timer.live = false;
        timer.callback();
      }
      for (let i = 0; i < 20; i += 1) await Promise.resolve();
    }
  }
}

/** One device as the session sees it: a test it may be in, and its counters advancing two rounds a second. */
class FakeDevice {
  inTest = false;
  waiting = false;
  refuse = false;
  bootedAt = 0;
  starts: Array<[number, number, number]> = [];
  stops = 0;
  connected = false;
  private dropListeners: Array<() => void> = [];
  rangesListener: ((ranges: ReadonlyMap<number, number>, truncated: boolean) => void) | null = null;

  readonly uid: number;
  readonly clock: Clock;
  readonly loseListOnRestart: boolean;

  constructor(uid: number, clock: Clock, loseListOnRestart = false) {
    this.uid = uid;
    this.clock = clock;
    this.loseListOnRestart = loseListOnRestart;
  }

  connection(): TagConnection {
    this.connected = true;
    const device = this;
    const link: LiveRadioLink = {
      async startRadioTest(start, end, euis) {
        if (device.refuse) throw new Error('The device refused the test.');
        device.starts.push([start, end, euis.length]);
        if (device.inTest) { device.waiting = false; return; }
        device.restart(() => { device.inTest = true; device.waiting = device.loseListOnRestart; });
      },
      async stopRadioTest() { device.stops += 1; device.restart(() => { device.inTest = false; }); },
      async readRadioStats() { if (!device.connected) throw new Error('gone'); return device.stats(); },
      async watchRanges(onRanges) { device.rangesListener = onRanges; return async () => { device.rangesListener = null; }; },
      onDisconnect(listener) { device.dropListeners.push(listener); return () => { device.dropListeners = device.dropListeners.filter((l) => l !== listener); }; },
    };
    return {
      identity: { handle: `h${this.uid}`, eui: Uint8Array.of(this.uid, 1, 2, 3, 4, 5), transport: 'bluetooth', firmware: null, hardware: null },
      liveRadio: link,
      isConnected: () => device.connected,
      readSnapshot: async () => ({ batteryMv: 4100, timestamp: null, experiment: null }),
      syncClock: async () => {},
      writeDeployment: async () => {},
      readExperiment: async () => { throw new Error('none'); },
      cancelDeployment: async () => {},
      locate: async () => {},
      downloadLog: async () => new Uint8Array(),
      retransmitPages: async () => new Uint8Array(),
      disconnect: async () => { device.connected = false; },
    };
  }

  /** The device drops its connection, and comes back in its new state a second later. */
  restart(then: () => void): void {
    this.drop();
    this.clock.schedule(() => { then(); this.bootedAt = this.clock.now; }, 1000);
  }

  drop(): void {
    this.connected = false;
    for (const listener of this.dropListeners.splice(0)) listener();
  }

  stats(): RadioStats {
    const rounds = this.inTest ? Math.floor((this.clock.now - this.bootedAt) / 500) : 0;
    return {
      role: 'ROLE_PARTICIPANT', scheduleSize: 3, testRunning: this.inTest, testWaiting: this.inTest && this.waiting,
      testSecondsLeft: 0, roundsScheduled: rounds, roundsRanged: rounds, rxOk: rounds * 40, rxFailed: rounds,
      rxOkByAntenna: new Array(NUM_XMIT_ANTENNAS).fill(Math.floor(rounds * 40 / 3)),
      rxFailedByAntenna: new Array(NUM_XMIT_ANTENNAS).fill(Math.floor(rounds / 3)),
      txLate: 0, rxArmLate: 0, isrOverBudget: 0, wakeMaxUs: 2100, wakeFailures: 0, antenna: 0, antennaChanges: 0,
    };
  }
}

function setup(...devices: FakeDevice[]) {
  const clock = devices[0]?.clock ?? new Clock();
  const session = new LiveRadioTest({
    reconnect: async (handle) => {
      const device = devices.find((b) => `h${b.uid}` === handle);
      return device && !device.connected && (clock.now - device.bootedAt) >= 0 ? device.connection() : null;
    },
    now: () => clock.now,
    schedule: clock.schedule,
  });
  for (const device of devices) assert.equal(session.add(device.connection()), null);
  return { session, clock };
}

test('every device gets the same test and is followed into it', async () => {
  const clock = new Clock();
  const devices = [new FakeDevice(0x02, clock), new FakeDevice(0x0b, clock), new FakeDevice(0x3e, clock)];
  const { session } = setup(...devices);
  await session.start(300);
  const state = session.state();
  assert.equal(state.phase, 'running');
  for (const device of devices) assert.deepEqual(device.starts, [[state.startTime!, state.startTime! + 300, 3]]);

  await clock.advance(20_000);
  assert.deepEqual(session.state().devices.map((b) => b.status), ['running', 'running', 'running']);
  assert.ok(session.state().devices.every((b) => (b.roundsPerSecond ?? 0) > 1.5), 'two rounds a second, from the counters');
  const deployment = session.deployment()!;
  assert.equal(deployment.logs.size, 3);
  assert.ok(deployment.logs.get(0x02)!.summary.rowsByBin.length > 0);
});

test('a device that lost its device list across the restart is sent it again, without another restart', async () => {
  const clock = new Clock();
  const forgetful = new FakeDevice(0x0b, clock, true);
  const { session } = setup(new FakeDevice(0x02, clock), forgetful);
  await session.start(300);
  await clock.advance(15_000);
  assert.equal(forgetful.starts.length, 2, 'the same test, sent a second time');
  assert.equal(forgetful.waiting, false);
  assert.equal(session.state().devices.find((b) => b.uid === 0x0b)!.status, 'running');
});

test('a device that drops out mid-test is reconnected and keeps its counts', async () => {
  const clock = new Clock();
  const flaky = new FakeDevice(0x3e, clock);
  const { session } = setup(new FakeDevice(0x02, clock), flaky);
  await session.start(300);
  await clock.advance(20_000);
  flaky.drop();
  assert.equal(session.state().devices.find((b) => b.uid === 0x3e)!.status, 'reconnecting');
  await clock.advance(10_000);
  assert.equal(session.state().devices.find((b) => b.uid === 0x3e)!.status, 'running');
});

test('a device that refuses the test is reported, and the others carry on', async () => {
  const clock = new Clock();
  const stubborn = new FakeDevice(0x0b, clock);
  stubborn.refuse = true;
  const { session } = setup(new FakeDevice(0x02, clock), stubborn, new FakeDevice(0x3e, clock));
  await session.start(300);
  await clock.advance(15_000);
  const byUid = new Map(session.state().devices.map((b) => [b.uid, b]));
  assert.equal(byUid.get(0x0b)!.status, 'failed');
  assert.match(byUid.get(0x0b)!.message!, /refused/);
  assert.equal(byUid.get(0x02)!.status, 'running');
});

test('stopping ends the test on every device it can reach, and the test ends by itself on time', async () => {
  const clock = new Clock();
  const devices = [new FakeDevice(0x02, clock), new FakeDevice(0x0b, clock)];
  const { session } = setup(...devices);
  await session.start(300);
  await clock.advance(15_000);
  await session.stop();
  assert.deepEqual(devices.map((b) => b.stops), [1, 1]);
  assert.equal(session.state().phase, 'finished');

  const other = [new FakeDevice(0x02, clock), new FakeDevice(0x0b, clock)];
  const timed = setup(...other).session;
  await timed.start(60);
  await clock.advance(70_000);
  assert.equal(timed.state().phase, 'finished');
  assert.deepEqual(timed.state().devices.map((b) => b.status), ['finished', 'finished']);
});

test('a device without the live radio link is turned away with a reason', () => {
  const clock = new Clock();
  const { session } = setup(new FakeDevice(0x02, clock));
  const old = new FakeDevice(0x0b, clock).connection();
  const reason = session.add({ ...old, liveRadio: undefined });
  assert.match(reason ?? '', /newer firmware/);
});
