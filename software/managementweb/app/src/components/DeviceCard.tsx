import { formatDateTime, formatTimeOfDay, localDailyWindow } from '@tottag/schema';
import { describeReboots, formatBytes, formatDuration, type DeviceReport } from '../features/deployment.ts';
import { summariseLog } from '../features/logSummary.ts';
import { plainFinding } from '../features/plainLanguage.ts';
import { StatusBadge } from './StatusBadge.tsx';
import { Stat } from './Stat.tsx';

interface Props {
  readonly report: DeviceReport;
  /** The clock to show times on: the study's time zone from the setup page. */
  readonly timezone: string;
  /** Present only for a log that came off a tag and so exists nowhere else yet. */
  readonly onSave?: (() => void) | undefined;
  readonly saved?: boolean;
}

/**
 * One tag's log, summarised.
 *
 * Ordered by what someone about to reuse the tags needs to know: whose log this is, whether it is
 * whole, whether it covers the dates the study was meant to, whether this tag saw every other tag,
 * and only then the detail. Each finding says first what it means and what to do; the analysis's own
 * wording is kept beneath it for whoever needs it.
 */
export function DeviceCard({ report, timezone, onSave, saved = false }: Props) {
  const { health, report: parseReport, error } = report;
  const summary = summariseLog(report);
  const scheduled = summary.scheduled;
  const daily = scheduled?.useDailyTimes
    ? localDailyWindow({ start: scheduled.dailyStartTime, end: scheduled.dailyEndTime }, timezone, scheduled.experimentStartTime)
    : null;
  const title = summary.label ?? summary.tag ?? report.name;
  const subtitle = [summary.label ? summary.tag : null, report.name !== title ? report.name : null, formatBytes(report.bytes)]
    .filter(Boolean).join(' · ');

  return (
    <article className={`card card--${report.status}`}>
      <header className="card__head">
        <div>
          <h3 className="card__title">{title}</h3>
          <p className="card__sub">{subtitle}</p>
        </div>
        <StatusBadge status={report.status} />
      </header>

      {onSave && (
        // A log offloaded over the air lives only in this page until it is written somewhere. The
        // control says so rather than sitting quietly beside a card that looks finished, because the
        // failure it prevents — closing the tab on an unsaved multi-minute transfer — is silent and
        // total.
        <div className={`keep${saved ? ' keep--done' : ''}`}>
          <p className="keep__text">
            {saved
              ? 'Saved to your computer. Save it again for another copy.'
              : 'Downloaded from the tag but not saved yet. It will be lost when this page closes.'}
          </p>
          <button type="button" className={saved ? 'button button--quiet' : 'button'} onClick={onSave}>
            {saved ? 'Save again' : `Save ${report.saveName}`}
          </button>
        </div>
      )}

      {error ? (
        <p className="card__error">
          This file could not be read: {error}
          <br />
          <span className="card__error-hint">
            If the download was interrupted, downloading it again from the tag usually fixes this. The data
            stays on the tag until a new deployment is written to it.
          </span>
        </p>
      ) : health ? (
        <>
          <dl className="when">
            {scheduled && (
              <div>
                <dt>Scheduled</dt>
                <dd>
                  {formatDateTime(scheduled.experimentStartTime, timezone)} – {formatDateTime(scheduled.experimentEndTime, timezone)}
                  {daily ? `, ${formatTimeOfDay(daily.start)} – ${formatTimeOfDay(daily.end)} each day` : ''}
                </dd>
              </div>
            )}
            {summary.firstRecord !== null && summary.lastRecord !== null && (
              <div>
                <dt>Recorded</dt>
                <dd>{formatDateTime(summary.firstRecord, timezone)} – {formatDateTime(summary.lastRecord, timezone)}</dd>
              </div>
            )}
          </dl>

          <dl className="stats">
            <Stat label="Ran for" value={formatDuration(health.spanSeconds)} />
            <Stat label="Data received" value={
              health.integrity.holes + health.integrity.crcFailures === 0
                ? 'All of it'
                : `${health.integrity.holes + health.integrity.crcFailures} piece(s) missing`
            } />
            <Stat label="Restarts" value={String(health.reboots.length)} hint={describeReboots(health)} />
            <Stat label="Battery" value={
              health.batteryStartMv && health.batteryEndMv
                ? `${(health.batteryStartMv / 1000).toFixed(2)} → ${(health.batteryEndMv / 1000).toFixed(2)} V`
                : '—'
            } />
            <Stat label="Records" value={health.recordCount.toLocaleString()} />
            {health.nearMisses.firmwareBuilds.length > 0 && (
              <Stat label="Firmware" value={health.nearMisses.firmwareBuilds.join(', ')} />
            )}
          </dl>

          {summary.peers.length > 0 && (
            <div className="peers">
              <h4 className="peers__title">Tags it measured distances to</h4>
              {!summary.selfKnown && (
                <p className="hint">This log does not say which tag it came from, so one of these is the tag itself.</p>
              )}
              <ul className="peers__list">
                {summary.peers.map((peer) => (
                  <li key={peer.uid} className={`peer peer--${peer.ranges > 0 ? (peer.scheduled ? 'ok' : 'other') : summary.selfKnown ? 'missing' : 'other'}`}>
                    <span className="peer__name">{peer.name}</span>
                    <span className="peer__count">
                      {peer.ranges > 0
                        ? `${peer.ranges.toLocaleString()} time${peer.ranges === 1 ? '' : 's'}${peer.scheduled ? '' : ', not in this deployment'}`
                        : 'never'}
                    </span>
                  </li>
                ))}
              </ul>
            </div>
          )}

          {health.anomalies.length > 0 && (
            <ul className="findings">
              {health.anomalies.map((anomaly) => {
                const plain = plainFinding(anomaly, health);
                return (
                  <li key={anomaly.code} className={`finding finding--${anomaly.severity === 'warning' ? 'warn' : 'note'}`}>
                    <span className="finding__tag">{anomaly.severity === 'warning' ? 'Check' : 'Note'}</span>
                    <div className="finding__body">
                      {plain ? (
                        <>
                          <p className="finding__text">{plain.summary}</p>
                          <p className="finding__action">{plain.action}</p>
                          <details className="finding__detail">
                            <summary>Technical detail</summary>
                            <p>{anomaly.message}</p>
                          </details>
                        </>
                      ) : (
                        <p className="finding__text">{anomaly.message}</p>
                      )}
                    </div>
                  </li>
                );
              })}
            </ul>
          )}
          {parseReport && <p className="card__foot">Log format v{parseReport.format}</p>}
        </>
      ) : null}
    </article>
  );
}
