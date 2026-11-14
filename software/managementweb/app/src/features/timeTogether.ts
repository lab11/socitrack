// Daily time together, as a table and as a CSV file. Pure.

import type { DailyRow, DayFigures } from '@tottag/schema';
import { cell } from './exportCsv.ts';
import { toUnit, UNIT_WORDS, type DistanceUnit } from './units.ts';

export interface TimeTogetherTable {
  /** The tag whose log this is. */
  readonly tag: string;
  /** The tags it was measured against, in the order the figures are. */
  readonly peers: readonly string[];
  readonly rows: readonly DailyRow[];
  readonly unit: DistanceUnit;
  /** In the unit above. */
  readonly touching: number;
  readonly cutoff: number;
  /** IANA zone the days run on. */
  readonly timezone: string;
}

/** Each line of the table: one per day and tag, then "any" and "all" where several tags were chosen. */
export interface TimeTogetherLine {
  readonly date: string;
  readonly with: string;
  readonly figures: DayFigures;
  readonly combined: boolean;
}

export function tableLines(table: TimeTogetherTable): TimeTogetherLine[] {
  const lines: TimeTogetherLine[] = [];
  for (const row of table.rows) {
    row.peers.forEach((figures, i) => lines.push({ date: row.date, with: table.peers[i]!, figures, combined: false }));
    if (row.any) lines.push({ date: row.date, with: table.peers.length === 2 ? 'Either one' : 'Any of them', figures: row.any, combined: true });
    if (row.all) lines.push({ date: row.date, with: table.peers.length === 2 ? 'Both at once' : 'All at once', figures: row.all, combined: true });
  }
  return lines;
}

/** Minutes to one decimal place: half a minute together is not rounded away. */
export const formatMinutes = (minutes: number) => minutes.toFixed(1);

/** One row per day and tag, every figure in the units chosen on screen, and the settings that produced them. */
export function timeTogetherCsv(table: TimeTogetherTable): string {
  const unit = UNIT_WORDS[table.unit].short;
  const header = [
    'date', 'tag', 'with', 'minutes_in_range', `minutes_within_${table.touching}_${unit}`, `mean_distance_${unit}`,
    'rounds_in_range', 'rounds_within_touching', `cutoff_${unit}`, `touching_${unit}`, 'time_zone',
  ];
  const rows = tableLines(table).map((line) => [
    cell(line.date),
    cell(table.tag),
    cell(line.with),
    cell(formatMinutes(line.figures.minutesInRange)),
    cell(formatMinutes(line.figures.minutesTouching)),
    cell(line.figures.meanMm === null ? null : toUnit(line.figures.meanMm, table.unit).toFixed(2)),
    cell(line.figures.rounds),
    cell(line.figures.touchingRounds),
    cell(table.cutoff),
    cell(table.touching),
    cell(table.timezone),
  ].join(','));
  return `${[header.join(','), ...rows].join('\n')}\n`;
}

/** "time-together-Child-2026-09-01.csv": says what it is, and is safe on any computer's file system. */
export function timeTogetherFilename(tag: string, firstDate: string | undefined): string {
  const safe = tag.replace(/\(.*?\)/g, '').trim().replace(/[^A-Za-z0-9_-]+/g, '-').replace(/^-+|-+$/g, '') || 'tag';
  return `time-together-${safe}${firstDate ? `-${firstDate}` : ''}.csv`;
}
