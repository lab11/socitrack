import { useMemo, useState } from 'react';
import { coverageGrid, formatDateTime, formatSpan, ROUNDS_PER_MINUTE, type EventLog, type Series } from '@tottag/schema';
import { CHART_KINDS, LINE_COLORS } from '../components/chartKinds.ts';
import { CoverageHeatmap } from '../components/CoverageHeatmap.tsx';
import { DeploymentTimeline } from '../components/DeploymentTimeline.tsx';
import { TimeSeriesChart } from '../components/TimeSeriesChart.tsx';
import { noteMarkers, type StudySettings } from '../features/chartData.ts';
import { formatDuration } from '../features/deployment.ts';
import { deploymentStory, type TagStory } from '../features/deploymentStory.ts';
import type { Deployment } from '../features/studyClock.ts';

interface Props {
  readonly deployment: Deployment;
  readonly settings: StudySettings;
  readonly notes: EventLog | null;
}

/** Events shown before "Show all", so a stormy log does not bury the page. */
const FIRST_EVENTS = 60;

/** One line per tag, one series per line, for a chart comparing every tag at once. */
const lineFor = (story: TagStory, index: number, data: Series) => ({
  id: story.report.id, label: story.name, color: LINE_COLORS[index % LINE_COLORS.length]!, data,
});

/**
 * What happened during a deployment, from every tag's log at once: a verdict first, then when each tag
 * recorded, charged, restarted and corrected its clock, hour by hour coverage, and the counters that say
 * how hard the firmware had to work.
 */
export function WhatHappened({ deployment, settings, notes }: Props) {
  const story = useMemo(() => deploymentStory(deployment), [deployment]);
  const [allEvents, setAllEvents] = useState(false);
  const [heatTag, setHeatTag] = useState<string | null>(null);
  const [view, setView] = useState<[number, number] | null>(null);
  const zone = deployment.timezone;
  const heatStory = story?.tags.find((tag) => tag.report.id === heatTag) ?? story?.tags[0];
  const grid = useMemo(() => {
    if (!story || !heatStory) return [];
    const ranged = heatStory.report.charts!.rangedPerMinute;
    return coverageGrid({
      timezone: zone, details: deployment.scheduled, from: story.from, to: story.to, knownUntil: story.to,
      recording: heatStory.recording, charging: heatStory.charging,
      roundsByMinute: Array.from(ranged.t, (t, i) => [t, ranged.v[i]!] as const),
      roundsPerSecond: ROUNDS_PER_MINUTE / 60,
    });
  }, [story, heatStory, zone, deployment.scheduled]);

  if (!story) return <p className="empty">None of this deployment's logs could be laid out in time.</p>;
  const events = allEvents ? story.events : story.events.slice(0, FIRST_EVENTS);
  const markers = noteMarkers(notes?.events ?? [], settings.offsetSeconds);
  const extent: [number, number] = [story.from, story.to];

  return (
    <div className="stack">
      <section className={`panel verdict verdict--${story.verdict.tone}`}>
        <h2 className="panel__title">{story.verdict.headline}</h2>
        {story.verdict.findings.length > 0 && (
          <ul className="verdict__list">
            {story.verdict.findings.map((finding) => <li key={finding}>{finding}</li>)}
          </ul>
        )}
        <p className="panel__note">
          The logs run from {formatDateTime(story.from, zone)} to {formatDateTime(story.to, zone)}
          {deployment.scheduled && <>; the tags were scheduled from {formatDateTime(deployment.scheduled.experimentStartTime, zone)} to {formatDateTime(deployment.scheduled.experimentEndTime, zone)}</>}.
        </p>
      </section>

      <section className="panel">
        <h2 className="panel__title">When each tag was recording</h2>
        <DeploymentTimeline story={story} timezone={zone} />
      </section>

      <section className="panel">
        <div className="panel__head">
          <h2 className="panel__title">What each tag did</h2>
          <span className="panel__count">{story.events.length.toLocaleString()} event{story.events.length === 1 ? '' : 's'}</span>
        </div>
        {story.events.length === 0 ? (
          <p className="empty">No restarts, charging, clock corrections, or missed hours in these logs.</p>
        ) : (
          <div className="table-scroll">
            <table className="rtable">
              <thead><tr><th>When</th><th>Tag</th><th>What happened</th><th>What it means</th></tr></thead>
              <tbody>
                {events.map((event, i) => (
                  <tr key={`${event.t}-${event.tag}-${i}`} className={event.tone === 'warn' ? 'rtable__warn' : undefined}>
                    <td className="nowrap">{formatDateTime(event.t, zone)}</td>
                    <td className="nowrap">{event.tag}</td>
                    <td>{event.title}</td>
                    <td className="rtable__detail">{event.detail}</td>
                  </tr>
                ))}
              </tbody>
            </table>
          </div>
        )}
        {!allEvents && story.events.length > FIRST_EVENTS && (
          <button type="button" className="link" onClick={() => setAllEvents(true)}>Show all {story.events.length.toLocaleString()}</button>
        )}
      </section>

      <section className="panel">
        <div className="panel__head">
          <h2 className="panel__title">Hour by hour</h2>
        </div>
        {story.tags.length > 1 && (
          <div className="chips" role="radiogroup" aria-label="Tag">
            {story.tags.map((tag) => (
              <button key={tag.report.id} type="button" role="radio" aria-checked={tag === heatStory}
                className={`chip chip__button${tag === heatStory ? ' chip--on' : ''}`} onClick={() => setHeatTag(tag.report.id)}>
                {tag.name}
              </button>
            ))}
          </div>
        )}
        <p className="panel__note">Each square is an hour on the study's clock. Hover over one to see what it was.</p>
        <CoverageHeatmap days={grid} timezone={zone} />
      </section>

      <section className="panel charts">
        <h2 className="panel__title">Battery and temperature, every tag</h2>
        <TimeSeriesChart title="Battery (volts)" extent={extent} view={view} onViewChange={setView} timezone={zone}
          kinds={CHART_KINDS} markers={markers}
          lines={story.tags.map((tag, i) => lineFor(tag, i, tag.report.charts!.voltage))}
          formatValue={(volts) => `${volts.toFixed(2)} V`} />
        {story.tags.some((tag) => tag.temperature) && (
          <TimeSeriesChart title="Chip temperature (°C)" extent={extent} view={view} onViewChange={setView} timezone={zone}
            kinds={CHART_KINDS} markers={markers} height={170}
            lines={story.tags.map((tag, i) => lineFor(tag, i, tag.report.charts!.temperature))}
            formatValue={(celsius) => `${Math.round(celsius)} °C`}
            footnote="From the tag's own chip, so it runs warmer than the room." />
        )}
      </section>

      <section className="panel">
        <h2 className="panel__title">Health of each tag</h2>
        <p className="panel__note">
          How hard each tag had to work to keep going. Zeros are what a healthy tag shows; anything else is explained on its card
          on the Tags tab.
        </p>
        <div className="table-scroll">
          <table className="rtable">
            <thead>
              <tr>
                <th>Tag</th><th>Firmware</th><th className="num">Recorded</th><th className="num">Missed</th>
                <th className="num">Near another tag</th><th className="num">Restarts</th><th className="num">Clock corrections</th>
                <th className="num">Battery</th><th className="num">Temperature</th><th className="num">Near stalls</th>
                <th className="num">Records lost</th><th className="num">Radio resets</th><th className="num">Bluetooth restarts</th>
              </tr>
            </thead>
            <tbody>
              {story.tags.map((tag) => {
                const health = tag.report.health!;
                const near = health.nearMisses;
                const unexpected = tag.restarts.filter((restart) => restart.tone === 'warn').length;
                return (
                  <tr key={tag.report.id}>
                    <td>{tag.name}</td>
                    <td>{near.firmwareBuilds.join(', ') || '—'}</td>
                    <td className="num">{formatDuration(tag.secondsRecorded)}</td>
                    <td className="num">{tag.secondsMissed ? formatSpan(tag.secondsMissed) : '—'}</td>
                    <td className="num">{formatDuration(tag.minutesInRange * 60)}</td>
                    <td className="num">{tag.restarts.length}{unexpected ? ` (${unexpected} unexpected)` : ''}</td>
                    <td className="num">{tag.clockSteps.length}</td>
                    <td className="num">
                      {health.batteryStartMv && health.batteryEndMv
                        ? `${(health.batteryStartMv / 1000).toFixed(2)} → ${(health.batteryEndMv / 1000).toFixed(2)} V` : '—'}
                    </td>
                    <td className="num">{tag.temperature ? `${tag.temperature[0]}–${tag.temperature[1]} °C` : '—'}</td>
                    <td className="num">{near.samples ? near.watchdogDeclines : '—'}</td>
                    <td className="num">{near.samples ? near.recordsDropped : '—'}</td>
                    <td className="num">{near.samples ? near.radioWakeFailures + near.radioIrqStuck : '—'}</td>
                    <td className="num">{near.samples ? near.bleResets : '—'}</td>
                  </tr>
                );
              })}
            </tbody>
          </table>
        </div>
        <p className="hint">
          A dash means the log does not carry that figure: firmware older than the diagnostics record leaves the last four out.
        </p>
      </section>
    </div>
  );
}
