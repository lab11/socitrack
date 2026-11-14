import { useEffect, useMemo, useState } from 'react';
import type { EventLog, Series } from '@tottag/schema';
import { CHART_KINDS, LINE_COLORS } from '../components/chartKinds.ts';
import { StudyTools } from '../components/StudyTools.tsx';
import { TimeSeriesChart, type ChartLine } from '../components/TimeSeriesChart.tsx';
import { cutoffIn, noteMarkers, scaleSeries, type StudySettings } from '../features/chartData.ts';
import type { DeviceReport } from '../features/deployment.ts';
import type { Deployment } from '../features/studyClock.ts';
import { logTag, type TagRef } from '../features/tagNames.ts';
import { formatInstant } from '../features/timeAxis.ts';
import { MM_PER, UNIT_WORDS } from '../features/units.ts';
import { requestRangeWindow } from '../lib/parseAsync.ts';
import { useLatest } from '../lib/useLatest.ts';

interface Props {
  readonly deployment: Deployment;
  readonly settings: StudySettings;
  readonly onSettings: (settings: StudySettings) => void;
  readonly notes: EventLog | null;
}

/** Window lengths offered, the same as the desktop tool's. */
const WINDOWS = [10, 15, 30, 60, 300, 600, 900, 1800, 3600] as const;
const windowWords = (seconds: number) =>
  seconds < 60 ? `${seconds} seconds` : seconds < 3600 ? `${seconds / 60} minute${seconds === 60 ? '' : 's'}` : '1 hour';

/** Slices of time for the overview: enough to show every approach at a typical width. */
const OVERVIEW_BUCKETS = 1500;
/** A one-hour window is 7,200 rounds a line; this keeps it every round up to half an hour. */
const WINDOW_BUCKETS = 4000;

interface Candidate {
  readonly report: DeviceReport;
  readonly tag: TagRef;
}

/** Leave out distances over the cutoff, in the unit chosen. A left-out distance is a break. */
function inUnits(series: Series | null | undefined, settings: StudySettings): Series | null {
  if (!series) return null;
  const scaled = scaleSeries(series, 1 / MM_PER[settings.unit]);
  const cutoff = settings.cutoffMm / MM_PER[settings.unit];
  return { t: scaled.t, v: scaled.v.map((value) => (value > cutoff ? Number.NaN : value)) };
}

/**
 * Two tags' measurements of each other, side by side, every round.
 *
 * A distance between two tags is measured once and logged at both ends, so the two lines should lie on
 * top of each other; where one has a gap the other fills, or they disagree, is worth seeing. The
 * overview shows the whole deployment; the window below shows a stretch of it at full resolution.
 */
export function CompareTags({ deployment, settings, onSettings, notes }: Props) {
  const candidates = useMemo<Candidate[]>(() => deployment.reports.flatMap((report) => {
    const tag = logTag(report, deployment.scheduled);
    return tag && report.rangeSpan ? [{ report, tag }] : [];
  }), [deployment]);
  const [first, setFirst] = useState<string | null>(null);
  const [second, setSecond] = useState<string | null>(null);
  const a = candidates.find((c) => c.report.id === first) ?? candidates[0];
  const b = candidates.find((c) => c.report.id === second && c !== a) ?? candidates.find((c) => c !== a);

  const span = useMemo<[number, number] | null>(() => {
    if (!a || !b) return null;
    // Where both were recording: outside it one line is missing by definition.
    const from = Math.max(a.report.rangeSpan![0], b.report.rangeSpan![0]);
    const to = Math.min(a.report.rangeSpan![1], b.report.rangeSpan![1]);
    return to > from ? [from, to] : [Math.min(a.report.rangeSpan![0], b.report.rangeSpan![0]), Math.max(a.report.rangeSpan![1], b.report.rangeSpan![1])];
  }, [a, b]);

  const [size, setSize] = useState<number>(30);
  const [start, setStart] = useState<number | null>(null);
  useEffect(() => setStart(null), [a?.report.id, b?.report.id]);
  const windowFrom = span ? Math.max(span[0], Math.min(start ?? span[0], span[1] - size)) : 0;
  const windowTo = windowFrom + size;

  const overviewA = useLatest(() => (a && b && span ? requestRangeWindow(a.report.id, b.tag.uid, span[0], span[1], OVERVIEW_BUCKETS) : null), [a?.report.id, b?.tag.uid, span?.[0], span?.[1]]);
  const overviewB = useLatest(() => (a && b && span ? requestRangeWindow(b.report.id, a.tag.uid, span[0], span[1], OVERVIEW_BUCKETS) : null), [b?.report.id, a?.tag.uid, span?.[0], span?.[1]]);
  const closeA = useLatest(() => (a && b && span ? requestRangeWindow(a.report.id, b.tag.uid, windowFrom, windowTo, WINDOW_BUCKETS) : null), [a?.report.id, b?.tag.uid, windowFrom, windowTo]);
  const closeB = useLatest(() => (a && b && span ? requestRangeWindow(b.report.id, a.tag.uid, windowFrom, windowTo, WINDOW_BUCKETS) : null), [b?.report.id, a?.tag.uid, windowFrom, windowTo]);

  if (candidates.length < 2 || !a || !b || !span) {
    return (
      <p className="empty">
        Comparing needs the logs of two tags in this deployment that measured distances, and that each say which tag they came
        from. Open another tag's log to compare.
      </p>
    );
  }

  const unit = UNIT_WORDS[settings.unit];
  const lines = (fromA: Series | null, fromB: Series | null): ChartLine[] => [
    ...(fromA ? [{ id: 'ab', label: `${a.tag.display} measuring ${b.tag.display}`, color: LINE_COLORS[0], data: fromA }] : []),
    ...(fromB ? [{ id: 'ba', label: `${b.tag.display} measuring ${a.tag.display}`, color: LINE_COLORS[1], data: fromB }] : []),
  ];
  const markers = noteMarkers(notes?.events ?? [], settings.offsetSeconds);
  const missing = [overviewA, overviewB, closeA, closeB].some((result) => result.value === null && !result.loading && !result.error);
  const zone = deployment.timezone;
  const format = (value: number) => `${value.toFixed(1)} ${unit.short}`;
  const step = Math.max(1, Math.round(size / 10));

  const choose = (current: Candidate, set: (id: string) => void, label: string, exclude?: Candidate) => (
    <label className="inline-field">
      {label}
      <select className="input input--select" value={current.report.id} onChange={(event) => set(event.target.value)}>
        {candidates.filter((c) => c !== exclude).map((c) => <option key={c.report.id} value={c.report.id}>{c.tag.display}</option>)}
      </select>
    </label>
  );

  return (
    <div className="stack">
      <div className="panel">
        <div className="tools__group">
          {choose(a, (id) => { setFirst(id); if (id === b.report.id) setSecond(a.report.id); }, 'Compare')}
          {choose(b, setSecond, 'with', a)}
        </div>
        <StudyTools settings={settings} onChange={onSettings} notes={notes} />
      </div>

      {missing && (
        <p className="banner banner--warn">
          One of these logs' measurements was set aside to save memory. Close some logs, then open this one again.
        </p>
      )}

      <div className="panel charts">
        <TimeSeriesChart title={`The whole deployment (${unit.long}). Click to move the window there.`} fixed
          lines={lines(inUnits(overviewA.value, settings), inUnits(overviewB.value, settings))}
          extent={span} timezone={zone} kinds={CHART_KINDS} markers={markers} highlight={[windowFrom, windowTo]}
          yDomain={[0, cutoffIn(settings)]} formatValue={format} height={180}
          onPick={(t) => setStart(t - size / 2)}
          footnote={`Distances over ${cutoffIn(settings)} ${unit.short} are left out. Each slice of time shows its nearest and furthest distance.`} />

        <div className="tools__group window-controls">
          <label className="inline-field">
            Window
            <select className="input input--select" value={size} onChange={(event) => setSize(Number(event.target.value))}>
              {WINDOWS.map((seconds) => <option key={seconds} value={seconds}>{windowWords(seconds)}</option>)}
            </select>
          </label>
          <button type="button" className="button button--quiet button--small" onClick={() => setStart(windowFrom - size)} disabled={windowFrom <= span[0]}>
            Earlier
          </button>
          <input className="window-controls__slider" type="range" aria-label="Where the window starts"
            min={span[0]} max={Math.max(span[0], span[1] - size)} step={step} value={windowFrom}
            onChange={(event) => setStart(Number(event.target.value))} />
          <button type="button" className="button button--quiet button--small" onClick={() => setStart(windowFrom + size)} disabled={windowTo >= span[1]}>
            Later
          </button>
          <span className="tools__value">{formatInstant(windowFrom, zone, true)}</span>
        </div>

        <TimeSeriesChart title={`${windowWords(size)} from ${formatInstant(windowFrom, zone, true)}, every measurement`} fixed
          lines={lines(inUnits(closeA.value, settings), inUnits(closeB.value, settings))}
          extent={[windowFrom, windowTo]} timezone={zone} kinds={CHART_KINDS} markers={markers}
          yDomain={[0, cutoffIn(settings)]} formatValue={format} height={240}
          footnote="The tags measure each other twice a second; a gap is time they did not." />
      </div>
    </div>
  );
}
