import { useEffect, useMemo, useRef, useState } from 'react';
import { RADIO_TEST_MAX_SECONDS, tagName } from '@tottag/schema';
import { webBluetoothTransport } from '../adapters/webBluetooth.ts';
import { LinkMatrix, PositionsPanel, RadioCard, VERDICT_ORDER, usePositions } from '../components/RadioResults.tsx';
import { LiveRadioTest, type LiveDevice, type LiveDeviceStatus } from '../features/liveRadioTest.ts';
import { formatPercent, runRadioCheck } from '../features/radioCheck.ts';
import { useKnownTags } from '../features/useKnownTags.ts';

const MIN_MINUTES = 2;
const MAX_MINUTES = Math.min(10, Math.floor(RADIO_TEST_MAX_SECONDS / 60));
const DEFAULT_MINUTES = 2;

const STATUS_LABEL: Record<LiveDeviceStatus, string> = {
  ready: 'Ready', starting: 'Starting', restarting: 'Restarting into the test', waiting: 'Being sent the tag list',
  running: 'Testing', reconnecting: 'Reconnecting', finished: 'Finished', failed: 'Failed',
};
/** A device the scheduler calls idle has not yet been given a slot, and is looking for a network to join. */
const roleLabel = (role: string) => {
  const name = role.replace(/^ROLE_/, '').toLowerCase().replace(/_/g, ' ');
  return name === 'idle' ? 'searching' : name;
};

const STATUS_CLASS: Record<LiveDeviceStatus, string> = {
  ready: 'note', starting: 'note', restarting: 'note', waiting: 'note', running: 'ok', reconnecting: 'warning', finished: 'ok', failed: 'error',
};

/**
 * The radio check run live: the devices range in a radio test while this page reads their ranges and radio counters
 * over Bluetooth. Nothing is logged, a device's deployment is left as it was, and every device goes back to normal by
 * itself when the test ends.
 */
export function LiveRadioCheck({ labelFor }: { labelFor: (eui: Uint8Array) => string | undefined }) {
  const unavailable = webBluetoothTransport.unavailableReason();
  const { tags: knownTags } = useKnownTags();
  const testRef = useRef<LiveRadioTest | null>(null);
  if (!testRef.current) testRef.current = new LiveRadioTest({ reconnect: (handle) => webBluetoothTransport.connect(handle) });
  const test = testRef.current;
  useEffect(() => () => test.dispose(), [test]);

  // Re-read the session whenever it reports a change, and every couple of seconds while ranges stream in
  const [version, setVersion] = useState(0);
  useEffect(() => test.subscribe(() => setVersion((v) => v + 1)), [test]);
  const state = test.state();
  const [tick, setTick] = useState(0);
  useEffect(() => {
    if (state.phase !== 'running') return;
    const timer = setInterval(() => setTick((t) => t + 1), 2000);
    return () => clearInterval(timer);
  }, [state.phase]);

  const [minutesText, setMinutesText] = useState(String(DEFAULT_MINUTES));
  const minutes = Number(minutesText);
  const minutesValid = Number.isInteger(minutes) && minutes >= MIN_MINUTES && minutes <= MAX_MINUTES;
  const [error, setError] = useState<string | null>(null);
  const [busy, setBusy] = useState(false);

  const addFromChooser = async () => {
    setError(null);
    try {
      const connection = await webBluetoothTransport.requestDevice();
      if (!connection) return;
      const refused = test.add(connection, labelFor(connection.identity.eui));
      if (refused) { setError(refused); void connection.disconnect(); }
    } catch (failure) {
      setError(failure instanceof Error ? failure.message : String(failure));
    }
  };

  const addKnown = async () => {
    setError(null);
    setBusy(true);
    const missing: string[] = [];
    try {
      for (const tag of knownTags.filter((known) => (known.transport ?? 'bluetooth') === 'bluetooth')) {
        if (state.devices.some((device) => device.handle === tag.handle)) continue;
        const connection = await webBluetoothTransport.connect(tag.handle);
        if (!connection) { missing.push(tagName(tag.eui)); continue; }
        const refused = test.add(connection, labelFor(Uint8Array.from(tag.eui)));
        if (refused) { missing.push(refused); void connection.disconnect(); }
      }
      if (missing.length) setError(`Could not add: ${missing.join(', ')}. A tag has to be on its charger, or in a running deployment, to be reached.`);
    } finally {
      setBusy(false);
    }
  };

  const start = async () => {
    if (!minutesValid) return;
    setError(null);
    setBusy(true);
    try { await test.start(minutes * 60); } finally { setBusy(false); }
  };

  const deployment = useMemo(() => test.deployment(), [test, version, tick]);
  const positions = usePositions(state.devices.length ? 'tottag.radioCheck.positions.live' : null);
  const result = useMemo(() => (deployment ? runRadioCheck(deployment, positions.positions) : null), [deployment, positions.positions]);
  const judged = result && result.windowStartMs !== null && result.windowEndMs !== null && result.windowEndMs > result.windowStartMs;
  const secondsLeft = state.endTime !== null && state.phase === 'running' ? Math.max(0, state.endTime - Math.floor(Date.now() / 1000)) : null;
  const truncated = state.devices.some((device) => device.truncated > 0);

  if (unavailable) return <p className="banner banner--warn">{unavailable}</p>;

  return (
    <>
      <section className="panel">
        <h2 className="panel__title">Check tags' radios live</h2>
        <p className="panel__note">
          Runs the tags in a short radio test and reads their distances and receive counts over Bluetooth as it goes,
          to find one with a weak receiver, a damaged antenna, or a distance calibration that is off. Nothing is
          recorded, a tag's deployment is left as it was, and every tag goes back to normal when the test ends.
        </p>
        <ol className="steps">
          <li>Put the tags on their chargers, or leave them running a deployment, so they can be reached.</li>
          <li>Add each tag. Tags this browser has seen before can be added all at once.</li>
          <li>Start the test. Each tag restarts into it and begins measuring, on its charger or off it.</li>
          <li>Set them where they can all see each other, at least half a metre apart. The test keeps running as they move.</li>
          <li>Verdicts appear within the first minute and update every 15 seconds; they firm up once every tag has a minute of data.</li>
        </ol>
        <div className="actions">
          <button type="button" className="button" disabled={state.phase !== 'setup' || busy} onClick={() => void addFromChooser()}>Add a tag</button>
          <button type="button" className="button button--quiet" disabled={state.phase !== 'setup' || busy || !knownTags.length || !webBluetoothTransport.reconnectAcrossReloadsSupported()}
            onClick={() => void addKnown()}>Add tags this browser knows</button>
          <label className="inline-field">
            Run for
            <input className="rtable__input" type="number" inputMode="numeric" min={MIN_MINUTES} max={MAX_MINUTES} step={1}
              value={minutesText} disabled={state.phase !== 'setup'} aria-label="Minutes to run the test for"
              onChange={(event) => setMinutesText(event.target.value)} /> minutes
          </label>
          {state.phase === 'setup' && (
            <button type="button" className="button" disabled={state.devices.length < 2 || busy || !minutesValid} onClick={() => void start()}>Start test</button>
          )}
          {(state.phase === 'starting' || state.phase === 'running') && (
            <button type="button" className="button button--quiet" onClick={() => void test.stop()}>Stop test</button>
          )}
        </div>
        {error && <p className="banner banner--crit">{error}</p>}
      </section>

      {state.devices.length > 0 && <DeviceTable devices={state.devices} secondsLeft={secondsLeft} finished={state.phase === 'finished'} onRemove={state.phase === 'setup' ? (handle) => test.remove(handle) : null} />}

      {truncated && (
        <p className="banner banner--warn">
          Some distance reports arrived cut short, because this computer's Bluetooth negotiated small packets. Rounds are
          still counted from each tag's own counter, but links to the tags cut off will read low.
        </p>
      )}

      {deployment && result && (judged ? (
        <>
          {result.notes.map((note) => <p key={note} className="banner banner--warn">{note}</p>)}
          <div className="cards">
            {[...result.devices]
              .sort((a, b) => VERDICT_ORDER[a.verdict] - VERDICT_ORDER[b.verdict] || a.label.localeCompare(b.label))
              .map((device) => <RadioCard key={device.uid} device={device} name={deployment.devices.find((d) => d.uid === device.uid)?.name ?? device.label} />)}
          </div>
          <LinkMatrix devices={deployment.devices} links={result.links} />
        </>
      ) : (
        <p className="panel__note">Verdicts appear once every tag has 15 seconds of data.</p>
      ))}

      {state.devices.length > 0 && (
        <PositionsPanel devices={state.devices.map((device) => ({ uid: device.uid, label: device.label, name: tagName(device.eui) }))} positions={positions} />
      )}
    </>
  );
}

function DeviceTable({ devices, secondsLeft, finished, onRemove }: {
  devices: readonly LiveDevice[];
  secondsLeft: number | null;
  finished: boolean;
  onRemove: ((handle: string) => void) | null;
}) {
  return (
    <section className="panel">
      <h2 className="panel__title">
        Tags{secondsLeft !== null && <> · {Math.floor(secondsLeft / 60)}:{String(secondsLeft % 60).padStart(2, '0')} left</>}
        {finished && <> · test over, each tag restarts back to normal</>}
      </h2>
      <table className="rtable">
        <thead>
          <tr><th>Tag</th><th>Status</th><th>Role</th><th>Rounds/s</th><th>Receives failed</th><th>By antenna</th><th /></tr>
        </thead>
        <tbody>
          {devices.map((device) => {
            const stats = device.stats;
            const total = stats ? stats.rxOk + stats.rxFailed : 0;
            // The antenna the device uses for schedules, join requests and status exchanges is bracketed
            const antennas = stats ? stats.rxFailedByAntenna.map((failed, i) => {
              const all = failed + stats.rxOkByAntenna[i]!;
              const rate = all ? formatPercent(failed / all) : '—';
              return i === stats.antenna ? `[${rate}]` : rate;
            }).join(' / ') : '—';
            return (
              <tr key={device.handle}>
                <td>{device.label}{device.label !== tagName(device.eui) && <span className="tag-id"> {tagName(device.eui)}</span>}</td>
                <td>
                  <span className={`badge badge--${STATUS_CLASS[device.status]}`}>{STATUS_LABEL[device.status]}</span>
                  {device.message && <div className="panel__note">{device.message}</div>}
                </td>
                <td>{stats ? roleLabel(stats.role) : '—'}</td>
                <td>{device.roundsPerSecond === null ? '—' : device.roundsPerSecond.toFixed(2)}</td>
                <td>{total ? formatPercent(stats!.rxFailed / total) : '—'}</td>
                <td>{antennas}</td>
                <td>{onRemove && <button type="button" className="link link--quiet" onClick={() => onRemove(device.handle)}>Remove</button>}</td>
              </tr>
            );
          })}
        </tbody>
      </table>
      {devices.some((device) => device.stats) && (
        <p className="panel__note">Brackets in the By antenna column mark the antenna that a tag listens for schedules on.</p>
      )}
    </section>
  );
}
