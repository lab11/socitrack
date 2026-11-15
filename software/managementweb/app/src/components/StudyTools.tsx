import { useEffect, useId, useState } from 'react';
import type { EventLog } from '@tottag/schema';
import { pickTextFile } from '../adapters/textFile.ts';
import { cutoffIn, touchingIn, type StudySettings } from '../features/chartData.ts';
import { toMm, UNIT_WORDS, type DistanceUnit } from '../features/units.ts';

interface Props {
  readonly settings: StudySettings;
  readonly onChange: (settings: StudySettings) => void;
  /** The notes as read on the study's clock, for the count and anything skipped. */
  readonly notes: EventLog | null;
  /** Offer the touching distance, which only the time-together table uses. */
  readonly showTouching?: boolean;
  /** Offer the distance settings at all; the charts of battery and movement have no use for them. */
  readonly showDistances?: boolean;
}

/** A number field that keeps the last good value while someone is part way through typing a new one. */
function NumberField({ label, value, unit, min, step, onChange }: {
  label: string; value: number; unit: string; min?: number; step?: number; onChange: (value: number) => void;
}) {
  const id = useId();
  // What is in the box, which may be half typed; the setting changes only when it is a usable number.
  const [text, setText] = useState(String(value));
  useEffect(() => {
    setText((current) => (Number(current) === value && current !== '' ? current : String(value)));
  }, [value]);
  return (
    <label className="inline-field" htmlFor={id}>
      {label}
      <input id={id} className="input input--number" type="number" value={text} min={min} step={step ?? 'any'}
        onChange={(event) => {
          setText(event.target.value);
          const next = Number(event.target.value);
          if (event.target.value !== '' && Number.isFinite(next) && (min === undefined || next >= min)) onChange(next);
        }}
        onBlur={() => setText(String(value))} />
      {unit}
    </label>
  );
}

/**
 * The settings every view of a deployment shares: distance unit, cutoff, touching distance, and a
 * researcher's own notes of what happened when.
 */
export function StudyTools({ settings, onChange, notes, showTouching = false, showDistances = true }: Props) {
  const unit = UNIT_WORDS[settings.unit];
  const addNotes = async () => {
    const picked = await pickTextFile();
    if (picked) onChange({ ...settings, notes: { fileName: picked.name, text: picked.text } });
  };
  return (
    <div className="tools">
      {showDistances && (
        <div className="tools__group">
          <span className="tools__label">Distances in</span>
          <div className="segmented" role="radiogroup" aria-label="Distance unit">
            {(['ft', 'm'] as DistanceUnit[]).map((choice) => (
              <button key={choice} type="button" role="radio" aria-checked={settings.unit === choice}
                className={`segmented__option${settings.unit === choice ? ' segmented__option--on' : ''}`}
                onClick={() => onChange({ ...settings, unit: choice })}>
                {UNIT_WORDS[choice].long}
              </button>
            ))}
          </div>
          <NumberField label="Leave out distances over" value={cutoffIn(settings)} unit={unit.short} min={0}
            onChange={(cutoff) => onChange({ ...settings, cutoffMm: toMm(cutoff, settings.unit) })} />
          {showTouching && (
            <NumberField label="Touching distance is within" value={touchingIn(settings)} unit={unit.short} min={0}
              onChange={(touching) => onChange({ ...settings, touchingMm: toMm(touching, settings.unit) })} />
          )}
        </div>
      )}
      <div className="tools__group">
        <span className="tools__label">Noted events</span>
        {settings.notes ? (
          <>
            <span className="tools__value">
              {notes?.events.length ?? 0} from {settings.notes.fileName}
            </span>
            <NumberField label="Move them by" value={settings.offsetSeconds} unit="seconds" step={1}
              onChange={(offsetSeconds) => onChange({ ...settings, offsetSeconds })} />
            <button type="button" className="link link--quiet" onClick={() => onChange({ ...settings, notes: null, offsetSeconds: 0 })}>
              Remove
            </button>
          </>
        ) : (
          <button type="button" className="button button--quiet button--small" onClick={() => void addNotes()}>
            Add a notes file
          </button>
        )}
      </div>
      {settings.notes && notes && notes.skipped.length > 0 && (
        <p className="hint tools__note">
          {notes.skipped.length === 1 ? 'Line' : 'Lines'} {notes.skipped.slice(0, 8).join(', ')}{notes.skipped.length > 8 ? '…' : ''} of the
          notes file {notes.skipped.length === 1 ? 'was' : 'were'} not an event. Each event is one line: the date, the time, and what
          happened, such as <code>2026-09-02 07:33:11 Arrived at daycare</code>.
        </p>
      )}
      {!settings.notes && (
        <p className="hint tools__note">
          A notes file is plain text with one event per line, the date and time on the study's clock and then what
          happened, such as <code>2026-09-02 07:33:11 Arrived at daycare</code>. Events are drawn on every chart.
        </p>
      )}
    </div>
  );
}
