import { useCallback, useEffect, useMemo, useRef, useState } from 'react';
import { Deploy } from './views/Deploy.tsx';
import { RadioCheck } from './views/RadioCheck.tsx';
import { DownloadFromTag } from './components/DownloadFromTag.tsx';
import { downloadBytes, downloadText } from './adapters/download.ts';
import { directoryLogSource } from './adapters/fileLogSource.ts';
import { DeviceCard } from './components/DeviceCard.tsx';
import { ImportPanel } from './components/ImportPanel.tsx';
import { buildReport, sortByUrgency, type DeviceReport } from './features/deployment.ts';
import { summaryCsv, summaryFilename } from './features/exportCsv.ts';
import type { LoadFailure, LoadResult } from './ports/logSource.ts';

type View = 'logs' | 'deploy' | 'radio';

export function App() {
  const [view, setView] = useState<View>('logs');
  const [reports, setReports] = useState<readonly DeviceReport[]>([]);
  const [failures, setFailures] = useState<readonly LoadFailure[]>([]);
  const [busy, setBusy] = useState(false);
  // Ids of logs whose bytes have been written to disk at least once. Only meaningful for logs that
  // came off a tag; an imported file is on disk by definition.
  const [saved, setSaved] = useState<ReadonlySet<string>>(() => new Set());
  const [saveError, setSaveError] = useState<string | null>(null);
  // Index counter kept outside state: onLoaded must not close over a stale reports array.
  const reportCount = useRef(0);

  const onLoaded = useCallback(async (result: LoadResult) => {
    setBusy(true);
    try {
      // Parsed in a worker, so a multi-megabyte deployment file does not freeze the page. Sequential
      // rather than parallel: one worker handles them in turn, and a dozen files racing for it would
      // only make the first one slower.
      const built: DeviceReport[] = [];
      for (const [index, log] of result.loaded.entries()) {
        built.push(await buildReport(log, reportCount.current + index));
      }
      reportCount.current += built.length;
      setReports((current) => [...current, ...built]);
      setFailures(result.failed);
    } finally {
      setBusy(false);
    }
  }, []);

  const ordered = useMemo(() => sortByUrgency(reports), [reports]);
  const counts = useMemo(() => {
    const tally = { ok: 0, note: 0, warning: 0, error: 0 };
    for (const report of reports) tally[report.status] += 1;
    return tally;
  }, [reports]);

  const unsaved = useMemo(
    () => reports.filter((report) => report.data !== null && !saved.has(report.id)),
    [reports, saved],
  );

  // The one thing this app can destroy. Bytes pulled off a tag exist nowhere else, a transfer costs
  // minutes, and a closed tab takes them with it silently. The browser's own dialog is generic and
  // cannot be reworded, but it is the only thing that fires before the page is gone.
  useEffect(() => {
    if (unsaved.length === 0) return undefined;
    const warn = (event: BeforeUnloadEvent) => {
      event.preventDefault();
      event.returnValue = '';   // still required by browsers that predate preventDefault() here
    };
    window.addEventListener('beforeunload', warn);
    return () => window.removeEventListener('beforeunload', warn);
  }, [unsaved.length]);

  const exportCsv = useCallback(() => {
    void downloadText(summaryFilename(new Date()), summaryCsv(ordered));
  }, [ordered]);

  const saveLog = useCallback(async (report: DeviceReport) => {
    if (!report.data) return;
    setSaveError(null);
    try {
      // Only on a real write. A dismissed save dialog must leave the log counted as unsaved, or the
      // warning and the disabled Clear — the two things standing between a cancelled save and lost
      // bytes — would both quietly go away.
      if (await downloadBytes(report.saveName, report.data)) {
        setSaved((current) => new Set(current).add(report.id));
      }
    } catch (caught) {
      setSaveError(caught instanceof Error ? caught.message : String(caught));
    }
  }, []);

  const openFolder = useCallback(async () => {
    onLoaded(await directoryLogSource.load());
  }, [onLoaded]);

  const hasLogs = reports.length > 0;

  return (
    <div className="shell">
      <header className="shell__head">
        <div className="shell__head-inner">
          <div>
            <h1 className="shell__title">TotTag</h1>
            <p className="shell__tagline">Set up deployments and read what they recorded</p>
          </div>
          <nav className="nav" aria-label="Sections">
            <button type="button" className={`nav__tab${view === 'deploy' ? ' nav__tab--active' : ''}`}
              onClick={() => setView('deploy')} aria-current={view === 'deploy'}>Set up a deployment</button>
            <button type="button" className={`nav__tab${view === 'logs' ? ' nav__tab--active' : ''}`}
              onClick={() => setView('logs')} aria-current={view === 'logs'}>Logs</button>
            <button type="button" className={`nav__tab${view === 'radio' ? ' nav__tab--active' : ''}`}
              onClick={() => setView('radio')} aria-current={view === 'radio'}>Radio check</button>
          </nav>
          {hasLogs && view === 'logs' && (
            <div className="shell__actions">
              <button type="button" className="button" onClick={exportCsv}>
                Export summary (CSV)
              </button>
              <button type="button" className="button button--quiet"
                onClick={() => { setReports([]); setFailures([]); setSaved(new Set()); setSaveError(null); }}
                disabled={unsaved.length > 0}
                title={unsaved.length > 0 ? 'Save the downloaded logs first — clearing would discard them' : undefined}>
                Clear
              </button>
            </div>
          )}
        </div>
      </header>

      <main className="shell__main">
        {view === 'deploy' ? <Deploy /> : view === 'radio' ? (
          <RadioCheck reports={reports} busy={busy} onLoaded={(result) => void onLoaded(result)} onOpenFolder={openFolder} />
        ) : !hasLogs ? (
          <>
            <ImportPanel onLoaded={(result) => void onLoaded(result)} onOpenFolder={openFolder} busy={busy} />
            <DownloadFromTag busy={busy} onDownloaded={(log) => void onLoaded({ loaded: [log], failed: [] })} />
          </>
        ) : (
          <>
            {unsaved.length > 0 && (
              <p className="banner banner--warning">
                {unsaved.length === 1 ? 'One log is' : `${unsaved.length} logs are`} only in this browser.
                Use <strong>Save</strong> on {unsaved.length === 1 ? 'its card' : 'their cards'} to write
                {unsaved.length === 1 ? ' it' : ' them'} to disk.
              </p>
            )}
            {saveError && <p className="banner banner--error">Could not save: {saveError}</p>}
            <div className="summary">
              <p className="summary__line">
                <strong>{reports.length}</strong> log{reports.length === 1 ? '' : 's'} open
                {counts.warning > 0 && (
                  <> · <span className="summary__warning">
                    {counts.warning} {counts.warning === 1 ? "needs" : "need"} attention
                  </span></>
                )}
                {counts.error > 0 && <> · <span className="summary__error">{counts.error} unreadable</span></>}
                {counts.warning === 0 && counts.error === 0 && <> · all look sound</>}
              </p>
            </div>
            <ImportPanel onLoaded={(result) => void onLoaded(result)} onOpenFolder={openFolder} busy={busy} compact />
            <DownloadFromTag busy={busy} onDownloaded={(log) => void onLoaded({ loaded: [log], failed: [] })} />
            <div className="cards">
              {ordered.map((report) => (
                <DeviceCard
                  key={report.id}
                  report={report}
                  onSave={report.data ? () => void saveLog(report) : undefined}
                  saved={saved.has(report.id)}
                />
              ))}
            </div>
          </>
        )}

        {failures.length > 0 && (
          <ul className="failures">
            {failures.map((failure) => (
              <li key={failure.name} className="failures__item">
                <strong>{failure.name}</strong> — {failure.reason}
              </li>
            ))}
          </ul>
        )}
      </main>

      <footer className="shell__foot">
        <p>
          {view !== 'deploy'
            ? 'Logs are read in this browser and never uploaded. An imported file is left untouched; a log downloaded from a tag exists only here until you save it.'
            : 'This deployment is saved in this browser as you type, so a reload will not lose it. Nothing is uploaded.'}
        </p>
      </footer>
    </div>
  );
}
