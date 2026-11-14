// Every distance a log measured, kept compactly enough to stay in memory. Pure.
//
// The parsed records of a worst-case week take about 757 MB and are discarded once a log is analysed.
// Two views need every round rather than a per-minute summary: daily time-together statistics, which
// count rounds and change with the time zone, the peers chosen and the thresholds; and the side-by-side
// comparison of two tags, which shows each round. So the ranging is kept on its own, as columns:
//
//   - one Uint32 per ranging round: its time, in ms since the deployment started;
//   - one Uint16 per round for each peer the log ever ranged to: the distance in mm, or NO_RANGE.
//
// That is 4 + 2 × peers bytes per round. A week of all-day recording is about 1.2 million rounds, so
// with nine peers (a ten-tag deployment, the worst case) it is 26 MB, about 3.5% of what the records
// took; a typical week (four tags, thirteen hours a day) is under 7 MB. Peers are columns rather than
// (time, peer, distance) triples because in a working deployment most peers are heard in most rounds,
// where a column costs 2 bytes per round and a triple 7.

import { ROUNDS_PER_MINUTE } from './radioCheck.ts';
import type { ParseResult } from './log.ts';
import { thinSeries, type Series } from './series.ts';
import { dayNumberToDate, localClock, localDayStart } from './timezone.ts';

/** A round in which this peer was not ranged. Above any distance the firmware stores (MAX_VALID_RANGE_MM). */
export const NO_RANGE = 0xffff;

/** Ranged rounds to one peer further apart than this are drawn with a break between them. */
export const ROUND_GAP_S = 2;

export interface RangeStore {
  /** Unix seconds the deployment started. */
  readonly start: number;
  /** Each round's time, ms since the start, ascending. */
  readonly ms: Uint32Array;
  /** Every peer this log ranged to, by one-byte ID. */
  readonly peers: readonly number[];
  /** Per peer, in the order of `peers`: each round's distance in mm, or NO_RANGE. */
  readonly mm: readonly Uint16Array[];
}

/** Bytes a store holds. */
export function rangeStoreBytes(store: RangeStore): number {
  return store.ms.byteLength + store.mm.reduce((sum, column) => sum + column.byteLength, 0);
}

/** Keep every ranging round of a parsed log, in time order. */
export function buildRangeStore(result: ParseResult): RangeStore {
  const peerSet = new Set<number>();
  let rounds = 0;
  for (const record of result.records) {
    if (record.kind !== 'ranges') continue;
    rounds += 1;
    for (const peer of record.ranges.keys()) peerSet.add(peer);
  }
  const peers = [...peerSet].sort((a, b) => a - b);
  const column = new Map(peers.map((peer, index) => [peer, index]));
  const ms = new Uint32Array(rounds);
  const mm = peers.map(() => new Uint16Array(rounds).fill(NO_RANGE));
  let row = 0;
  let sorted = true;
  for (const record of result.records) {
    if (record.kind !== 'ranges') continue;
    ms[row] = record.ms;
    if (row > 0 && record.ms < ms[row - 1]!) sorted = false;
    for (const [peer, millimeters] of record.ranges) mm[column.get(peer)!]![row] = millimeters;
    row += 1;
  }
  if (sorted) return { start: result.experimentStartTime, ms, peers, mm };

  // A clock correction can leave a few rounds out of order. Windows are found by binary search, so
  // put them in order once here.
  const order = Uint32Array.from({ length: rounds }, (_, i) => i).sort((a, b) => ms[a]! - ms[b]! || a - b);
  return {
    start: result.experimentStartTime,
    ms: Uint32Array.from(order, (i) => ms[i]!),
    peers,
    mm: mm.map((values) => Uint16Array.from(order, (i) => values[i]!)),
  };
}

/**
 * Distances to one peer between two instants, in mm, broken where rounds to it stop.
 *
 * Returns every round when there are few enough, and otherwise thins to about `buckets` slices of time
 * keeping each slice's nearest and furthest. An overview of a week and an exact view of thirty
 * seconds come from the same call.
 */
export function rangeWindow(store: RangeStore, peer: number, from: number, to: number, buckets: number): Series {
  const index = store.peers.indexOf(peer);
  if (index < 0 || !(to >= from)) return { t: new Float64Array(0), v: new Float32Array(0) };
  const values = store.mm[index]!;
  const startMs = (from - store.start) * 1000;
  const endMs = (to - store.start) * 1000;
  let low = 0;
  let high = store.ms.length;
  while (low < high) {
    const middle = (low + high) >>> 1;
    if (store.ms[middle]! < startMs) low = middle + 1;
    else high = middle;
  }
  // The ranged round just before the window, if it is within a break's length (two rounds a second), so
  // the line runs to the window's edge.
  let first = low;
  for (let row = low - 1; row >= 0 && row >= low - 2 * ROUND_GAP_S; row -= 1) {
    if (values[row] !== NO_RANGE) {
      first = row;
      break;
    }
  }
  const outT: number[] = [];
  const outV: number[] = [];
  let previous = Number.NaN;
  for (let row = first; row < store.ms.length; row += 1) {
    const value = values[row]!;
    if (value === NO_RANGE) continue;
    const at = store.ms[row]!;
    const t = store.start + at / 1000;
    if (!Number.isNaN(previous) && t - previous > ROUND_GAP_S) {
      outT.push(previous + ROUND_GAP_S / 2);
      outV.push(Number.NaN);
    }
    outT.push(t);
    outV.push(value);
    previous = t;
    // The first ranged round past the window is kept, for the same reason, and ends the walk.
    if (at > endMs) break;
  }
  return thinSeries({ t: Float64Array.from(outT), v: Float32Array.from(outV) }, from, to, buckets);
}

/** One peer, or a combination of them, on one day. */
export interface DayFigures {
  /** Rounds with a distance at or under the cutoff. */
  readonly rounds: number;
  /** Of those, rounds at or under the touching distance. */
  readonly touchingRounds: number;
  /** Rounds divided by the rounds in a minute. */
  readonly minutesInRange: number;
  readonly minutesTouching: number;
  /** Mean of the distances counted, mm; null when there were none. */
  readonly meanMm: number | null;
}

export interface DailyRow {
  /** The study's local date, `YYYY-MM-DD`. */
  readonly date: string;
  /** Unix seconds that local day began. */
  readonly dayStart: number;
  /** In the order the peers were asked for. */
  readonly peers: readonly DayFigures[];
  /** Rounds in range of at least one of the peers asked for; null with only one peer. */
  readonly any: DayFigures | null;
  /** Rounds in range of every one of them; null with only one peer. */
  readonly all: DayFigures | null;
}

export interface DailyOptions {
  /** The study's IANA zone: days run from its midnight to its midnight. */
  readonly timezone: string;
  readonly peers: readonly number[];
  /** A distance counts as "touching" at or under this, mm. */
  readonly touchingMm: number;
  /** Distances above this are not counted at all, mm. */
  readonly cutoffMm: number;
}

class Tally {
  rounds = 0;
  touching = 0;
  sum = 0;
  count = 0;
  figures(): DayFigures {
    return {
      rounds: this.rounds,
      touchingRounds: this.touching,
      minutesInRange: this.rounds / ROUNDS_PER_MINUTE,
      minutesTouching: this.touching / ROUNDS_PER_MINUTE,
      meanMm: this.count ? this.sum / this.count : null,
    };
  }
}

/**
 * Time near each chosen peer on each day of the study, on the study's own calendar.
 *
 * Counted in ranging rounds, which run ROUNDS_PER_MINUTE to the minute, and converted to minutes only at
 * the end, so a day with no rounds is zero minutes and a part-minute is reported as the part it was.
 * With several peers, "any" counts a round in which at least one of them was in range, and "all" one in
 * which every one was; each mean is over every distance its rounds counted.
 */
export function dailyRangingStats(store: RangeStore, options: DailyOptions): DailyRow[] {
  const { timezone, touchingMm, cutoffMm } = options;
  const columns = options.peers.map((peer) => {
    const index = store.peers.indexOf(peer);
    return index >= 0 ? store.mm[index]! : null;
  });
  if (!store.ms.length) return [];
  const firstT = store.start + store.ms[0]! / 1000;
  const lastT = store.start + store.ms[store.ms.length - 1]! / 1000;
  const clock = localClock(timezone, firstT, lastT);
  const firstDay = clock.dayNumber(firstT);
  const lastDay = clock.dayNumber(lastT);
  const several = options.peers.length > 1;
  const days = Array.from({ length: lastDay - firstDay + 1 }, () => ({
    peers: options.peers.map(() => new Tally()),
    any: new Tally(),
    all: new Tally(),
  }));

  // Local midnights are found once and the rounds walked in order against them.
  let day = firstDay;
  let nextMidnight = localDayStart(day + 1, timezone);
  for (let row = 0; row < store.ms.length; row += 1) {
    const t = store.start + store.ms[row]! / 1000;
    while (t >= nextMidnight && day < lastDay) {
      day += 1;
      nextMidnight = localDayStart(day + 1, timezone);
    }
    const tallies = days[day - firstDay]!;
    let inRange = 0;
    let touching = 0;
    let sum = 0;
    for (let p = 0; p < columns.length; p += 1) {
      const value = columns[p]?.[row] ?? NO_RANGE;
      if (value === NO_RANGE || value > cutoffMm) continue;
      const tally = tallies.peers[p]!;
      tally.rounds += 1;
      tally.sum += value;
      tally.count += 1;
      inRange += 1;
      sum += value;
      if (value <= touchingMm) {
        tally.touching += 1;
        touching += 1;
      }
    }
    if (!several || inRange === 0) continue;
    tallies.any.rounds += 1;
    tallies.any.sum += sum;
    tallies.any.count += inRange;
    if (touching > 0) tallies.any.touching += 1;
    if (inRange === columns.length) {
      tallies.all.rounds += 1;
      tallies.all.sum += sum;
      tallies.all.count += inRange;
      if (touching === columns.length) tallies.all.touching += 1;
    }
  }

  return days.map((tallies, offset) => ({
    date: dayNumberToDate(firstDay + offset),
    dayStart: localDayStart(firstDay + offset, timezone),
    peers: tallies.peers.map((tally) => tally.figures()),
    any: several ? tallies.any.figures() : null,
    all: several ? tallies.all.figures() : null,
  }));
}

/** Unix seconds of the first and last round, or null for a log that never ranged. */
export function rangeStoreSpan(store: RangeStore): [number, number] | null {
  if (!store.ms.length) return null;
  return [store.start + store.ms[0]! / 1000, store.start + store.ms[store.ms.length - 1]! / 1000];
}

