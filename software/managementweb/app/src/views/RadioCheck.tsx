import { useMemo, useState } from 'react';
import { ImportPanel } from '../components/ImportPanel.tsx';
import { LinkMatrix, PositionsPanel, RadioCard, VERDICT_ORDER, usePositions } from '../components/RadioResults.tsx';
import type { DeviceReport } from '../features/deployment.ts';
import { groupRadioDeployments, runRadioCheck } from '../features/radioCheck.ts';
import type { LoadResult } from '../ports/logSource.ts';
import { LiveRadioCheck } from './LiveRadioCheck.tsx';

interface Props {
  readonly reports: readonly DeviceReport[];
  readonly busy: boolean;
  readonly onLoaded: (result: LoadResult) => void;
  readonly onOpenFolder: () => void;
}

/**
 * The radio check, two ways: live over Bluetooth, with nothing to set up or download, or from the logs of a
 * deployment that already ran. Both judge each device against the others by the same rules.
 */
export function RadioCheck(props: Props) {
  const [mode, setMode] = useState<'live' | 'logs'>(props.reports.length ? 'logs' : 'live');
  return (
    <div className="radio">
      <nav className="nav" aria-label="Radio check source">
        <button type="button" className={`nav__tab${mode === 'live' ? ' nav__tab--active' : ''}`} onClick={() => setMode('live')}>
          Live over Bluetooth
        </button>
        <button type="button" className={`nav__tab${mode === 'logs' ? ' nav__tab--active' : ''}`} onClick={() => setMode('logs')}>
          From downloaded logs
        </button>
      </nav>
      {mode === 'live' ? <LiveRadioCheck /> : <LogRadioCheck {...props} />}
    </div>
  );
}

/** Every device a deployment selected, judged against the others from their logs. */
function LogRadioCheck({ reports, busy, onLoaded, onOpenFolder }: Props) {
  const deployments = useMemo(() => groupRadioDeployments(reports), [reports]);
  const [selectedKey, setSelectedKey] = useState<string | null>(null);
  const deployment = deployments.find((d) => d.key === selectedKey) ?? deployments[0] ?? null;
  const positions = usePositions(deployment ? `tottag.radioCheck.positions.${deployment.key}` : null);
  const result = useMemo(() => (deployment ? runRadioCheck(deployment, positions.positions) : null), [deployment, positions.positions]);

  if (!deployment || !result) {
    return (
      <>
        <section className="panel">
          <h2 className="panel__title">Check devices' radios from their logs</h2>
          <p className="panel__note">
            Compares every device in a deployment against the others, to find one with a weak receiver, a
            damaged antenna, or a distance calibration that is off. The live check does the same without a
            deployment; this one reads the logs of a deployment that already ran.
          </p>
          <ol className="steps">
            <li>Set up a deployment with the devices you want to check, starting now.</li>
            <li>Put them where they can all see each other, at least half a metre apart, and leave them for 10 minutes.</li>
            <li>Download every device's log, then open the logs here.</li>
          </ol>
        </section>
        <ImportPanel onLoaded={onLoaded} onOpenFolder={onOpenFolder} busy={busy} />
      </>
    );
  }

  const devices = [...result.devices].sort((a, b) => VERDICT_ORDER[a.verdict] - VERDICT_ORDER[b.verdict] || a.label.localeCompare(b.label));
  const counts = { pass: 0, check: 0, fail: 0, missing: 0 };
  for (const device of result.devices) counts[device.verdict] += 1;
  const minutes = result.windowStartMs !== null && result.windowEndMs !== null
    ? Math.round((result.windowEndMs - result.windowStartMs) / 60_000) : 0;

  return (
    <>
      {deployments.length > 1 && (
        <section className="panel">
          <h2 className="panel__title">Deployment</h2>
          <div className="radio__choices">
            {deployments.map((d) => (
              <label key={d.key} className="checkbox">
                <input type="radio" name="deployment" checked={d.key === deployment.key} onChange={() => setSelectedKey(d.key)} />
                Started {new Date(d.startTime * 1000).toLocaleString()} · {d.devices.length} devices, {d.logs.size} logs loaded
              </label>
            ))}
          </div>
        </section>
      )}

      <div className="summary">
        <p className="summary__line">
          <strong>{deployment.devices.length}</strong> device{deployment.devices.length === 1 ? '' : 's'} in the deployment
          started {new Date(deployment.startTime * 1000).toLocaleString()}
          {minutes > 0 && <> · compared over the {minutes} minute{minutes === 1 ? '' : 's'} they were all running</>}
          {' · '}
          {counts.fail > 0 && <span className="summary__warning">{counts.fail} failed · </span>}
          {counts.check > 0 && <span className="summary__warning">{counts.check} to check · </span>}
          {counts.missing > 0 && <span className="summary__error">{counts.missing} without a log · </span>}
          {counts.pass} passed
        </p>
      </div>

      {result.notes.map((note) => <p key={note} className="banner banner--warning">{note}</p>)}
      {deployment.unmatched.map((note) => <p key={note} className="banner banner--warning">{note}</p>)}

      <div className="cards">
        {devices.map((device) => <RadioCard key={device.uid} device={device} />)}
      </div>
      <LinkMatrix devices={deployment.devices} links={result.links} />
      <PositionsPanel devices={deployment.devices} positions={positions} />
    </>
  );
}
