import { useCallback, useMemo, useState } from 'react';
import {
  buildManifest, diffReadback, encodeExperimentDetails, hasErrors, manifestToText,
  formatEui, validateDeployment, type DeviceWriteResult, type Problem,
} from '@tottag/schema';
import { webBluetoothTransport } from '../adapters/webBluetooth.ts';
import { webSerialTransport } from '../adapters/webSerial.ts';
import { downloadText } from '../adapters/download.ts';
import { useDeploymentDraft } from '../features/useDeploymentDraft.ts';
import { describeKnownTag, knownTagTransport, tagDisplayName, useKnownTags, type KnownTag } from '../features/useKnownTags.ts';
import type { TagTransport } from '../ports/tagTransport.ts';

type WriteState = 'idle' | 'writing' | 'done';

/** Long enough to find a tag in a bag, short enough not to be a nuisance. Matches the Python tool. */
const BUZZ_SECONDS = 10;

const TRANSPORTS: Record<TagTransport['id'], TagTransport> = {
  bluetooth: webBluetoothTransport,
  serial: webSerialTransport,
};

/** How to reach a tag again: which transport, and that transport's handle for it. */
interface TagRoute {
  readonly transport: TagTransport['id'];
  readonly handle: string;
}

/** Why a tag this browser knows how to reach did not answer, in terms of what to do about it. */
function unreachable(route: TagRoute): string {
  if (route.transport === 'serial') return 'Could not reach this tag. It needs to be plugged into this computer over USB.';
  return webBluetoothTransport.reconnectAcrossReloadsSupported()
    ? 'Could not reach this tag. It needs to be powered and within range.'
    : 'This browser cannot reconnect to a tag added before the page was reloaded. Remove it and add it again.';
}

interface DeviceProgress {
  readonly label: string;
  readonly state: 'pending' | 'writing' | 'confirmed' | 'mismatch' | 'failed';
  readonly detail?: string;
}

const localInput = (unixSeconds: number): string => {
  const date = new Date(unixSeconds * 1000);
  const pad = (value: number) => String(value).padStart(2, '0');
  return `${date.getFullYear()}-${pad(date.getMonth() + 1)}-${pad(date.getDate())}T${pad(date.getHours())}:${pad(date.getMinutes())}`;
};
const fromLocalInput = (value: string): number => Math.floor(new Date(value).getTime() / 1000);

const timeOfDayInput = (seconds: number): string =>
  `${String(Math.floor(seconds / 3600)).padStart(2, '0')}:${String(Math.floor((seconds % 3600) / 60)).padStart(2, '0')}`;
const fromTimeOfDay = (value: string): number => {
  const [h = '0', m = '0'] = value.split(':');
  return Number(h) * 3600 + Number(m) * 60;
};

export function Deploy() {
  const { config, update, addDevice, updateDevice, removeDevice, reset } = useDeploymentDraft();
  const { tags: knownTags, remember, forget } = useKnownTags();
  const [showKnown, setShowKnown] = useState(false);
  // EUI (as hex) -> how to reach it. A tag is only connected while something is being done to it, so
  // what is kept here is how to REACH each tag, not an open link to it.
  const [handles, setHandles] = useState<Map<string, TagRoute>>(new Map());
  const [writeState, setWriteState] = useState<WriteState>('idle');
  const [progress, setProgress] = useState<DeviceProgress[]>([]);
  const [error, setError] = useState<string | null>(null);
  const [manifestReady, setManifestReady] = useState<{ json: string; text: string; name: string } | null>(null);

  const problems = useMemo(() => validateDeployment(config), [config]);
  const blocked = hasErrors(problems);
  const bluetoothReason = webBluetoothTransport.unavailableReason();
  const serialReason = webSerialTransport.unavailableReason();
  const noTransport = !!bluetoothReason && !!serialReason;
  const nameOf = useCallback((eui: Uint8Array): string => {
    const route = handles.get(formatEui(eui));
    return route ? tagDisplayName(eui, route.transport) : formatEui(eui);
  }, [handles]);

  const problemsFor = useCallback(
    (index: number): Problem[] => problems.filter((problem) => problem.devices.includes(index)),
    [problems],
  );

  const addNewTag = useCallback(async (transport: TagTransport) => {
    setError(null);
    try {
      // Which tag was picked cannot be known before connecting: over Bluetooth the chooser shows only
      // advertised names and the EUI comes from GATT System ID; over USB every tag is the same kind
      // of port and the EUI has to be asked for.
      const connection = await transport.requestDevice();
      if (!connection) return;
      const { identity } = connection;
      // Nothing more is wanted from it right now, and a tag accepts only two connections.
      await connection.disconnect();
      const key = formatEui(identity.eui);
      const known = knownTags.find((tag) => tag.handle === identity.handle);
      remember(identity, known?.lastLabel ?? '');
      setHandles((current) => new Map(current).set(key, { transport: identity.transport, handle: identity.handle }));
      addDevice({ eui: identity.eui, label: known?.lastLabel ?? '' });
    } catch (caught) {
      setError(caught instanceof Error ? caught.message : String(caught));
    }
  }, [addDevice, knownTags, remember]);

  /**
   * Sound a tag's buzzer so it can be told apart from the others.
   *
   * Explicit, never automatic: these are worn by children in a study, and a device that makes a
   * noise nobody asked for is a device that gets switched off. It is here because every tag
   * advertises the same name, so after adding one there is otherwise no way to confirm which
   * physical tag it was.
   */
  const buzz = useCallback(async (device: { eui: Uint8Array }) => {
    setError(null);
    const route = handles.get(formatEui(device.eui));
    if (!route) {
      setError('That tag has not been added from this browser, so it cannot be reached.');
      return;
    }
    let connection = null;
    try {
      connection = await TRANSPORTS[route.transport].connect(route.handle);
      if (!connection) {
        setError(unreachable(route));
        return;
      }
      await connection.locate(BUZZ_SECONDS);
    } catch (caught) {
      setError(caught instanceof Error ? caught.message : String(caught));
    } finally {
      await connection?.disconnect();
    }
  }, [handles]);

  const addKnownTag = useCallback((tag: KnownTag) => {
    const eui = Uint8Array.from(tag.eui);
    setHandles((current) => new Map(current).set(formatEui(eui), { transport: knownTagTransport(tag), handle: tag.handle }));
    addDevice({ eui, label: tag.lastLabel });
  }, [addDevice]);

  const write = useCallback(async () => {
    setWriteState('writing');
    setError(null);
    setManifestReady(null);
    const results: DeviceWriteResult[] = [];
    const running: DeviceProgress[] = config.devices.map((device) => ({
      label: device.label || nameOf(device.eui),
      state: 'pending',
    }));
    setProgress([...running]);

    for (let index = 0; index < config.devices.length; index += 1) {
      const device = config.devices[index]!;
      const name = device.label || nameOf(device.eui);
      running[index] = { label: name, state: 'writing' };
      setProgress([...running]);

      const route = handles.get(formatEui(device.eui));
      const connection = route ? await TRANSPORTS[route.transport].connect(route.handle) : null;
      if (!connection) {
        const detail = !route
          ? 'This tag has not been added from this browser, so it cannot be reached. Add it again.'
          : unreachable(route);
        running[index] = { label: name, state: 'failed', detail };
        results.push({ eui: device.eui, label: device.label, outcome: 'failed', detail });
        setProgress([...running]);
        continue;
      }

      try {
        await connection.writeDeployment(config);
        // Read back rather than trusting the write. A device that silently stored something else is
        // precisely what the manifest exists to catch, and it can only be caught here.
        const readback = await connection.readExperiment();
        const differences = diffReadback(config, readback);
        if (differences.length === 0) {
          running[index] = { label: name, state: 'confirmed' };
          results.push({
            eui: device.eui, label: device.label, outcome: 'confirmed',
            firmware: connection.identity.firmware ?? undefined,
            hardware: connection.identity.hardware ?? undefined,
          });
        } else {
          running[index] = { label: name, state: 'mismatch', detail: differences.join('; ') };
          results.push({ eui: device.eui, label: device.label, outcome: 'mismatch', detail: differences.join('; ') });
        }
      } catch (caught) {
        const detail = caught instanceof Error ? caught.message : String(caught);
        running[index] = { label: name, state: 'failed', detail };
        results.push({ eui: device.eui, label: device.label, outcome: 'failed', detail });
      } finally {
        // One tag at a time, exactly as the desktop tool does it: connect, write, disconnect. A tag
        // accepts two Bluetooth connections at most, and leaving links open across a ten-tag run
        // would exhaust that for no benefit. A USB port held open would block every other program.
        await connection.disconnect();
      }
      setProgress([...running]);
    }

    const manifest = buildManifest(config, encodeExperimentDetails(config), results, new Date());
    const stamp = new Date().toISOString().slice(0, 19).replace(/[:T]/g, '-');
    setManifestReady({
      json: JSON.stringify(manifest, null, 2),
      text: manifestToText(manifest),
      name: `tottag-deployment-${stamp}`,
    });
    setWriteState('done');
  }, [config, handles, nameOf]);

  const generalProblems = problems.filter((problem) => problem.devices.length === 0);

  return (
    <div className="deploy">
      {noTransport && (
        <p className="banner banner--warning">
          <strong>Neither Bluetooth nor USB is available here.</strong> {bluetoothReason} You can still
          build a deployment and save its manifest, but nothing can be written to a tag from this browser.
        </p>
      )}

      <section className="panel">
        <h2 className="panel__title">When</h2>
        <div className="fields">
          <label className="field">
            <span className="field__label">Starts</span>
            <input type="datetime-local" value={localInput(config.startTime)}
              onChange={(event) => update({ startTime: fromLocalInput(event.target.value) })} />
          </label>
          <label className="field">
            <span className="field__label">Ends</span>
            <input type="datetime-local" value={localInput(config.endTime)}
              onChange={(event) => update({ endTime: fromLocalInput(event.target.value) })} />
          </label>
          <label className="field">
            <span className="field__label">Times entered in</span>
            <input type="text" value={config.timezone} onChange={(event) => update({ timezone: event.target.value })} />
          </label>
        </div>
        <label className="checkbox">
          <input type="checkbox" checked={config.useDailyTimes}
            onChange={(event) => update({ useDailyTimes: event.target.checked })} />
          <span>Only record during part of each day</span>
        </label>
        {config.useDailyTimes && (
          <div className="fields">
            <label className="field">
              <span className="field__label">Daily start (UTC)</span>
              <input type="time" value={timeOfDayInput(config.dailyStartTime)}
                onChange={(event) => update({ dailyStartTime: fromTimeOfDay(event.target.value) })} />
            </label>
            <label className="field">
              <span className="field__label">Daily end (UTC)</span>
              <input type="time" value={timeOfDayInput(config.dailyEndTime)}
                onChange={(event) => update({ dailyEndTime: fromTimeOfDay(event.target.value) })} />
            </label>
            <p className="field__note">
              The tag compares these against its own clock, which runs in UTC — so these are UTC
              times, not local ones. A window that runs past midnight is fine.
            </p>
          </div>
        )}
      </section>

      <section className="panel">
        <h2 className="panel__title">Which tags</h2>
        <p className="panel__note">
          The browser asks about tags one at a time and gives no way for a page to list what is
          nearby, so each tag is added individually the first time. After that it is remembered and
          can be added straight from the list. Tags appear in the chooser as <code>TotTag-XX</code>,
          where XX is the last byte of the address; on firmware older than that they all show as
          plain <code>TotTag</code>, and the only way to tell which one you picked is the address
          shown after it is added. A tag plugged into this computer can be added over USB instead,
          and is written to the same way; it shows as <code>USB-Connected</code> followed by its address.
        </p>
        <div className="panel__actions">
          <button type="button" className="button" onClick={() => void addNewTag(webBluetoothTransport)}
            disabled={!!bluetoothReason || writeState === 'writing'} title={bluetoothReason ?? undefined}>
            Add a tag
          </button>
          <button type="button" className="button button--quiet" onClick={() => void addNewTag(webSerialTransport)}
            disabled={!!serialReason || writeState === 'writing'} title={serialReason ?? undefined}>
            Add a USB-connected tag
          </button>
          {knownTags.length > 0 && (
            <button type="button" className="button button--quiet" onClick={() => setShowKnown((open) => !open)}
              disabled={writeState === 'writing'}>
              {showKnown ? 'Hide known tags' : `Add a known tag (${knownTags.length})`}
            </button>
          )}
        </div>

        {showKnown && (
          <>
          <p className="known__heading">Tags this browser has seen before</p>
          <ul className="known">
            {knownTags.map((tag) => {
              const already = config.devices.some((device) => formatEui(device.eui) === formatEui(Uint8Array.from(tag.eui)));
              return (
                <li key={tag.handle} className="known__row">
                  <code className="known__eui">{describeKnownTag(tag)}</code>
                  {already ? (
                    <span className="known__state">already in this deployment</span>
                  ) : (
                    <button type="button" className="link" onClick={() => addKnownTag(tag)}>Add</button>
                  )}
                  <button type="button" className="link known__forget" onClick={() => forget(tag.handle)}>Forget</button>
                </li>
              );
            })}
          </ul>
          </>
        )}

        {config.devices.length === 0 ? (
          <p className="empty">No tags in this deployment yet.</p>
        ) : (
          <ul className="devices">
            {config.devices.map((device, index) => {
              const issues = problemsFor(index);
              const worst = issues.some((issue) => issue.severity === 'error') ? 'error' : issues.length ? 'warning' : null;
              // Whether this browser knows how to reach the tag, not whether a link is open — one
              // is only opened while something is being written.
              const reachable = handles.has(formatEui(device.eui));
              return (
                <li key={formatEui(device.eui)} className={`device${worst ? ` device--${worst}` : ''}`}>
                  <div className="device__head">
                    <code className="device__eui">{nameOf(device.eui)}</code>
                    <span className="device__short" title="The byte tags identify each other by">
                      ends {(device.eui[0] ?? 0).toString(16).padStart(2, '0').toUpperCase()}
                    </span>
                    <span className={`device__link device__link--${reachable ? 'on' : 'off'}`}>
                      {reachable ? 'ready' : 'not added from this browser'}
                    </span>
                    {reachable && (
                      <button type="button" className="link" title="Sound this tag's buzzer so you can tell which one it is"
                        onClick={() => void buzz(device)} disabled={writeState === 'writing'}>
                        Buzz
                      </button>
                    )}
                    <button type="button" className="link" onClick={() => removeDevice(index)}>Remove</button>
                  </div>
                  <input className="device__label" type="text" placeholder="Label, e.g. 10043_CG1"
                    value={device.label} onChange={(event) => updateDevice(index, { label: event.target.value })} />
                  {issues.map((issue) => (
                    <p key={issue.code} className={`device__issue device__issue--${issue.severity}`}>{issue.message}</p>
                  ))}
                </li>
              );
            })}
          </ul>
        )}
      </section>

      {generalProblems.length > 0 && (
        <ul className="findings">
          {generalProblems.map((problem) => (
            <li key={problem.code} className={`finding finding--${problem.severity === 'error' ? 'warning' : 'note'}`}>
              <span className="finding__tag">{problem.severity === 'error' ? 'Fix' : 'Note'}</span>
              <span className="finding__text">{problem.message}</span>
            </li>
          ))}
        </ul>
      )}

      <section className="panel">
        <div className="panel__actions">
          <button type="button" className="button" onClick={() => void write()}
            disabled={blocked || noTransport || writeState === 'writing' || config.devices.length === 0}>
            {writeState === 'writing' ? 'Writing…' : 'Write to tags'}
          </button>
          <button type="button" className="button button--quiet" onClick={reset} disabled={writeState === 'writing'}>
            Start over
          </button>
        </div>
        {blocked && <p className="panel__note">Fix the problems above before writing.</p>}
        {error && <p className="banner banner--error">{error}</p>}

        {progress.length > 0 && (
          <ul className="progress">
            {progress.map((entry) => (
              <li key={entry.label} className={`progress__row progress__row--${entry.state}`}>
                <span className="progress__name">{entry.label}</span>
                <span className="progress__state">{entry.state}</span>
                {entry.detail && <span className="progress__detail">{entry.detail}</span>}
              </li>
            ))}
          </ul>
        )}

        {manifestReady && (
          <div className="manifest">
            <p>
              <strong>Save the manifest.</strong> It records exactly what each tag confirmed it
              stored, so that months from now there is an answer to &ldquo;what did we actually
              deploy?&rdquo; that does not depend on the tags or on anyone&rsquo;s memory.
            </p>
            <div className="panel__actions">
              <button type="button" className="button"
                onClick={() => void downloadText(`${manifestReady.name}.json`, manifestReady.json)}>
                Save manifest (JSON)
              </button>
              <button type="button" className="button button--quiet"
                onClick={() => void downloadText(`${manifestReady.name}.txt`, manifestReady.text)}>
                Save as text
              </button>
            </div>
          </div>
        )}
      </section>
    </div>
  );
}
