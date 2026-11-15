import { useMemo, useState } from 'react';
import { formatDay, type EventLog } from '@tottag/schema';
import { downloadText } from '../adapters/download.ts';
import { StudyTools } from '../components/StudyTools.tsx';
import { cutoffIn, touchingIn, type StudySettings } from '../features/chartData.ts';
import type { Deployment } from '../features/studyClock.ts';
import { zoneName } from '../features/studyClock.ts';
import { logName, logTag, tagRef } from '../features/tagNames.ts';
import { formatMinutes, tableLines, timeTogetherCsv, timeTogetherFilename } from '../features/timeTogether.ts';
import { toUnit, UNIT_WORDS } from '../features/units.ts';
import { requestDailyStats } from '../lib/parseAsync.ts';
import { useLatest } from '../lib/useLatest.ts';

interface Props {
  readonly deployment: Deployment;
  readonly settings: StudySettings;
  readonly onSettings: (settings: StudySettings) => void;
  readonly notes: EventLog | null;
}

/**
 * How long one tag spent near others, day by day on the study's calendar.
 *
 * Counted from every ranging round, which the tags run twice a second, rather than from per-minute
 * summaries, so a few seconds together still count as the few seconds they were.
 */
export function TimeTogether({ deployment, settings, onSettings, notes }: Props) {
  const logs = deployment.reports.filter((report) => report.rangePeers.length > 0);
  const [chosen, setChosen] = useState<string | null>(null);
  const report = logs.find((entry) => entry.id === chosen) ?? logs[0];
  const peers = useMemo(
    () => (report ? report.rangePeers.map((uid) => tagRef(uid, deployment.scheduled)) : []),
    [report, deployment.scheduled],
  );
  // No peer chosen yet means all of them, so the table is never empty on arrival.
  const [picked, setPicked] = useState<readonly number[] | null>(null);
  const selected = peers.filter((peer) => (picked ?? peers.map((p) => p.uid)).includes(peer.uid));
  const togglePeer = (uid: number) => {
    const current = picked ?? peers.map((p) => p.uid);
    setPicked(current.includes(uid) ? current.filter((value) => value !== uid) : [...current, uid]);
  };

  const zone = deployment.timezone;
  const options = { timezone: zone, peers: selected.map((peer) => peer.uid), touchingMm: settings.touchingMm, cutoffMm: settings.cutoffMm };
  const rows = useLatest(
    () => (report && selected.length ? requestDailyStats(report.id, options) : null),
    [report?.id, zone, options.peers.join(), options.touchingMm, options.cutoffMm],
  );

  if (!report) return <p className="empty">None of this deployment's logs measured a distance to another tag.</p>;
  const tag = logTag(report, deployment.scheduled)?.display ?? report.name;
  const unit = UNIT_WORDS[settings.unit];
  const table = rows.value
    ? { tag, peers: selected.map((peer) => peer.display), rows: rows.value, unit: settings.unit, touching: touchingIn(settings), cutoff: cutoffIn(settings), timezone: zone }
    : null;
  const lines = table ? tableLines(table) : [];

  return (
    <div className="stack">
      <div className="panel">
        <div className="chooser">
          <span className="tools__label">Tag</span>
          <div className="chips" role="radiogroup" aria-label="Tag">
            {logs.map((entry) => (
              <button key={entry.id} type="button" role="radio" aria-checked={entry.id === report.id}
                className={`chip chip__button${entry.id === report.id ? ' chip--on' : ''}`}
                onClick={() => { setChosen(entry.id); setPicked(null); }}>
                {logName(entry, deployment.scheduled)}
              </button>
            ))}
          </div>
        </div>
        <div className="chooser">
          <span className="tools__label">Time near</span>
          <div className="chips">
            {peers.map((peer) => (
              <label key={peer.uid} className={`chip${selected.includes(peer) ? ' chip--on' : ''}`}>
                <input type="checkbox" checked={selected.includes(peer)} onChange={() => togglePeer(peer.uid)} />
                {peer.display}
              </label>
            ))}
          </div>
        </div>
        <StudyTools settings={settings} onChange={onSettings} notes={notes} showTouching />
      </div>

      <section className="panel">
        <div className="panel__head">
          <h2 className="panel__title">Time {tag} spent near {selected.length === 1 ? selected[0]!.display : 'other tags'}, each day</h2>
          {table && lines.length > 0 && (
            <button type="button" className="button button--quiet button--small panel__count"
              onClick={() => void downloadText(timeTogetherFilename(tag, table.rows[0]?.date), timeTogetherCsv(table))}>
              Export (CSV)
            </button>
          )}
        </div>
        <p className="panel__note">
          Days run from midnight to midnight in {zoneName(zone)}. A minute in range is 120 measurements, which the tags make
          twice a second, within {cutoffIn(settings)} {unit.short}; touching distance is within {touchingIn(settings)} {unit.short}.
          {selected.length > 1 && ' "Either one" or "Any of them" counts time near at least one of the tags chosen; "Both" or "All" counts time near every one at once.'}
        </p>
        {!selected.length ? (
          <p className="empty">Choose at least one tag above.</p>
        ) : rows.error ? (
          <p className="banner banner--crit">Could not work these out: {rows.error}</p>
        ) : rows.value === null ? (
          <p className="banner banner--warn">
            This log's measurements were set aside to save memory. Close some logs, then open this one again.
          </p>
        ) : !table ? (
          <p className="empty">Working these out…</p>
        ) : (
          <div className="table-scroll">
            <table className="rtable">
              <thead>
                <tr>
                  <th>Day</th>
                  <th>Near</th>
                  <th className="num">Minutes in range</th>
                  <th className="num">Minutes within touching distance</th>
                  <th className="num">Average distance while in range</th>
                </tr>
              </thead>
              <tbody>
                {lines.map((line, index) => (
                  <tr key={`${line.date}-${line.with}`} className={line.combined ? 'rtable__combined' : index > 0 && lines[index - 1]!.date !== line.date ? 'rtable__day' : undefined}>
                    <td>{index === 0 || lines[index - 1]!.date !== line.date ? formatDay(table.rows.find((row) => row.date === line.date)!.dayStart + 43200, zone) : ''}</td>
                    <td>{line.with}</td>
                    <td className="num">{formatMinutes(line.figures.minutesInRange)}</td>
                    <td className="num">{formatMinutes(line.figures.minutesTouching)}</td>
                    <td className="num">{line.figures.meanMm === null ? '—' : `${toUnit(line.figures.meanMm, settings.unit).toFixed(1)} ${unit.short}`}</td>
                  </tr>
                ))}
              </tbody>
            </table>
          </div>
        )}
      </section>
    </div>
  );
}
