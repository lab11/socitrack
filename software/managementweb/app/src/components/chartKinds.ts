import type { ChartKind } from './TimeSeriesChart.tsx';

/** How each kind of span and marker looks, the same on every chart. */
export const CHART_KINDS: Readonly<Record<string, ChartKind>> = {
  charging: { label: 'On the charger', color: 'var(--ok)' },
  restart: { label: 'Restart (normal)', color: 'var(--note)' },
  'restart-warn': { label: 'Restart (worth a look)', color: 'var(--crit)' },
  clock: { label: 'Clock corrected', color: 'var(--ink-3)', dashed: true },
  note: { label: 'Noted event', color: 'var(--primary)', dashed: true },
  'low-battery': { label: 'Battery critically low', color: 'var(--warn)' },
};

/** Line colors for several series on one chart, from the tokens. */
// Apricot is kept for the mark alone, as tokens.css asks.
export const LINE_COLORS = ['var(--primary)', 'var(--ok)', 'var(--warn)', 'var(--note)', 'var(--crit)'] as const;
