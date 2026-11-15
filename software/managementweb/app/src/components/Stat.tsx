/**
 * One labelled figure.
 *
 * Numeric values render in the mono stack with tabular figures, so a column of them aligns and a
 * transposed digit is visible — these are numbers people copy into analyses. Values that are PROSE
 * do not: rendering "All of it" in a monospace face makes a plain-English answer look like a code
 * literal, which is the opposite of the reassurance it is there to give.
 */
const LOOKS_NUMERIC = /^[\d.,\s\u2192+-]+[A-Za-z%\u00b0]{0,3}$/;

export function Stat({ label, value, hint }: { label: string; value: string; hint?: string | undefined }) {
  const numeric = LOOKS_NUMERIC.test(value);
  return (
    <div className="stat">
      <dt className="stat__label">{label}</dt>
      <dd className={numeric ? 'stat__value numeric' : 'stat__value stat__value--prose'}>{value}</dd>
      {hint ? <dd className="stat__hint">{hint}</dd> : null}
    </div>
  );
}
