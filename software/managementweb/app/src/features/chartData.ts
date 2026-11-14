// What the review page's charts draw, assembled from what the log worker sent. Pure.

import type { DeploymentHealth, LogCharts, NotedEvent, RadioSummary, Series } from '@tottag/schema';
import { explainRestart } from './plainLanguage.ts';
import { DEFAULT_CUTOFF_FT, DEFAULT_TOUCHING_FT, toMm, toUnit, type DistanceUnit } from './units.ts';

/** Settings shared by every view of one deployment, so a cutoff chosen once applies everywhere. */
export interface StudySettings {
  readonly unit: DistanceUnit;
  /**
   * Distances beyond this are left out, and at or under the other count as touching distance. Kept in
   * millimeters, so switching between feet and meters and back returns exactly what was typed.
   */
  readonly cutoffMm: number;
  readonly touchingMm: number;
  /** A researcher's own notes of what happened when, as the file's text. */
  readonly notes: { readonly fileName: string; readonly text: string } | null;
  /** Seconds to move every note by, for a notes clock that ran early or late. */
  readonly offsetSeconds: number;
}

export const DEFAULT_SETTINGS: StudySettings = {
  unit: 'ft', cutoffMm: toMm(DEFAULT_CUTOFF_FT, 'ft'), touchingMm: toMm(DEFAULT_TOUCHING_FT, 'ft'), notes: null, offsetSeconds: 0,
};

/** A distance setting as shown, in the unit chosen, to a tenth. */
const shown = (mm: number, unit: DistanceUnit) => Math.round(toUnit(mm, unit) * 10) / 10;
export const cutoffIn = (settings: StudySettings) => shown(settings.cutoffMm, settings.unit);
export const touchingIn = (settings: StudySettings) => shown(settings.touchingMm, settings.unit);

/** A moment marked on a chart. Kinds are styled by the chart's caller. */
export interface Marker {
  readonly t: number;
  readonly kind: 'restart' | 'restart-warn' | 'clock' | 'note' | 'low-battery';
  readonly label: string;
}

/** Each restart, at the time it happened, named by what caused it. */
export function restartMarkers(health: DeploymentHealth, start: number): Marker[] {
  return health.reboots.map((reboot) => {
    const story = explainRestart(reboot);
    return { t: start + reboot.ms / 1000, kind: story.tone === 'ok' ? 'restart' : 'restart-warn', label: story.title };
  });
}

/** Each correction of the tag's clock, where it happened. */
export function clockMarkers(health: DeploymentHealth, start: number): Marker[] {
  return health.clock.offsetSteps.map((step) => ({
    t: start + step.ms / 1000,
    kind: 'clock',
    label: `Clock corrected by ${(Math.abs(step.stepMs) / 1000).toFixed(1)} s`,
  }));
}

/** Each noted event, moved by the offset chosen. */
export function noteMarkers(events: readonly NotedEvent[], offsetSeconds: number): Marker[] {
  return events.map((event) => ({ t: event.t + offsetSeconds, kind: 'note', label: event.message || 'Noted event' }));
}

export function lowBatteryMarkers(charts: LogCharts): Marker[] {
  return [...charts.lowBattery].map((t) => ({ t, kind: 'low-battery', label: 'Battery critically low' }));
}

/** [from, to, from, to, …] as spans of one kind. */
export function spansOf(pairs: Float64Array, kind: string): Array<{ from: number; to: number; kind: string }> {
  const spans: Array<{ from: number; to: number; kind: string }> = [];
  for (let i = 0; i + 1 < pairs.length; i += 2) spans.push({ from: pairs[i]!, to: pairs[i + 1]!, kind });
  return spans;
}

/**
 * The median distance to one peer in each minute, in the unit chosen, from the radio summary the worker
 * already made. A minute with no rounds, or whose median is beyond the cutoff, is a break.
 */
export function rangeMinuteSeries(radio: RadioSummary, peer: number, cutoff: number, unit: DistanceUnit): Series {
  const bins = radio.peers.find((entry) => entry.uid === peer)?.bins ?? [];
  const start = radio.experimentStartTime ?? 0;
  const binSeconds = radio.binMs / 1000;
  const sorted = [...bins].sort((a, b) => a[0] - b[0]);
  const times: number[] = [];
  const values: number[] = [];
  sorted.forEach(([bin, , medianMm], i) => {
    const value = toUnit(medianMm, unit);
    const t = start + (bin + 0.5) * binSeconds;
    if (value > cutoff) {
      if (values.length && !Number.isNaN(values[values.length - 1]!)) {
        times.push(t);
        values.push(Number.NaN);
      }
      return;
    }
    times.push(t);
    values.push(value);
    const next = sorted[i + 1];
    if (!next || next[0] !== bin + 1) {
      times.push(start + (bin + 1) * binSeconds);
      values.push(Number.NaN);
    }
  });
  return { t: Float64Array.from(times), v: Float32Array.from(values) };
}

/** A series with every value multiplied, for a change of unit. Breaks stay breaks. */
export function scaleSeries(series: Series, factor: number): Series {
  return { t: series.t, v: series.v.map((value) => value * factor) };
}
