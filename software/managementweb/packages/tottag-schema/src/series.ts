// Time series small enough to leave a worker, and thinning that keeps what matters in them.
//
// A week of ranging is over a million rounds; a chart is a few hundred pixels wide. Everything drawn
// is reduced first, in the worker beside the parser, to a series of at most a few thousand points, and
// thinned again for the pixels it lands on. Thinning keeps each slice of time's lowest and highest
// value rather than averaging them away, so a brief drop in battery or a moment two tags came close is
// still on the screen however far out the chart is zoomed.
//
// Times are Unix seconds. A NaN value is a break: the tag was not recording between its neighbors, and
// a line must not be drawn across that stretch as though it were.

export interface Series {
  /** Unix seconds, ascending. */
  readonly t: Float64Array;
  /** One value per time. NaN marks a break in the line. */
  readonly v: Float32Array;
}

export const EMPTY_SERIES: Series = { t: new Float64Array(0), v: new Float32Array(0) };

/** First index whose time is at or after `time`. */
export function lowerBound(times: Float64Array, time: number): number {
  let low = 0;
  let high = times.length;
  while (low < high) {
    const middle = (low + high) >>> 1;
    if (times[middle]! < time) low = middle + 1;
    else high = middle;
  }
  return low;
}

/** First index whose time is after `time`. */
export function upperBound(times: Float64Array, time: number): number {
  let low = 0;
  let high = times.length;
  while (low < high) {
    const middle = (low + high) >>> 1;
    if (times[middle]! <= time) low = middle + 1;
    else high = middle;
  }
  return low;
}

/** Build a series from parallel arrays, sorted by time. Stable, so equal times keep their order. */
export function seriesFrom(times: ArrayLike<number>, values: ArrayLike<number>): Series {
  const count = Math.min(times.length, values.length);
  let sorted = true;
  for (let i = 1; i < count; i += 1) if (times[i]! < times[i - 1]!) { sorted = false; break; }
  const order = Array.from({ length: count }, (_, i) => i);
  if (!sorted) order.sort((a, b) => times[a]! - times[b]! || a - b);
  const t = new Float64Array(count);
  const v = new Float32Array(count);
  order.forEach((from, to) => {
    t[to] = times[from]!;
    v[to] = values[from]!;
  });
  return { t, v };
}

/**
 * Insert a break wherever consecutive points are further apart than `maxGapSeconds`.
 *
 * The break sits just after the earlier point, which is where the data stopped.
 */
export function breakAtGaps(series: Series, maxGapSeconds: number): Series {
  const times: number[] = [];
  const values: number[] = [];
  for (let i = 0; i < series.t.length; i += 1) {
    if (i > 0 && series.t[i]! - series.t[i - 1]! > maxGapSeconds && !Number.isNaN(series.v[i - 1]!) && !Number.isNaN(series.v[i]!)) {
      times.push(series.t[i - 1]! + Math.min(maxGapSeconds, (series.t[i]! - series.t[i - 1]!) / 2));
      values.push(Number.NaN);
    }
    times.push(series.t[i]!);
    values.push(series.v[i]!);
  }
  return { t: Float64Array.from(times), v: Float32Array.from(values) };
}

/**
 * The part of a series between `from` and `to`, reduced to about `buckets` slices of time.
 *
 * Each slice keeps its first, lowest, highest and last point, in the order they occurred, so extremes
 * survive and the line still joins up from one slice to the next. Breaks are kept exactly. One point
 * either side of the window is kept too, so a line runs to the edge of the plot rather than stopping
 * short of it. A window that already holds few enough points comes back untouched.
 */
export function thinSeries(series: Series, from: number, to: number, buckets: number): Series {
  const { t, v } = series;
  const start = Math.max(0, lowerBound(t, from) - 1);
  const end = Math.min(t.length, upperBound(t, to) + 1);
  const slots = Math.max(1, Math.floor(buckets));
  if (end - start <= slots * 2 || !(to > from)) return { t: t.slice(start, end), v: v.slice(start, end) };

  const width = (to - from) / slots;
  const outT: number[] = [];
  const outV: number[] = [];
  let bucket = Number.NaN;
  let first = -1;
  let low = -1;
  let high = -1;
  let last = -1;
  const flush = () => {
    if (first < 0) return;
    const picks = [first, low, high, last].filter((index, position, all) => all.indexOf(index) === position).sort((a, b) => a - b);
    for (const index of picks) {
      outT.push(t[index]!);
      outV.push(v[index]!);
    }
    first = low = high = last = -1;
  };
  for (let i = start; i < end; i += 1) {
    const value = v[i]!;
    if (Number.isNaN(value)) {
      flush();
      if (outV.length && !Number.isNaN(outV[outV.length - 1]!)) {
        outT.push(t[i]!);
        outV.push(Number.NaN);
      }
      continue;
    }
    const slot = Math.floor((t[i]! - from) / width);
    if (slot !== bucket) {
      flush();
      bucket = slot;
    }
    if (first < 0) {
      first = low = high = last = i;
      continue;
    }
    if (value < v[low]!) low = i;
    if (value > v[high]!) high = i;
    last = i;
  }
  flush();
  return { t: Float64Array.from(outT), v: Float32Array.from(outV) };
}

/** Lowest and highest value present, ignoring breaks; null when there is none. */
export function seriesRange(series: Series): [number, number] | null {
  let low = Number.POSITIVE_INFINITY;
  let high = Number.NEGATIVE_INFINITY;
  for (const value of series.v) {
    if (Number.isNaN(value)) continue;
    if (value < low) low = value;
    if (value > high) high = value;
  }
  return low <= high ? [low, high] : null;
}
