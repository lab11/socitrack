import { DeviceCard } from '../components/DeviceCard.tsx';
import { DownloadFromTag } from '../components/DownloadFromTag.tsx';
import { ImportPanel } from '../components/ImportPanel.tsx';
import type { LogsApi } from '../lib/useLogs.ts';

/** Getting logs off tags or off disk, and seeing at a glance whether each one is sound. */
export function Review({ logs, timezone }: { logs: LogsApi; timezone: string }) {
  const { reports, ordered, counts, failures, busy, saved, unsaved, saveError, onLoaded, saveLog, openFolder } = logs;
  const hasLogs = reports.length > 0;

  return (
    <>
      {unsaved.length > 0 && (
        <p className="banner banner--warn">
          {unsaved.length === 1 ? 'One log is' : `${unsaved.length} logs are`} only in this browser.
          Use <strong>Save</strong> on {unsaved.length === 1 ? 'its card' : 'their cards'} to put
          {unsaved.length === 1 ? ' it' : ' them'} on your computer.
        </p>
      )}
      {saveError && <p className="banner banner--crit">Could not save: {saveError}</p>}

      <div className="review__intake">
        <DownloadFromTag busy={busy} onDownloaded={(log) => void onLoaded({ loaded: [log], failed: [] })} />
        <ImportPanel onLoaded={(result) => void onLoaded(result)} onOpenFolder={() => void openFolder()} busy={busy} compact={hasLogs} />
      </div>

      {failures.length > 0 && (
        <ul className="failures">
          {failures.map((failure) => (
            <li key={failure.name} className="failures__item"><strong>{failure.name}</strong>: {failure.reason}</li>
          ))}
        </ul>
      )}

      {hasLogs && (
        <>
          <p className="summary-line">
            <strong>{reports.length}</strong> log{reports.length === 1 ? '' : 's'} open
            {counts.warning > 0 && <> · <span className="summary-line__warn">{counts.warning} {counts.warning === 1 ? 'needs' : 'need'} attention</span></>}
            {counts.error > 0 && <> · <span className="summary-line__crit">{counts.error} could not be read</span></>}
            {counts.warning === 0 && counts.error === 0 && <> · all look sound</>}
          </p>
          <div className="cards">
            {ordered.map((report) => (
              <DeviceCard
                key={report.id}
                report={report}
                timezone={timezone}
                onSave={report.data ? () => void saveLog(report) : undefined}
                saved={saved.has(report.id)}
              />
            ))}
          </div>
        </>
      )}
    </>
  );
}
