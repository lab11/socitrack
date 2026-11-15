import { useEffect, useMemo, useState } from 'react';
import type { EventLog } from '@tottag/schema';
import { CHART_KINDS, LINE_COLORS } from '../components/chartKinds.ts';
import { StudyTools } from '../components/StudyTools.tsx';
import { TimeSeriesChart, type ChartLine } from '../components/TimeSeriesChart.tsx';
import {
  clockMarkers, cutoffIn, lowBatteryMarkers, noteMarkers, rangeMinuteSeries, restartMarkers, spansOf, type StudySettings,
} from '../features/chartData.ts';
import type { Deployment } from '../features/studyClock.ts';
import { logName, tagRef } from '../features/tagNames.ts';
import { MM_PER, UNIT_WORDS } from '../features/units.ts';

interface Props {
  readonly deployment: Deployment;
  readonly settings: StudySettings;
  readonly onSettings: (settings: StudySettings) => void;
  readonly notes: EventLog | null;
}

/**
 * One tag's log as charts: battery and charging, distance to another tag, movement, and the tags it
 * heard. Every chart shares one time axis and one zoom, so a moment picked out on one is the same moment
 * on all of them.
 */
export function TagCharts({ deployment, settings, onSettings, notes }: Props) {
  const logs = deployment.reports.filter((report) => report.charts && report.health);
  const [chosen, setChosen] = useState<string | null>(null);
  const report = logs.find((entry) => entry.id === chosen) ?? logs[0];
  const [view, setView] = useState<[number, number] | null>(null);
  useEffect(() => setView(null), [report?.id]);

  const peers = useMemo(() => {
    if (!report?.radio) return [];
    return [...report.radio.peers].sort((a, b) => b.bins.length - a.bins.length).map((peer) => tagRef(peer.uid, deployment.scheduled));
  }, [report, deployment.scheduled]);
  const [peerChoice, setPeerChoice] = useState<number | null>(null);
  const peer = peers.find((entry) => entry.uid === peerChoice) ?? peers[0];

  const charts = report?.charts;
  const health = report?.health;
  const shared = useMemo(() => {
    if (!charts || !health) return null;
    return {
      markers: [
        ...restartMarkers(health, charts.start), ...clockMarkers(health, charts.start),
        ...noteMarkers(notes?.events ?? [], settings.offsetSeconds),
      ],
      charging: spansOf(charts.charging, 'charging'),
    };
  }, [charts, health, notes, settings.offsetSeconds]);
  const distance = useMemo<ChartLine[]>(() => (report?.radio && peer ? [{
    id: 'distance', label: peer.display, color: 'var(--primary)',
    data: rangeMinuteSeries(report.radio, peer.uid, settings.cutoffMm / MM_PER[settings.unit], settings.unit),
  }] : []), [report, peer, settings.cutoffMm, settings.unit]);

  if (!report || !charts || !health || !shared || charts.first === null || charts.last === null) {
    return <p className="empty">None of this deployment's logs has anything to chart.</p>;
  }
  const extent: [number, number] = [charts.first, charts.last];
  const zone = deployment.timezone;
  const common = { extent, view, onViewChange: setView, timezone: zone, kinds: CHART_KINDS };
  const unit = UNIT_WORDS[settings.unit];
  const notesHere = shared.markers.filter((marker) => marker.kind === 'note');

  return (
    <div className="stack">
      <div className="panel">
        <div className="chooser">
          <span className="tools__label">Tag</span>
          <div className="chips" role="radiogroup" aria-label="Tag">
            {logs.map((entry) => (
              <button key={entry.id} type="button" role="radio" aria-checked={entry.id === report.id}
                className={`chip chip__button${entry.id === report.id ? ' chip--on' : ''}`} onClick={() => setChosen(entry.id)}>
                {logName(entry, deployment.scheduled)}
              </button>
            ))}
          </div>
        </div>
        <StudyTools settings={settings} onChange={onSettings} notes={notes} />
      </div>

      <div className="panel charts">
        <TimeSeriesChart {...common} title="Battery (volts)"
          lines={[{ id: 'battery', label: 'Battery', color: 'var(--primary)', data: charts.voltage }]}
          spans={shared.charging} markers={[...shared.markers, ...lowBatteryMarkers(charts)]}
          formatValue={(volts) => `${volts.toFixed(2)} V`} />

        <div className="chart-choice">
          <label className="inline-field">
            Distance to
            <select className="input input--select" value={peer?.uid ?? ''} onChange={(event) => setPeerChoice(Number(event.target.value))}
              disabled={!peers.length}>
              {peers.map((entry) => <option key={entry.uid} value={entry.uid}>{entry.display}</option>)}
            </select>
          </label>
        </div>
        {peer ? (
          <TimeSeriesChart {...common} title={`Distance to ${peer.display} (${unit.long}), the middle value of each minute`}
            lines={distance} markers={notesHere} yDomain={[0, cutoffIn(settings)]}
            formatValue={(value) => `${value.toFixed(1)} ${unit.short}`}
            footnote={`Distances over ${cutoffIn(settings)} ${unit.short} are left out.`} />
        ) : (
          <p className="empty">This tag never measured a distance to another tag.</p>
        )}

        <TimeSeriesChart {...common} title="Moving or still" height={130}
          lines={[{ id: 'motion', label: 'Moving', color: 'var(--ok)', data: charts.motion, step: true, fill: true }]}
          markers={notesHere} yDomain={[0, 1]} yLabels={[{ value: 0, label: 'Still' }, { value: 1, label: 'Moving' }]}
          formatValue={(value) => (value >= 0.5 ? 'Moving' : 'Still')} />

        <TimeSeriesChart {...common} title="Tags heard over Bluetooth, each minute" height={150}
          lines={[{ id: 'heard', label: 'Tags heard', color: 'var(--note)', data: charts.bleScan, step: true, fill: true }]}
          markers={notesHere}
          formatValue={(value) => `${Math.round(value)} tag${Math.round(value) === 1 ? '' : 's'}`} />

        {charts.imu.x.t.length > 0 ? (
          <TimeSeriesChart {...common} title="Movement (accelerometer)"
            lines={(['x', 'y', 'z'] as const).map((axis, i) => ({
              id: axis, label: `${axis.toUpperCase()} axis`, color: LINE_COLORS[i]!, data: charts.imu[axis],
            }))}
            markers={notesHere} formatValue={(value) => Math.round(value).toLocaleString()}
            footnote="Raw accelerometer readings, recorded while the tag moves." />
        ) : (
          <p className="empty">This tag recorded no accelerometer readings.</p>
        )}
      </div>
    </div>
  );
}
