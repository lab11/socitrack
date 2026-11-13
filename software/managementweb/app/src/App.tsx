import { useCallback, useEffect, useState, type ReactNode } from 'react';
import { webBluetoothTransport } from './adapters/webBluetooth.ts';
import { webSerialTransport } from './adapters/webSerial.ts';
import { ConfirmButton } from './components/ConfirmButton.tsx';
import { BrandMark, CalendarIcon, DownloadIcon, LockIcon, SearchIcon, SignalIcon } from './components/Icons.tsx';
import { useDeploymentDraft } from './features/useDeploymentDraft.ts';
import { useLogs } from './lib/useLogs.ts';
import { CheckTag } from './views/CheckTag.tsx';
import { Deploy } from './views/Deploy.tsx';
import { RadioCheck } from './views/RadioCheck.tsx';
import { Review } from './views/Review.tsx';

type View = 'deploy' | 'check' | 'review' | 'radio';

// In the order a study meets them: set the tags up, check one when in doubt, bring the data back in.
// The radio check is a bench test done now and then, so it comes last.
const VIEWS: ReadonlyArray<{ id: View; label: string; title: string; icon: ReactNode }> = [
  { id: 'deploy', label: 'Set up a deployment', title: 'Set up a deployment', icon: <CalendarIcon /> },
  { id: 'check', label: 'Check a tag', title: 'Check a tag', icon: <SearchIcon /> },
  { id: 'review', label: 'Download & review', title: 'Download and review data', icon: <DownloadIcon /> },
  { id: 'radio', label: 'Radio check', title: "Check tags' radios", icon: <SignalIcon /> },
];

const viewFromHash = (): View => {
  const id = window.location.hash.replace('#', '');
  return VIEWS.some((view) => view.id === id) ? (id as View) : 'deploy';
};

export function App() {
  const [view, setView] = useState<View>(viewFromHash);
  const draftApi = useDeploymentDraft();
  const logs = useLogs();
  const { draft, addDevice } = draftApi;

  // The page is in the address, so a reload or the back button lands where it was.
  useEffect(() => {
    if (window.location.hash !== `#${view}`) history.replaceState(null, '', `#${view}`);
    window.scrollTo(0, 0);
  }, [view]);
  useEffect(() => {
    const follow = () => setView(viewFromHash());
    window.addEventListener('hashchange', follow);
    return () => window.removeEventListener('hashchange', follow);
  }, []);

  const labelFor = useCallback((eui: Uint8Array) => {
    const match = draft.devices.find((device) => device.eui.length === eui.length && device.eui.every((b, i) => b === eui[i]));
    return match?.label.trim() || undefined;
  }, [draft.devices]);

  const active = VIEWS.find((entry) => entry.id === view)!;
  const bluetooth = !webBluetoothTransport.unavailableReason();
  const usb = !webSerialTransport.unavailableReason();

  return (
    <div className="shell">
      <nav className="rail" aria-label="Sections">
        <div className="brand">
          <BrandMark />
          <div>
            <span className="brand__name">TotTag</span>
            <span className="brand__sub">Deployment tools</span>
          </div>
        </div>
        <div className="rail__links">
          {VIEWS.map((entry) => (
            <button key={entry.id} type="button" className="rail__link"
              aria-current={view === entry.id ? 'page' : undefined} onClick={() => setView(entry.id)}>
              {entry.icon}
              <span>{entry.label}</span>
              {entry.id === 'review' && logs.unsaved.length > 0 && (
                <span className="rail__badge" title="Downloaded logs not saved yet">{logs.unsaved.length}</span>
              )}
            </button>
          ))}
        </div>
        <div className="rail__foot">
          <p className="rail__note"><LockIcon /> Everything stays on this computer. Nothing is uploaded.</p>
          <p className="rail__note">
            {bluetooth || usb
              ? `This browser can reach tags over ${[bluetooth && 'Bluetooth', usb && 'USB'].filter(Boolean).join(' and ')}.`
              : 'This browser cannot reach tags. Use Chrome, Edge or Opera on a computer.'}
          </p>
        </div>
      </nav>

      <div className="main">
        <header className="topbar">
          <h1 className="topbar__title">{active.title}</h1>
          <div className="topbar__actions">
            {view === 'deploy' && (
              <ConfirmButton
                label="Start a new deployment"
                question="Start a new deployment? The dates, tags and labels are cleared. The time zone and daily hours are kept."
                confirmLabel="Start a new one"
                onConfirm={draftApi.startNew}
              />
            )}
            {view === 'review' && logs.reports.length > 0 && (
              <>
                <button type="button" className="button button--quiet" onClick={logs.exportCsv}>Export summary (CSV)</button>
                <button type="button" className="button button--quiet" onClick={logs.clear} disabled={logs.unsaved.length > 0}
                  title={logs.unsaved.length > 0 ? 'Save the downloaded logs first. Clearing would discard them.' : 'Close every log on this page'}>
                  Clear
                </button>
              </>
            )}
          </div>
        </header>

        <main className="content">
          {/* Every page stays mounted, so switching between them never discards what is on one. */}
          <div hidden={view !== 'deploy'}><Deploy draftApi={draftApi} /></div>
          <div hidden={view !== 'check'}>
            <CheckTag
              timezone={draft.timezone}
              inDeployment={(eui) => !!labelFor(eui) || draft.devices.some((d) => d.eui.every((b, i) => b === eui[i]))}
              onAddToDeployment={(eui) => addDevice({ eui, label: '' })}
            />
          </div>
          <div hidden={view !== 'review'}><Review logs={logs} timezone={draft.timezone} /></div>
          <div hidden={view !== 'radio'}>
            <RadioCheck reports={logs.reports} busy={logs.busy} onLoaded={(result) => void logs.onLoaded(result)}
              onOpenFolder={() => void logs.openFolder()} labelFor={labelFor} />
          </div>
        </main>
      </div>
    </div>
  );
}
