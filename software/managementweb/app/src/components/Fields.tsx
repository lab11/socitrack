// Form fields for a deployment: a time zone with type-ahead, and date and time inputs that hold exactly
// what was typed.

import { useId, useMemo, type ReactNode } from 'react';
import { describeTimezone, EARLIEST_YEAR, isValidTimezone, LATEST_YEAR, supportedTimezones } from '@tottag/schema';

export function Field({ label, hint, error, children, htmlFor }: {
  label: string;
  hint?: ReactNode;
  error?: string | null | undefined;
  htmlFor?: string;
  children: ReactNode;
}) {
  return (
    <div className={`field${error ? ' field--invalid' : ''}`}>
      <label className="field__label" htmlFor={htmlFor}>{label}</label>
      {children}
      {error ? <p className="field__error">{error}</p> : hint ? <p className="field__hint">{hint}</p> : null}
    </div>
  );
}

/**
 * Time zone entry with type-ahead.
 *
 * A plain select lists four hundred zones in one column, which is unusable for finding one. A datalist
 * filters as you type and still only accepts a real zone. The zone's everyday name is shown beneath,
 * so "America/Chicago" is confirmed as Central Time rather than taken on trust.
 */
export function TimezoneField({ value, onChange }: { value: string; onChange: (timezone: string) => void }) {
  const id = useId();
  const zones = useMemo(() => supportedTimezones(), []);
  const valid = isValidTimezone(value);
  return (
    <Field
      label="Time zone of the study"
      htmlFor={id}
      error={valid ? null : value.trim() ? 'Not a time zone this browser recognises. Type a city, such as Chicago, and pick from the list.' : 'Choose a time zone.'}
      hint={<>{describeTimezone(value)}. Every time on this page is on this clock.</>}
    >
      <input id={id} className="input" list={`${id}-zones`} value={value} aria-invalid={!valid}
        placeholder="Start typing a city, such as Chicago" autoComplete="off" spellCheck={false}
        onChange={(event) => onChange(event.target.value)} />
      <datalist id={`${id}-zones`}>
        {zones.map((zone) => <option key={zone} value={zone} />)}
      </datalist>
    </Field>
  );
}

/**
 * A date and time on the study's clock.
 *
 * The value shown is always exactly the value the browser last reported. The old field converted
 * every keystroke into an instant and back, and the first digit typed into the year — which arrives
 * as year 0002 — came back out unpadded, which the browser rejected and blanked the whole field.
 */
export function DateTimeField({ label, value, onChange, error }: {
  label: string;
  value: string;
  onChange: (value: string) => void;
  error?: string | null | undefined;
}) {
  const id = useId();
  return (
    <Field label={label} htmlFor={id} error={error}>
      <input id={id} className="input" type="datetime-local" value={value} aria-invalid={!!error}
        min={`${EARLIEST_YEAR}-01-01T00:00`} max={`${LATEST_YEAR}-12-31T23:59`}
        onChange={(event) => onChange(event.target.value)} />
    </Field>
  );
}

export function TimeField({ label, value, onChange, error }: {
  label: string;
  value: string;
  onChange: (value: string) => void;
  error?: string | null | undefined;
}) {
  const id = useId();
  return (
    <Field label={label} htmlFor={id} error={error}>
      <input id={id} className="input" type="time" value={value} aria-invalid={!!error}
        onChange={(event) => onChange(event.target.value)} />
    </Field>
  );
}
