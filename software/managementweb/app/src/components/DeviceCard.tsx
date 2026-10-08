import { describeReboots, formatBytes, formatDuration, type DeviceReport } from '../features/deployment.ts';
import { StatusBadge } from './StatusBadge.tsx';
import { Stat } from './Stat.tsx';

interface Props {
  readonly report: DeviceReport;
  /** Present only for a log that came off a tag and so exists nowhere else yet. */
  readonly onSave?: (() => void) | undefined;
  readonly saved?: boolean;
}

/**
 * One log, summarised.
 *
 * The ordering is deliberate and comes from what the 3.9-day audit actually needed to know, in the
 * order it needed to know it: is the file whole, how long did it run, did the device restart, and
 * only then the detail. Anomalies are shown in full rather than counted, because a count is a
 * prompt to go and look somewhere else, and there is nowhere else.
 */
export function DeviceCard({ report, onSave, saved = false }: Props) {
  const { health, report: parseReport, error } = report;

  return (
    <article className={`card card--${report.status}`}>
      <header className="card__head">
        <div>
          <h3 className="card__title">{report.name}</h3>
          <p className="card__sub">
            {formatBytes(report.bytes)}
            {parseReport ? ` · format v${parseReport.format}` : ''}
          </p>
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
              ? 'Saved. Save it again to write another copy.'
              : 'Downloaded from the tag and not saved yet — it will be lost when this page closes.'}
          </p>
          <button
            type="button"
            className={saved ? 'button button--quiet' : 'button'}
            onClick={onSave}
          >
            {saved ? 'Save again' : `Save ${report.saveName}`}
          </button>
        </div>
      )}

      {error ? (
        <p className="card__error">
          This file could not be read: {error}
          <br />
          <span className="card__error-hint">
            If the download was interrupted, downloading it again from the tag usually fixes this —
            the data is still on the device until a new deployment is scheduled.
          </span>
        </p>
      ) : health ? (
        <>
          <dl className="stats">
            <Stat label="Ran for" value={formatDuration(health.spanSeconds)} />
            <Stat label="Records" value={health.recordCount.toLocaleString()} />
            <Stat label="Restarts" value={String(health.reboots.length)} hint={describeReboots(health)} />
            <Stat
              label="Data recovered"
              value={
                health.integrity.holes + health.integrity.crcFailures === 0
                  ? 'All of it'
                  : `${health.integrity.holes + health.integrity.crcFailures} page(s) lost`
              }
            />
            <Stat
              label="Battery"
              value={
                health.batteryStartMv && health.batteryEndMv
                  ? `${(health.batteryStartMv / 1000).toFixed(2)} → ${(health.batteryEndMv / 1000).toFixed(2)} V`
                  : '—'
              }
            />
            <Stat label="Peers seen" value={String(health.rangingPeers.size)} />
            {health.nearMisses.firmwareBuilds.length > 0 && (
              <Stat label="Firmware" value={health.nearMisses.firmwareBuilds.join(', ')} />
            )}
          </dl>

          {health.anomalies.length > 0 && (
            <ul className="findings">
              {health.anomalies.map((anomaly) => (
                <li key={anomaly.code} className={`finding finding--${anomaly.severity}`}>
                  <span className="finding__tag">{anomaly.severity === 'warning' ? 'Check' : 'Note'}</span>
                  <span className="finding__text">{anomaly.message}</span>
                </li>
              ))}
            </ul>
          )}
        </>
      ) : null}
    </article>
  );
}
