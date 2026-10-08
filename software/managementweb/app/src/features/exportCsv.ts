// CSV generation. Pure — returns a string, writes nothing.
//
// This exists because the pipeline it replaces was: run a script, capture printed text, then scrape
// that text with a regex into a CSV, with the input and output paths hardcoded to one researcher's
// Desktop. Every step of that could silently drop a row. Producing the CSV directly from parsed
// records removes the scrape entirely.

import type { DeviceReport } from './deployment.ts';

/** RFC 4180 quoting. A label containing a comma is not hypothetical — they are free text. */
function cell(value: string | number | null | undefined): string {
  if (value === null || value === undefined) return '';
  const text = String(value);
  return /[",\n\r]/.test(text) ? `"${text.replace(/"/g, '""')}"` : text;
}

const HEADERS = [
  'file',
  'format_version',
  'bytes',
  'span_seconds',
  'records',
  'reboots',
  'watchdog_resets',
  'run_time_lost_fraction',
  'pages_delivered',
  'pages_advertised',
  'pages_lost',
  'crc_failures',
  'battery_start_mv',
  'battery_end_mv',
  'peers_seen',
  'warnings',
  'notes',
  'anomaly_codes',
];

/**
 * One row per log.
 *
 * Deliberately a summary rather than a record dump: the per-record export belongs with the
 * proximity statistics, which is a later increment and a different shape. Shipping a half-finished
 * record export now would be a file people start depending on before it is right.
 */
export function summaryCsv(reports: readonly DeviceReport[]): string {
  const rows = [HEADERS.join(',')];
  for (const report of reports) {
    const health = report.health;
    const anomalies = health?.anomalies ?? [];
    rows.push(
      [
        cell(report.name),
        cell(report.report?.format),
        cell(report.bytes),
        cell(health ? Math.round(health.spanSeconds) : null),
        cell(health?.recordCount),
        cell(health?.reboots.length),
        cell(health?.watchdogResets),
        cell(health ? health.runTimeLostFraction.toFixed(6) : null),
        cell(health?.integrity.pagesDelivered),
        cell(health?.integrity.pagesAdvertised),
        cell(health?.integrity.holes),
        cell(health?.integrity.crcFailures),
        cell(health?.batteryStartMv),
        cell(health?.batteryEndMv),
        cell(health?.rangingPeers.size),
        cell(anomalies.filter((a) => a.severity === 'warning').length),
        cell(anomalies.filter((a) => a.severity === 'note').length),
        cell(anomalies.map((a) => a.code).join(' ')),
      ].join(','),
    );
  }
  return `${rows.join('\n')}\n`;
}

/** A filename that sorts chronologically and says what it is. */
export function summaryFilename(now: Date): string {
  const stamp = now.toISOString().slice(0, 19).replace(/[:T]/g, '-');
  return `tottag-deployment-summary-${stamp}.csv`;
}
