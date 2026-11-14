// One log reduced to what its charts draw. Pure.
//
// Runs in the worker beside the parser, over the records the page never sees. A worst-case week is
// about 1.2 million ranging rounds, as many Bluetooth scans, and the parsed records take most of a
// gigabyte; what leaves here is a few thousand points per chart, in typed arrays that move to the page
// without being copied. Each reducer below says what it keeps and why.

import { BATTERY_EVENT, TIME_ALIGNED_INTERVAL_S } from './constants.ts';
import type { LogRecord, ParseResult } from './log.ts';
import { breakAtGaps, EMPTY_SERIES, seriesFrom, thinSeries, type Series } from './series.ts';

/**
 * Silence longer than this means the tag was not recording.
 *
 * A recording tag writes its heartbeat — battery, clock anchor, diagnostics — every TIME_ALIGNED_INTERVAL_S
 * whatever else it is doing, so two heartbeats missed in a row, with a minute to spare, is a tag that was
 * asleep outside its daily hours, on its charger, or off.
 */
export const RECORDING_GAP_S = 2 * TIME_ALIGNED_INTERVAL_S + 60;

/** A line through heartbeat readings stops where more than two and a half heartbeats are missing. */
export const HEARTBEAT_GAP_S = 2.5 * TIME_ALIGNED_INTERVAL_S;

/** Movement readings arrive about once a second while the tag moves; a longer pause is a break. */
export const IMU_GAP_S = 5;

/** About how many slices of time a whole-log series is reduced to before it leaves the worker. */
export const CHART_BUCKETS = 3000;

/** A shaded stretch is merged with the next when they are closer than this, past MAX_SPANS of them. */
const MAX_SPANS = 2000;

export interface LogCharts {
  /** Unix seconds the deployment started; every record's time is measured from it. */
  readonly start: number;
  /** First and last record, Unix seconds. */
  readonly first: number | null;
  readonly last: number | null;
  /** Battery, in volts. */
  readonly voltage: Series;
  /** Time on the charger, from the tag's own plugged and unplugged records: [from, to, from, to, …]. */
  readonly charging: Float64Array;
  /** When the tag reported its battery critically low. */
  readonly lowBattery: Float64Array;
  /** 1 while moving and 0 while still, held until the next change. */
  readonly motion: Series;
  /** Distinct tags heard in Bluetooth scans, per minute, held for the minute. */
  readonly bleScan: Series;
  /** Accelerometer, three axes, thinned. */
  readonly imu: { readonly x: Series; readonly y: Series; readonly z: Series };
  /** Chip temperature, °C, from the diagnostics records. */
  readonly temperature: Series;
  /** Stretches the tag was recording: [from, to, from, to, …]. */
  readonly recording: Float64Array;
  /** Ranging rounds that measured at least one other tag, per minute: the minute's start, and the count. */
  readonly rangedPerMinute: Series;
}

/** Unix seconds of a record. */
const unix = (start: number, record: LogRecord) => start + record.ms / 1000;

/**
 * Stretches of continuous recording: [from, to, …], in time order, merged where they touch.
 *
 * Linear and tolerant of the small backward steps a log has at a clock correction, because records
 * are taken in the order they were written rather than sorted first.
 */
export function recordingSegments(records: readonly LogRecord[], start: number, gapSeconds = RECORDING_GAP_S): Float64Array {
  const segments: Array<[number, number]> = [];
  let from = Number.NaN;
  let to = Number.NaN;
  for (const record of records) {
    const t = unix(start, record);
    if (Number.isNaN(from)) {
      from = to = t;
    } else if (t >= from - gapSeconds && t <= to + gapSeconds) {
      if (t < from) from = t;
      if (t > to) to = t;
    } else {
      segments.push([from, to]);
      from = to = t;
    }
  }
  if (!Number.isNaN(from)) segments.push([from, to]);
  segments.sort((a, b) => a[0] - b[0]);
  const merged: Array<[number, number]> = [];
  for (const segment of segments) {
    const previous = merged[merged.length - 1];
    if (previous && segment[0] <= previous[1] + gapSeconds) previous[1] = Math.max(previous[1], segment[1]);
    else merged.push([...segment]);
  }
  return Float64Array.from(merged.flat());
}

/** Which recording segment (index into pairs) holds `t`, or -1. Segments are in time order. */
function segmentOf(segments: Float64Array, t: number): number {
  let low = 0;
  let high = segments.length / 2 - 1;
  while (low <= high) {
    const middle = (low + high) >>> 1;
    if (t < segments[2 * middle]!) high = middle - 1;
    else if (t > segments[2 * middle + 1]!) low = middle + 1;
    else return middle;
  }
  return -1;
}

/** Merge stretches closer together than needed to keep at most `max` of them. */
export function capSpans(pairs: readonly number[], max = MAX_SPANS): Float64Array {
  let spans: Array<[number, number]> = [];
  for (let i = 0; i + 1 < pairs.length; i += 2) spans.push([pairs[i]!, pairs[i + 1]!]);
  spans.sort((a, b) => a[0] - b[0]);
  let join = 0;
  while (spans.length > max) {
    join = join ? join * 2 : 1;
    const merged: Array<[number, number]> = [];
    for (const span of spans) {
      const previous = merged[merged.length - 1];
      if (previous && span[0] - previous[1] <= join) previous[1] = Math.max(previous[1], span[1]);
      else merged.push([...span]);
    }
    spans = merged;
  }
  return Float64Array.from(spans.flat());
}

/**
 * Time on the charger, from the tag's own records rather than guessed from the battery curve.
 *
 * Plugged starts a stretch and Unplugged ends it. Every boot writes the charger's state as one of its
 * first records, so a stretch that began before a restart is picked up again after it, and one still
 * open when the log ends runs to the last record.
 */
export function chargingSpans(records: readonly LogRecord[], start: number, last: number | null): { spans: Float64Array; lowBattery: Float64Array } {
  const pairs: number[] = [];
  const low: number[] = [];
  let since: number | null = null;
  for (const record of records) {
    if (record.kind !== 'charging') continue;
    const t = unix(start, record);
    if (record.event === BATTERY_EVENT.BATTERY_PLUGGED) {
      since ??= t;
    } else if (record.event === BATTERY_EVENT.BATTERY_UNPLUGGED) {
      if (since !== null) pairs.push(since, Math.max(since, t));
      since = null;
    } else if (record.event === BATTERY_EVENT.BATTERY_CRITICAL_VOLTAGE) {
      low.push(t);
    }
  }
  if (since !== null) pairs.push(since, Math.max(since, last ?? since));
  return { spans: capSpans(pairs), lowBattery: Float64Array.from(low) };
}

/**
 * Moving (1) or still (0), held from each change to the next, stopping where recording stopped.
 *
 * A state is only known while the tag is recording, so it is ended at the end of the stretch it was
 * recorded in rather than carried across a night on the charger.
 */
export function motionSteps(records: readonly LogRecord[], start: number, recording: Float64Array): Series {
  const changeTimes: number[] = [];
  const changeValues: number[] = [];
  for (const record of records) {
    if (record.kind !== 'motion') continue;
    changeTimes.push(unix(start, record));
    changeValues.push(record.inMotion ? 1 : 0);
  }
  const changes = seriesFrom(changeTimes, changeValues);
  const times: number[] = [];
  const values: number[] = [];
  let heldSegment = -2;
  for (let i = 0; i < changes.t.length; i += 1) {
    const t = changes.t[i]!;
    const value = changes.v[i]!;
    const segment = segmentOf(recording, t);
    // A repeat of the state already held, in the same stretch of recording, adds nothing to a step line.
    if (segment !== heldSegment || values[values.length - 1] !== value) {
      times.push(t);
      values.push(value);
      heldSegment = segment;
    }
    const next = changes.t[i + 1];
    if (segment >= 0 && (next === undefined || segmentOf(recording, next) !== segment)) {
      times.push(recording[2 * segment + 1]!);
      values.push(Number.NaN);
      heldSegment = -2;
    }
  }
  return { t: Float64Array.from(times), v: Float32Array.from(values) };
}

/**
 * Distinct tags heard in Bluetooth scans, per minute of the deployment, held for that minute.
 *
 * A tag scans twice a second, so a minute's scans are pooled: the count is how many different tags it
 * heard at all during the minute. A minute with no scan at all is a break, not a zero.
 */
export function bleScanPerMinute(records: readonly LogRecord[], start: number): Series {
  const minutes = new Map<number, Set<number>>();
  for (const record of records) {
    if (record.kind !== 'bleScan') continue;
    const minute = Math.floor(record.ms / 60_000);
    let heard = minutes.get(minute);
    if (!heard) minutes.set(minute, (heard = new Set()));
    for (const peer of record.peers) heard.add(peer);
  }
  const keys = [...minutes.keys()].sort((a, b) => a - b);
  const times: number[] = [];
  const values: number[] = [];
  keys.forEach((minute, i) => {
    times.push(start + minute * 60);
    values.push(minutes.get(minute)!.size);
    if (keys[i + 1] !== minute + 1) {
      times.push(start + (minute + 1) * 60);
      values.push(Number.NaN);
    }
  });
  return { t: Float64Array.from(times), v: Float32Array.from(values) };
}

/**
 * Ranging rounds that measured at least one other tag, per minute of the deployment.
 *
 * Not the same as rounds logged: a round can be logged with nobody in it. Only minutes with at least
 * one such round are listed; this is a count to look up, not a line to draw.
 */
export function rangedRoundsPerMinute(records: readonly LogRecord[], start: number): Series {
  const minutes = new Map<number, number>();
  for (const record of records) {
    if (record.kind !== 'ranges' || record.ranges.size === 0) continue;
    const minute = Math.floor(record.ms / 60_000);
    minutes.set(minute, (minutes.get(minute) ?? 0) + 1);
  }
  const keys = [...minutes.keys()].sort((a, b) => a - b);
  return { t: Float64Array.from(keys, (minute) => start + minute * 60), v: Float32Array.from(keys, (minute) => minutes.get(minute)!) };
}

/** Battery readings in volts, broken where heartbeats are missing. */
export function voltageSeries(records: readonly LogRecord[], start: number): Series {
  const times: number[] = [];
  const values: number[] = [];
  for (const record of records) {
    if (record.kind !== 'voltage') continue;
    times.push(unix(start, record));
    values.push(record.millivolts / 1000);
  }
  return breakAtGaps(seriesFrom(times, values), HEARTBEAT_GAP_S);
}

/** Chip temperature in °C from the diagnostics records, broken where heartbeats are missing. */
export function temperatureSeries(records: readonly LogRecord[], start: number): Series {
  const times: number[] = [];
  const values: number[] = [];
  for (const record of records) {
    if (record.kind !== 'diagnostics' || record.temperatureC === null) continue;
    times.push(unix(start, record));
    values.push(record.temperatureC);
  }
  return breakAtGaps(seriesFrom(times, values), HEARTBEAT_GAP_S);
}

/** Accelerometer readings, one series per axis, thinned to `buckets` slices of the whole log. */
export function imuSeries(records: readonly LogRecord[], start: number, buckets = CHART_BUCKETS): LogCharts['imu'] {
  const times: number[] = [];
  const axes: [number[], number[], number[]] = [[], [], []];
  for (const record of records) {
    if (record.kind !== 'imu') continue;
    times.push(unix(start, record));
    axes[0].push(record.accel[0]);
    axes[1].push(record.accel[1]);
    axes[2].push(record.accel[2]);
  }
  if (!times.length) return { x: EMPTY_SERIES, y: EMPTY_SERIES, z: EMPTY_SERIES };
  const [x, y, z] = axes.map((values) => {
    const series = breakAtGaps(seriesFrom(times, values), IMU_GAP_S);
    return thinSeries(series, series.t[0]!, series.t[series.t.length - 1]!, buckets);
  }) as [Series, Series, Series];
  return { x, y, z };
}

/** Thin a series to `buckets` slices of its own span, for the trip out of the worker. */
const thinWhole = (series: Series, buckets = CHART_BUCKETS): Series =>
  series.t.length ? thinSeries(series, series.t[0]!, series.t[series.t.length - 1]!, buckets) : series;

/** Everything one log's charts need, from one parsed log. */
export function buildLogCharts(result: ParseResult): LogCharts {
  const { records, experimentStartTime: start } = result;
  let first: number | null = null;
  let last: number | null = null;
  for (const record of records) {
    const t = unix(start, record);
    if (first === null || t < first) first = t;
    if (last === null || t > last) last = t;
  }
  const recording = recordingSegments(records, start);
  const charging = chargingSpans(records, start, last);
  return {
    start,
    first,
    last,
    voltage: thinWhole(voltageSeries(records, start)),
    charging: charging.spans,
    lowBattery: charging.lowBattery,
    motion: thinWhole(motionSteps(records, start, recording)),
    bleScan: thinWhole(bleScanPerMinute(records, start)),
    imu: imuSeries(records, start),
    temperature: thinWhole(temperatureSeries(records, start)),
    recording,
    rangedPerMinute: rangedRoundsPerMinute(records, start),
  };
}

/** Every typed array in a LogCharts, for handing to postMessage's transfer list. */
export function chartBuffers(charts: LogCharts): ArrayBuffer[] {
  const series = [charts.voltage, charts.motion, charts.bleScan, charts.imu.x, charts.imu.y, charts.imu.z, charts.temperature, charts.rangedPerMinute];
  const arrays = [charts.charging, charts.lowBattery, charts.recording, ...series.flatMap((s) => [s.t, s.v])];
  // An empty series shares one buffer; a buffer can only be listed once, and a shared one not at all.
  const buffers = new Set<ArrayBuffer>();
  for (const array of arrays) if (array.byteLength) buffers.add(array.buffer as ArrayBuffer);
  return [...buffers];
}
