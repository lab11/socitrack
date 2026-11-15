import { useEffect, useMemo, useState } from 'react';
import { parseEventLog } from '@tottag/schema';
import { fileLogSource } from '../adapters/fileLogSource.ts';
import { DeviceCard } from '../components/DeviceCard.tsx';
import { DownloadFromTag } from '../components/DownloadFromTag.tsx';
import { ImportPanel } from '../components/ImportPanel.tsx';
import { StudyClock } from '../components/StudyClock.tsx';
import { DEFAULT_SETTINGS, type StudySettings } from '../features/chartData.ts';
import { groupDeployments, type Deployment } from '../features/studyClock.ts';
import type { LogsApi } from '../lib/useLogs.ts';
import { CompareTags } from './CompareTags.tsx';
import { TagCharts } from './TagCharts.tsx';
import { TimeTogether } from './TimeTogether.tsx';
import { WhatHappened } from './WhatHappened.tsx';

type Tab = 'tags' | 'story' | 'charts' | 'together' | 'compare';

const TABS: ReadonlyArray<{ id: Tab; label: string }> = [
  { id: 'tags', label: 'Tags' },
  { id: 'story', label: 'What happened' },
  { id: 'charts', label: 'Charts' },
  { id: 'together', label: 'Time together' },
  { id: 'compare', label: 'Compare two tags' },
];

/** Getting logs off tags or off disk, seeing at a glance whether each one is sound, and looking into the data. */
export function Review({ logs, timezone }: { logs: LogsApi; timezone: string }) {
  const { reports, ordered, counts, failures, records, busy, saved, unsaved, saveError, onLoaded, saveLog, openFolder } = logs;
  const hasLogs = reports.length > 0;

  // The setup page's zone is only the fallback: a deployment whose record is open uses the record's.
  const { deployments, unreadable } = useMemo(() => groupDeployments(ordered, records, timezone), [ordered, records, timezone]);
  const [selected, setSelected] = useState<string | null>(null);
  const [tab, setTab] = useState<Tab>('tags');
  const deployment: Deployment | undefined = deployments.find((d) => d.key === selected) ?? deployments[0];
  useEffect(() => {
    if (deployment && deployment.key !== selected) setSelected(deployment.key);
  }, [deployment, selected]);

  // Distance settings and noted events belong to one deployment, and last only as long as the page:
  // nothing study-specific is remembered for the next one.
  const [allSettings, setAllSettings] = useState<Readonly<Record<string, StudySettings>>>({});
  const settings = (deployment && allSettings[deployment.key]) || DEFAULT_SETTINGS;
  const setSettings = (next: StudySettings) => {
    if (deployment) setAllSettings((current) => ({ ...current, [deployment.key]: next }));
  };
  // Read on the deployment's own clock, so adding the record afterwards moves the notes with everything else.
  const notes = useMemo(
    () => (settings.notes && deployment ? parseEventLog(settings.notes.text, deployment.timezone) : null),
    [settings.notes, deployment],
  );

  const strays = useMemo(
    () => records.filter((open) => !deployments.some((d) => d.record?.id === open.id)),
    [deployments, records],
  );
  const addRecord = () => {
    void fileLogSource.load().then((result) => onLoaded(result));
  };

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
            {deployments.length > 1 && <> from <strong>{deployments.length}</strong> deployments</>}
            {counts.warning > 0 && <> · <span className="summary-line__warn">{counts.warning} {counts.warning === 1 ? 'needs' : 'need'} attention</span></>}
            {counts.error > 0 && <> · <span className="summary-line__crit">{counts.error} could not be read</span></>}
            {counts.warning === 0 && counts.error === 0 && <> · all look sound</>}
          </p>

          {unreadable.length > 0 && (
            <div className="cards">
              {unreadable.map((report) => (
                <DeviceCard key={report.id} report={report} timezone={timezone}
                  onSave={report.data ? () => void saveLog(report) : undefined} saved={saved.has(report.id)} />
              ))}
            </div>
          )}

          {deployments.length > 1 && (
            <div className="chips" role="radiogroup" aria-label="Deployment">
              {deployments.map((d) => (
                <button key={d.key} type="button" role="radio" aria-checked={d.key === deployment?.key}
                  className={`chip chip__button${d.key === deployment?.key ? ' chip--on' : ''}`} onClick={() => setSelected(d.key)}>
                  {d.title}
                </button>
              ))}
            </div>
          )}

          {deployment && (
            <section className="deployment" aria-label={deployment.title}>
              <StudyClock deployment={deployment} strays={strays} busy={busy} onAddRecord={addRecord} />
              <div className="tabs" role="tablist" aria-label="What to look at">
                {TABS.map((entry) => (
                  <button key={entry.id} type="button" role="tab" aria-selected={tab === entry.id}
                    className={`tabs__tab${tab === entry.id ? ' tabs__tab--on' : ''}`} onClick={() => setTab(entry.id)}>
                    {entry.label}
                  </button>
                ))}
              </div>

              {tab === 'tags' && (
                <div className="cards">
                  {deployment.reports.map((report) => (
                    <DeviceCard
                      key={report.id}
                      report={report}
                      timezone={deployment.timezone}
                      onSave={report.data ? () => void saveLog(report) : undefined}
                      saved={saved.has(report.id)}
                    />
                  ))}
                </div>
              )}
              {tab === 'story' && <WhatHappened key={deployment.key} deployment={deployment} settings={settings} notes={notes} />}
              {tab === 'charts' && <TagCharts key={deployment.key} deployment={deployment} settings={settings} onSettings={setSettings} notes={notes} />}
              {tab === 'together' && <TimeTogether key={deployment.key} deployment={deployment} settings={settings} onSettings={setSettings} notes={notes} />}
              {tab === 'compare' && <CompareTags key={deployment.key} deployment={deployment} settings={settings} onSettings={setSettings} notes={notes} />}
            </section>
          )}
        </>
      )}
    </>
  );
}
