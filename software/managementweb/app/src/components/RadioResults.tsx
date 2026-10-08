// What the radio check shows, whichever way its data arrived: a card per device, the link matrix, and the table of
// where the devices were. Shared by the check from downloaded logs and the live check over Bluetooth, so the two
// cannot drift into showing the same result differently.

import { useEffect, useMemo, useState } from 'react';
import type { RadioCheckDevice, RadioCheckLink, RadioVerdict } from '@tottag/schema';
import { Stat } from './Stat.tsx';
import { circlePositions, formatMm, formatPercent, linePositions, shortId, type Positions, type RadioDevice } from '../features/radioCheck.ts';

export const VERDICT_LABEL: Record<RadioVerdict, string> = { pass: 'Pass', check: 'Check', fail: 'Fail', missing: 'No log' };
export const VERDICT_CLASS: Record<RadioVerdict, string> = { pass: 'ok', check: 'note', fail: 'warning', missing: 'error' };
export const VERDICT_ORDER: Record<RadioVerdict, number> = { fail: 0, check: 1, missing: 2, pass: 3 };

/** Entered positions as typed, so a half-typed "1." is not reformatted under the cursor. */
type PositionText = Record<number, { x: string; y: string }>;

function toPositions(text: PositionText): Positions {
  const out = new Map<number, { x: number; y: number }>();
  for (const [uid, value] of Object.entries(text)) {
    const x = Number.parseFloat(value.x);
    const y = Number.parseFloat(value.y);
    if (Number.isFinite(x) && Number.isFinite(y)) out.set(Number(uid), { x, y });
  }
  return out;
}

function fromPositions(positions: Positions): PositionText {
  return Object.fromEntries([...positions].map(([uid, p]) => [uid, { x: String(p.x), y: String(p.y) }]));
}

/** Positions entered for one test, remembered in this browser so re-opening it does not mean re-measuring. */
export function usePositions(storageKey: string | null) {
  const [text, setText] = useState<PositionText>({});
  useEffect(() => {
    if (!storageKey) return;
    try {
      const raw = localStorage.getItem(storageKey);
      setText(raw ? (JSON.parse(raw) as PositionText) : {});
    } catch {
      setText({});
    }
  }, [storageKey]);
  const update = (next: PositionText) => {
    setText(next);
    try { if (storageKey) localStorage.setItem(storageKey, JSON.stringify(next)); } catch { /* private window */ }
  };
  const positions = useMemo(() => toPositions(text), [text]);
  return { text, positions, update };
}

export function PositionsPanel({ devices, positions }: {
  devices: readonly RadioDevice[];
  positions: ReturnType<typeof usePositions>;
}) {
  const { text, update } = positions;
  const [radius, setRadius] = useState('1');
  const [spacing, setSpacing] = useState('1');
  return (
    <section className="panel">
      <h2 className="panel__title">Where the devices were</h2>
      <p className="panel__note">
        Optional. In metres, from any corner you like. With at least three positions, the check works
        out whether one device reads long or short on every link.
      </p>
      <table className="rtable">
        <thead><tr><th>Device</th><th>X (m)</th><th>Y (m)</th></tr></thead>
        <tbody>
          {devices.map((device) => (
            <tr key={device.uid}>
              <td>{device.label} <span className="device__short">{shortId(device.uid)}</span></td>
              {(['x', 'y'] as const).map((axis) => (
                <td key={axis}>
                  <input
                    className="rtable__input" inputMode="decimal" aria-label={`${device.label} ${axis.toUpperCase()} in metres`}
                    value={text[device.uid]?.[axis] ?? ''}
                    onChange={(event) => update({ ...text, [device.uid]: { x: text[device.uid]?.x ?? '', y: text[device.uid]?.y ?? '', [axis]: event.target.value } })}
                  />
                </td>
              ))}
            </tr>
          ))}
        </tbody>
      </table>
      <div className="panel__actions">
        <label className="field__label radio__layout">
          Circle of radius
          <input className="rtable__input" inputMode="decimal" value={radius} onChange={(event) => setRadius(event.target.value)} /> m
        </label>
        <button type="button" className="button button--quiet"
          onClick={() => update(fromPositions(circlePositions(devices.map((b) => b.uid), Number.parseFloat(radius) || 1)))}>
          Fill in a circle
        </button>
        <label className="field__label radio__layout">
          Line spaced
          <input className="rtable__input" inputMode="decimal" value={spacing} onChange={(event) => setSpacing(event.target.value)} /> m
        </label>
        <button type="button" className="button button--quiet"
          onClick={() => update(fromPositions(linePositions(devices.map((b) => b.uid), Number.parseFloat(spacing) || 1)))}>
          Fill in a line
        </button>
        <button type="button" className="button button--quiet" onClick={() => update({})}>Clear positions</button>
      </div>
    </section>
  );
}

export function RadioCard({ device }: { device: RadioCheckDevice }) {
  const antennas = device.antennaFailureRates.map((rate, i) => `${i + 1}: ${formatPercent(rate)}`).join('  ');
  return (
    <article className={`card card--${VERDICT_CLASS[device.verdict]}`}>
      <header className="card__head">
        <div>
          <h3 className="card__title">{device.label}</h3>
          <p className="card__sub">Short ID {shortId(device.uid)}</p>
        </div>
        <span className={`badge badge--${VERDICT_CLASS[device.verdict]}`}>{VERDICT_LABEL[device.verdict]}</span>
      </header>
      {device.verdict !== 'missing' && (
        <dl className="stats">
          <Stat label="Rounds ranged" value={formatPercent(device.participation)} />
          <Stat label="Receives failed" value={formatPercent(device.rxFailureRate)} />
          <Stat label="Failed by antenna" value={antennas || '—'} />
          <Stat label="Typical link" value={formatPercent(device.linkCoverage)} hint="of rounds with a range to a given peer" />
          <Stat label="Distance offset" value={device.biasMm === null ? '—' : `${device.biasMm > 0 ? '+' : ''}${Math.round(device.biasMm)} mm`} />
          <Stat label="Distance spread" value={formatMm(device.noiseMm)} />
        </dl>
      )}
      {device.reasons.length > 0 && (
        <ul className="findings">
          {device.reasons.map((reason) => (
            <li key={reason} className={`finding finding--${device.verdict === 'fail' ? 'warning' : 'note'}`}>
              <span className="finding__text">{reason}</span>
            </li>
          ))}
        </ul>
      )}
    </article>
  );
}

/** Coverage between every pair, read along a row: how often that device ranged to the column's. */
export function LinkMatrix({ devices, links }: { devices: readonly RadioDevice[]; links: readonly RadioCheckLink[] }) {
  if (devices.length < 2) return null;
  const find = (a: number, b: number) => links.find((link) => (link.a === a && link.b === b) || (link.a === b && link.b === a));
  return (
    <section className="panel">
      <h2 className="panel__title">Links</h2>
      <p className="panel__note">
        How often the device in each row ranged to the device in each column. A device weak on every link is
        the device; one weak pair is more likely something between them. Hover a cell for its distance.
      </p>
      <div className="matrix__scroll">
        <table className="matrix">
          <thead>
            <tr><th />{devices.map((device) => <th key={device.uid} scope="col">{device.label}</th>)}</tr>
          </thead>
          <tbody>
            {devices.map((row) => (
              <tr key={row.uid}>
                <th scope="row">{row.label}</th>
                {devices.map((column) => {
                  if (row.uid === column.uid) return <td key={column.uid} className="matrix__self" />;
                  const link = find(row.uid, column.uid);
                  const coverage = link ? (link.a === row.uid ? link.coverageAtoB : link.coverageBtoA) : 0;
                  const band = coverage >= 0.9 ? 'good' : coverage >= 0.7 ? 'fair' : 'poor';
                  const detail = link?.medianMm !== null && link?.medianMm !== undefined
                    ? `${formatMm(link.medianMm)} ± ${formatMm(link.noiseMm)}${link.residualMm !== null ? `, ${link.residualMm > 0 ? '+' : ''}${Math.round(link.residualMm)} mm from the positions` : ''}`
                    : 'no ranges';
                  return (
                    <td key={column.uid} className={`matrix__cell matrix__cell--${band}`} title={`${row.label} → ${column.label}: ${detail}`}>
                      {formatPercent(coverage)}
                    </td>
                  );
                })}
              </tr>
            ))}
          </tbody>
        </table>
      </div>
    </section>
  );
}
