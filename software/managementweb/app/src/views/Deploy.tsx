import { useCallback, useEffect, useMemo, useRef, useState } from 'react';
import {
  buildManifest, diffReadback, encodeExperimentDetails, formatDate, manifestToText, MAX_NUM_RANGING_DEVICES,
  tagName, unixToWallClock, type ClockChangePolicy, type DeviceWriteResult, type Problem, type WriteOutcome,
} from '@tottag/schema';
import { webBluetoothTransport } from '../adapters/webBluetooth.ts';
import { webSerialTransport } from '../adapters/webSerial.ts';
import { downloadText } from '../adapters/download.ts';
import { DateTimeField, Field, TimeField, TimezoneField } from '../components/Fields.tsx';
import { TagPills } from '../components/TagFacts.tsx';
import { describeSchedule, draftProblems, draftToConfig, recordBaseName, type DraftDevice } from '../features/schedule.ts';
import { assessSnapshot, batteryLow, replaceDecision, storedFromDetails, type ReplaceDecision, type TagCheck } from '../features/tagStatus.ts';
import type { DraftApi } from '../features/useDeploymentDraft.ts';
import { describeKnownTag, routeFor, useKnownTags, type KnownTag } from '../features/useKnownTags.ts';
import { useDownloaded, useLastRecord, useTagChecks } from '../features/useTagRecords.ts';
import { errorText, reachableWithoutChooser, transportFor, unreachableMessage, withTag } from '../lib/tagAccess.ts';
import { useNow } from '../lib/useNow.ts';
import type { TagTransport } from '../ports/tagTransport.ts';

/** Long enough to find a tag in a bag, short enough not to be a nuisance. Matches the Python tool. */
const BUZZ_SECONDS = 10;
const LABEL_MAX_BYTES = 16;

type ProgressState = 'waiting' | 'checking' | 'writing' | WriteOutcome;

interface ProgressEntry {
  readonly key: string;
  readonly name: string;
  readonly label: string;
  readonly state: ProgressState;
  readonly detail?: string | undefined;
}

const PROGRESS_WORDS: Record<ProgressState, string> = {
  waiting: 'Waiting', checking: 'Checking…', writing: 'Writing…', confirmed: 'Done', mismatch: 'Did not match',
  unconfirmed: 'Not checked', failed: 'Not written', skipped: 'Left alone',
};
const PROGRESS_TONE: Record<ProgressState, string> = {
  waiting: 'quiet', checking: 'info', writing: 'info', confirmed: 'ok', mismatch: 'crit', unconfirmed: 'warn', failed: 'crit', skipped: 'warn',
};

const keyOf = (eui: Uint8Array) => Array.from(eui).join('.');
const sameEui = (a: Uint8Array | readonly number[], b: Uint8Array | readonly number[]) => a.length === b.length && Array.from(a).every((x, i) => x === b[i]);
const labelBytes = (label: string) => new TextEncoder().encode(label).length;

interface Notice { readonly tone: 'info' | 'error'; readonly text: string }

export function Deploy({ draftApi }: { draftApi: DraftApi }) {
  const { draft, generation, update, addDevice, updateDevice, removeDevice } = draftApi;
  const { tags: knownTags, remember, forget } = useKnownTags();
  const checks = useTagChecks();
  const { downloadedAt } = useDownloaded();
  const { record, setRecord, markSaved } = useLastRecord();
  const now = useNow();

  const [notice, setNotice] = useState<Notice | null>(null);
  const [busyKey, setBusyKey] = useState<string | null>(null);
  const [writing, setWriting] = useState(false);
  const [progress, setProgress] = useState<ProgressEntry[]>([]);
  const [showKnown, setShowKnown] = useState(false);

  // A new deployment leaves nothing on screen describing the last one, except its record until saved.
  useEffect(() => { setProgress([]); setNotice(null); }, [generation]);

  // The write loop runs for a while; it reads the latest known tags rather than the ones from when it began.
  const knownRef = useRef(knownTags);
  knownRef.current = knownTags;

  const config = useMemo(() => draftToConfig(draft), [draft]);
  const problems = useMemo(() => draftProblems(draft, now), [draft, now]);
  const summary = useMemo(() => describeSchedule(draft), [draft]);
  const bluetoothReason = webBluetoothTransport.unavailableReason();
  const serialReason = webSerialTransport.unavailableReason();
  const noTransport = !!bluetoothReason && !!serialReason;

  const downloadedFor = useCallback((eui: Uint8Array, check: TagCheck | null) =>
    check?.stored ? downloadedAt(eui[0] ?? 0, check.stored.startTime) : null, [downloadedAt]);

  const rows = draft.devices.map((device, index) => {
    const route = routeFor(knownTags, device.eui);
    const check = checks.get(device.eui);
    const decision = replaceDecision(check, config, now, !!downloadedFor(device.eui, check), device.replaceAllowed);
    return {
      device, index, route, check, decision,
      reachable: reachableWithoutChooser(route),
      issues: problems.filter((problem) => problem.devices.includes(index)),
    };
  });

  const generalProblems = problems.filter((problem) => problem.devices.length === 0);
  const blockers: string[] = [
    ...problems.filter((p) => p.severity === 'error' && p.devices.length === 0).map((p) => p.message),
    ...[...new Set(problems.filter((p) => p.severity === 'error' && p.devices.length > 0).map((p) => p.message))],
    ...(noTransport ? [] : rows.filter((row) => !row.reachable).map((row) => `Reconnect ${tagName(row.device.eui)}: after a reload, this browser needs you to pick it again.`)),
    ...rows.filter((row) => row.decision === 'ask').map((row) => `${tagName(row.device.eui)} still holds a deployment whose data has not been saved here. Download it first, or say it may be replaced.`),
  ];
  const unlabelled = problems.filter((p) => p.code === 'label-missing').length;
  const cautions: string[] = [
    ...generalProblems.filter((p) => p.severity === 'warning').map((p) => p.message),
    ...(unlabelled ? [`${unlabelled} tag${unlabelled === 1 ? ' has' : 's have'} no label yet.`] : []),
    ...rows.filter((row) => batteryLow(row.check?.batteryMv)).map((row) => `${tagName(row.device.eui)}'s battery is low. Leave it charging before it goes out.`),
    ...(summary.clockChange ? [`The clocks change on ${summary.clockChange.day}. Check the recording hours under "When to record".`] : []),
  ];

  // --- Talking to tags ---------------------------------------------------------------------------

  const recordCheckFrom = useCallback(async (connection: Parameters<Parameters<typeof withTag>[1]>[0]) => {
    const snapshot = await connection.readSnapshot();
    const check = assessSnapshot(snapshot, connection.identity.firmware, Date.now() / 1000);
    checks.record(connection.identity.eui, check);
    return check;
  }, [checks]);

  const addNewTag = useCallback(async (transport: TagTransport) => {
    setNotice(null);
    try {
      // Which tag was picked cannot be known before connecting: over Bluetooth the chooser shows only
      // advertised names and the address comes from GATT System ID; over USB it has to be asked for.
      const connection = await transport.requestDevice();
      if (!connection) return;
      try {
        const { identity } = connection;
        remember(identity);
        // Read it while the link is open anyway, so its row says straight away what state it is in.
        await recordCheckFrom(connection).catch(() => null);
        if (draft.devices.some((device) => sameEui(device.eui, identity.eui))) {
          setNotice({ tone: 'info', text: `${tagName(identity.eui)} is already in this deployment.` });
        } else {
          addDevice({ eui: identity.eui, label: '' });
        }
      } finally {
        // A tag accepts only two connections, and nothing more is wanted from it right now.
        await connection.disconnect();
      }
    } catch (caught) {
      setNotice({ tone: 'error', text: errorText(caught) });
    }
  }, [addDevice, draft.devices, recordCheckFrom, remember]);

  const addKnownTag = useCallback((tag: KnownTag) => {
    setNotice(null);
    addDevice({ eui: Uint8Array.from(tag.eui), label: '' });
  }, [addDevice]);

  /** Pick a tag in the chooser again, for a row this browser can no longer reach on its own. */
  const reconnect = useCallback(async (device: DraftDevice, route: KnownTag | null) => {
    setNotice(null);
    const transport = route ? transportFor(route) : webBluetoothTransport;
    setBusyKey(keyOf(device.eui));
    try {
      const connection = await transport.requestDevice();
      if (!connection) return;
      try {
        remember(connection.identity);
        await recordCheckFrom(connection).catch(() => null);
        if (!sameEui(connection.identity.eui, device.eui)) {
          setNotice({ tone: 'error', text: `That was ${tagName(connection.identity.eui)}, not ${tagName(device.eui)}. Try again and pick TotTag-${tagName(device.eui).slice(-2)} in the list.` });
        }
      } finally {
        await connection.disconnect();
      }
    } catch (caught) {
      setNotice({ tone: 'error', text: errorText(caught) });
    } finally {
      setBusyKey(null);
    }
  }, [recordCheckFrom, remember]);

  /** Read a tag's battery, clock and deployment again. */
  const recheck = useCallback(async (device: DraftDevice, route: KnownTag | null) => {
    setNotice(null);
    if (!route) { setNotice({ tone: 'error', text: unreachableMessage(device.eui, route) }); return; }
    setBusyKey(keyOf(device.eui));
    try {
      if (!(await withTag(route, recordCheckFrom))) setNotice({ tone: 'error', text: unreachableMessage(device.eui, route) });
    } catch (caught) {
      setNotice({ tone: 'error', text: errorText(caught) });
    } finally {
      setBusyKey(null);
    }
  }, [recordCheckFrom]);

  /**
   * Sound a tag's buzzer so it can be told apart from the others.
   *
   * Explicit, never automatic: these are worn by children in a study, and a device that makes a
   * noise nobody asked for is a device that gets switched off.
   */
  const buzz = useCallback(async (device: DraftDevice, route: KnownTag | null) => {
    setNotice(null);
    if (!route) { setNotice({ tone: 'error', text: unreachableMessage(device.eui, route) }); return; }
    setBusyKey(keyOf(device.eui));
    try {
      if (!(await withTag(route, (connection) => connection.locate(BUZZ_SECONDS)))) {
        setNotice({ tone: 'error', text: unreachableMessage(device.eui, route) });
      }
    } catch (caught) {
      setNotice({ tone: 'error', text: errorText(caught) });
    } finally {
      setBusyKey(null);
    }
  }, []);

  const write = useCallback(async () => {
    setWriting(true);
    setNotice(null);
    const entries: ProgressEntry[] = draft.devices.map((device) => ({
      key: keyOf(device.eui), name: tagName(device.eui), label: device.label, state: 'waiting',
    }));
    setProgress([...entries]);
    const set = (index: number, patch: Partial<ProgressEntry>) => {
      entries[index] = { ...entries[index]!, ...patch };
      setProgress([...entries]);
    };
    const results: DeviceWriteResult[] = [];

    try {
      for (let index = 0; index < draft.devices.length; index += 1) {
        const device = draft.devices[index]!;
        set(index, { state: 'checking' });
        const route = routeFor(knownRef.current, device.eui);
        let result: Omit<DeviceWriteResult, 'eui' | 'label'>;
        try {
          const attempt = route ? await withTag(route, async (connection) => {
            // Checked again here, not trusted from when the tag was added: it may have been set up from
            // another computer since, and replacing a deployment is the one step that can lose data.
            const check = await recordCheckFrom(connection);
            const at = Date.now() / 1000;
            const decision = replaceDecision(check, config, at, !!downloadedFor(device.eui, check), device.replaceAllowed);
            if (decision === 'ask') {
              const stored = check.stored!;
              return {
                outcome: 'skipped' as const,
                batteryMv: check.batteryMv,
                detail: `It still holds a deployment from ${formatDate(stored.startTime, draft.timezone)} to ${formatDate(stored.endTime, draft.timezone)} ` +
                  'whose data has not been saved from this browser. Download it first, or tick "OK to replace" on its row, then write again.',
              };
            }
            set(index, { state: 'writing' });
            await connection.writeDeployment(config);
            // Read back rather than trusting the write. A tag that silently stored something else is
            // precisely what the record exists to catch, and it can only be caught here.
            const readback = await connection.readExperiment();
            checks.record(device.eui, { ...check, checkedAt: Math.floor(Date.now() / 1000), stored: storedFromDetails(readback) });
            const differences = diffReadback(config, readback);
            return differences.length
              ? { outcome: 'mismatch' as const, detail: differences.join(' '), batteryMv: check.batteryMv }
              : {
                  outcome: 'confirmed' as const, batteryMv: check.batteryMv,
                  firmware: connection.identity.firmware ?? undefined, hardware: connection.identity.hardware ?? undefined,
                };
          }) : null;
          result = attempt ? attempt.value : { outcome: 'failed', detail: unreachableMessage(device.eui, route) };
        } catch (caught) {
          result = { outcome: 'failed', detail: errorText(caught) };
        }
        results.push({ eui: device.eui, label: device.label, ...result });
        set(index, { state: result.outcome, detail: result.detail ?? undefined });
      }

      const manifest = buildManifest(config, encodeExperimentDetails(config), results, new Date(), draft.name);
      const day = unixToWallClock(Date.now() / 1000, draft.timezone).slice(0, 10);
      setRecord({
        base: `${recordBaseName(draft)}-${day}`,
        json: JSON.stringify(manifest, null, 2),
        text: manifestToText(manifest),
        writtenAt: manifest.writtenAt,
        saved: false,
      });
      const confirmed = results.filter((r) => r.outcome === 'confirmed').length;
      setNotice(confirmed === results.length
        ? { tone: 'info', text: `All ${confirmed} tags confirmed the deployment. Save the record below.` }
        : { tone: 'error', text: `${confirmed} of ${results.length} tags confirmed the deployment. See below for the others, fix what they need, and write again.` });
    } catch (caught) {
      // Nothing above is expected to throw, but if it does the page must not be left stuck on "Writing…".
      setNotice({ tone: 'error', text: `Writing stopped: ${errorText(caught)}` });
    } finally {
      setWriting(false);
    }
  }, [checks, config, downloadedFor, draft, recordCheckFrom, setRecord]);

  const saveRecord = useCallback(async (kind: 'json' | 'txt') => {
    if (!record) return;
    try {
      if (await downloadText(`${record.base}.${kind}`, kind === 'json' ? record.json : record.text)) markSaved();
    } catch (caught) {
      setNotice({ tone: 'error', text: `Could not save the record: ${errorText(caught)}` });
    }
  }, [markSaved, record]);

  // --- Rendering ------------------------------------------------------------------------------------

  const timeError = (code: string) => problems.find((p) => p.code === code)?.message;
  const dailyError = draft.useDailyTimes ? problems.find((p) => p.code === 'bad-daily-time' || p.code === 'empty-daily-window')?.message : null;
  const canWrite = !writing && !noTransport && draft.devices.length > 0 && blockers.length === 0;
  const tagCount = `${draft.devices.length} tag${draft.devices.length === 1 ? '' : 's'}`;

  return (
    <div className="split">
      <div className="split__main">
        {noTransport && (
          <p className="banner banner--warn">
            <strong>This browser cannot talk to tags.</strong> Use Chrome, Edge or Opera on a computer to write a
            deployment. You can still plan one here; it is kept in this browser.
          </p>
        )}

        <section className="panel">
          <h2 className="panel__title">About this deployment</h2>
          <div className="fields fields--2">
            <Field label="Name (optional)" htmlFor="deployment-name"
              hint="For your own records. It names the deployment record file and is not stored on the tags.">
              <input id="deployment-name" className="input" value={draft.name} placeholder="For example, Family 10051"
                onChange={(event) => update({ name: event.target.value })} />
            </Field>
            <TimezoneField value={draft.timezone} onChange={(timezone) => update({ timezone })} />
          </div>
        </section>

        <section className="panel">
          <h2 className="panel__title">When to record</h2>
          <div className="fields fields--2">
            <DateTimeField label="Starts" value={draft.start} onChange={(start) => update({ start })}
              error={timeError('missing-start')} />
            <DateTimeField label="Ends" value={draft.end} onChange={(end) => update({ end })}
              error={timeError('missing-end') ?? timeError('end-before-start') ?? timeError('too-long') ?? timeError('already-over')} />
          </div>
          <label className="toggle">
            <input type="checkbox" checked={draft.useDailyTimes} onChange={(event) => update({ useDailyTimes: event.target.checked })} />
            <span>Only record during certain hours each day</span>
          </label>
          {draft.useDailyTimes && (
            <>
              <div className="fields fields--2">
                <TimeField label="Each day from" value={draft.dailyStart} onChange={(dailyStart) => update({ dailyStart })} error={dailyError} />
                <TimeField label="Until" value={draft.dailyEnd} onChange={(dailyEnd) => update({ dailyEnd })} />
              </div>
              <p className="hint">
                Hours on the study's own clock. Hours that run past midnight, such as 10:00 PM to 6:00 AM, record overnight.
              </p>
            </>
          )}
          {summary.clockChange && (
            <ClockChangeChoice notice={summary.clockChange} value={draft.clockChangePolicy}
              onChange={(clockChangePolicy) => update({ clockChangePolicy })} />
          )}
        </section>

        <section className="panel">
          <div className="panel__head">
            <h2 className="panel__title">Tags</h2>
            <span className="panel__count">{draft.devices.length} of {MAX_NUM_RANGING_DEVICES}</span>
          </div>
          <ol className="steps">
            <li><strong>Put every tag on its charger.</strong> A tag can only be reached while it is charging.</li>
            <li>Click <strong>Add a tag</strong> and pick <code>TotTag-XX</code> from the list, where XX is the end of the ID on the tag's sticker.</li>
            <li>Use <strong>Buzz</strong> to make sure you picked the tag you meant, then give each one a label.</li>
          </ol>
          <div className="actions">
            <button type="button" className="button" onClick={() => void addNewTag(webBluetoothTransport)}
              disabled={!!bluetoothReason || writing} title={bluetoothReason ?? undefined}>Add a tag</button>
            <button type="button" className="button button--quiet" onClick={() => void addNewTag(webSerialTransport)}
              disabled={!!serialReason || writing} title={serialReason ?? 'For a tag plugged into this computer with a USB cable'}>
              Add a tag plugged in by USB
            </button>
            {knownTags.length > 0 && (
              <button type="button" className="button button--quiet" aria-expanded={showKnown}
                onClick={() => setShowKnown((open) => !open)} disabled={writing}>
                {showKnown ? 'Hide tags this browser knows' : `Add a tag this browser knows (${knownTags.length})`}
              </button>
            )}
          </div>

          {showKnown && (
            <ul className="chips">
              {knownTags.map((tag) => {
                const already = draft.devices.some((device) => sameEui(device.eui, tag.eui));
                return (
                  <li key={tag.handle} className={`chip${already ? ' chip--on' : ''}`}>
                    <span className="chip__name">{describeKnownTag(tag)}</span>
                    {already
                      ? <span className="chip__state">in this deployment</span>
                      : <button type="button" className="link" onClick={() => addKnownTag(tag)}>Add</button>}
                    <button type="button" className="link link--quiet" onClick={() => forget(tag.handle)}
                      title="Stop remembering this tag in this browser">Forget</button>
                  </li>
                );
              })}
            </ul>
          )}

          {notice && <p className={`banner banner--${notice.tone === 'error' ? 'crit' : 'info'}`} role="status">{notice.text}</p>}

          {draft.devices.length === 0 ? (
            <p className="empty">No tags yet. Add the tags taking part, one at a time.</p>
          ) : (
            <ul className="tags">
              {rows.map(({ device, route, check, decision, reachable, issues }) => {
                const key = keyOf(device.eui);
                const worst = issues.some((issue) => issue.severity === 'error') || decision === 'ask' ? 'crit'
                  : issues.length || batteryLow(check?.batteryMv) ? 'warn' : null;
                const bytes = labelBytes(device.label);
                const busy = busyKey === key || writing;
                return (
                  <li key={key} className={`tag${worst ? ` tag--${worst}` : ''}`}>
                    <div className="tag__main">
                      <span className="tag__name" title={`Full address ${Array.from(device.eui).reverse().map((b) => b.toString(16).padStart(2, '0').toUpperCase()).join(':')}`}>
                        {tagName(device.eui)}{route?.transport === 'serial' ? <small> USB</small> : null}
                      </span>
                      <div className="tag__label">
                        <input className="input" type="text" placeholder="Label, e.g. 10043_CG1" value={device.label}
                          aria-label={`Label for ${tagName(device.eui)}`} spellCheck={false}
                          onChange={(event) => updateDevice(device.eui, { label: event.target.value })} />
                        <span className={`counter${bytes > LABEL_MAX_BYTES ? ' counter--over' : ''}`}>{bytes}/{LABEL_MAX_BYTES}</span>
                      </div>
                      <div className="tag__actions">
                        {!noTransport && !reachable ? (
                          <button type="button" className="button button--small" disabled={busy}
                            onClick={() => void reconnect(device, route)}>Reconnect</button>
                        ) : (
                          <>
                            <button type="button" className="link" disabled={busy || noTransport}
                              title="Sound this tag's buzzer so you can tell which one it is" onClick={() => void buzz(device, route)}>Buzz</button>
                            <button type="button" className="link" disabled={busy || noTransport}
                              title="Read its battery, clock and deployment again" onClick={() => void recheck(device, route)}>Check</button>
                          </>
                        )}
                        <button type="button" className="link link--quiet" disabled={writing} onClick={() => removeDevice(device.eui)}>Remove</button>
                      </div>
                    </div>
                    <div className="tag__pills">
                      {busyKey === key && !writing ? <span className="pill pill--info">Talking to the tag…</span> : <TagPills check={check} now={now} timezone={draft.timezone} />}
                    </div>
                    {(decision === 'ask' || (decision === 'allowed' && check?.stored)) && check?.stored && (
                      <ReplaceNotice decision={decision} start={check.stored.startTime} end={check.stored.endTime} timezone={draft.timezone}
                        onChange={(allowed) => updateDevice(device.eui, { replaceAllowed: allowed ? check.stored!.startTime : null })} />
                    )}
                    {decision === 'downloaded' && (
                      <p className="tag__note">Its current deployment's data has been saved from this browser, so it can be replaced.</p>
                    )}
                    {issues.map((issue: Problem) => (
                      <p key={issue.code} className={`tag__issue tag__issue--${issue.severity === 'error' ? 'crit' : 'warn'}`}>{issue.message}</p>
                    ))}
                  </li>
                );
              })}
            </ul>
          )}
        </section>
      </div>

      <aside className="split__side">
        <section className="panel panel--sticky">
          <h2 className="panel__title">Summary</h2>
          <dl className="summary-list">
            <div><dt>Starts</dt><dd>{summary.starts}</dd></div>
            <div><dt>Ends</dt><dd>{summary.ends}</dd></div>
            <div><dt>Length</dt><dd>{summary.length}</dd></div>
            <div><dt>Records</dt><dd>{summary.hours}</dd></div>
            <div><dt>Time zone</dt><dd>{summary.zone}</dd></div>
            <div><dt>Tags</dt><dd>{draft.devices.length ? `${tagCount}: ${draft.devices.map((d) => d.label.trim() || tagName(d.eui)).join(', ')}` : 'None yet'}</dd></div>
          </dl>

          <ul className="checklist">
            {blockers.map((text) => <li key={text} className="checklist__item checklist__item--crit">{text}</li>)}
            {cautions.map((text) => <li key={text} className="checklist__item checklist__item--warn">{text}</li>)}
            {blockers.length === 0 && draft.devices.length > 0 && (
              <li className="checklist__item checklist__item--ok">Ready to write to {tagCount}.</li>
            )}
          </ul>

          <button type="button" className="button button--primary button--wide" onClick={() => void write()} disabled={!canWrite}>
            {writing ? `Writing… (${progress.filter((p) => !['waiting', 'checking', 'writing'].includes(p.state)).length} of ${progress.length})` : `Write to ${tagCount}`}
          </button>
          <p className="hint">Each tag is checked, written, then read back to confirm what it stored. Keep them on their chargers.</p>

          {progress.length > 0 && (
            <ul className="progress">
              {progress.map((entry) => (
                <li key={entry.key} className={`progress__row progress__row--${PROGRESS_TONE[entry.state]}`}>
                  <span className="progress__name">{entry.label || entry.name}{entry.label && <small> {entry.name}</small>}</span>
                  <span className="progress__state">{PROGRESS_WORDS[entry.state]}</span>
                  {entry.detail && <span className="progress__detail">{entry.detail}</span>}
                </li>
              ))}
            </ul>
          )}

          {record && (
            <div className={`record${record.saved ? ' record--saved' : ''}`}>
              <h3 className="record__title">Deployment record</h3>
              <p className="record__text">
                {record.saved
                  ? 'Saved. Save it again for another copy.'
                  : 'Not saved yet. It lists exactly what each tag confirmed it stored, so that months from now there is an answer to "what did we deploy?" that does not depend on anyone\'s memory.'}
              </p>
              <p className="record__meta">Written {new Date(record.writtenAt).toLocaleString()}</p>
              <div className="actions">
                <button type="button" className={record.saved ? 'button button--quiet' : 'button'} onClick={() => void saveRecord('json')}>Save record</button>
                <button type="button" className="button button--quiet" onClick={() => void saveRecord('txt')}>Save as text</button>
              </div>
            </div>
          )}
        </section>
      </aside>
    </div>
  );
}

/** Which hours to keep when the clocks change during the deployment. A tag cannot follow the change itself. */
function ClockChangeChoice({ notice, value, onChange }: {
  notice: NonNullable<ReturnType<typeof describeSchedule>['clockChange']>;
  value: ClockChangePolicy;
  onChange: (policy: ClockChangePolicy) => void;
}) {
  const choices: Array<{ policy: ClockChangePolicy; title: string; detail: string }> = [
    { policy: 'start', title: `Keep ${notice.asked} until the change`, detail: `After it, the tags record ${notice.options.start.after}.` },
    { policy: 'end', title: `Keep ${notice.asked} from the change on`, detail: `Before it, the tags record ${notice.options.end.before}.` },
    { policy: 'cover', title: `Cover ${notice.asked} on every day`, detail: `Records an hour longer each day: ${notice.options.cover.before} before the change, ${notice.options.cover.after} after.` },
  ];
  return (
    <fieldset className="callout callout--warn">
      <legend className="callout__title">The clocks change on {notice.day}</legend>
      <p>Tags cannot adjust their hours when the clocks change, so the hours they record move by an hour partway through. Choose what to keep:</p>
      {choices.map((choice) => (
        <label key={choice.policy} className="choice">
          <input type="radio" name="clock-change" checked={value === choice.policy} onChange={() => onChange(choice.policy)} />
          <span><strong>{choice.title}.</strong> {choice.detail}</span>
        </label>
      ))}
    </fieldset>
  );
}

/** A tag that still holds a deployment whose data has not been saved from this browser. */
function ReplaceNotice({ decision, start, end, timezone, onChange }: {
  decision: ReplaceDecision;
  start: number;
  end: number;
  timezone: string;
  onChange: (allowed: boolean) => void;
}) {
  return (
    <div className="tag__replace">
      <p>
        This tag still holds a deployment from {formatDate(start, timezone)} to {formatDate(end, timezone)}, and its data has not
        been saved from this browser. Writing a new deployment puts that data out of reach. Download it on the
        <strong> Download &amp; review</strong> page first, unless it is already saved somewhere else.
      </p>
      <label className="toggle toggle--small">
        <input type="checkbox" checked={decision === 'allowed'} onChange={(event) => onChange(event.target.checked)} />
        <span>OK to replace: its data is saved elsewhere, or not needed</span>
      </label>
    </div>
  );
}
