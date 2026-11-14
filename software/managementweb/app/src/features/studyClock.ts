// Which deployment each open log belongs to, and which clock its times are shown on. Pure.
//
// A tag keeps one clock and a log records nothing about where the study was. The study's time zone is
// written down in exactly one place: the deployment record saved when the tags were set up. So every
// time on the review page comes from here — the record's zone when a record matching the logs is open,
// and the zone on the setup page otherwise, said plainly so nobody mistakes one for the other.

import {
  describeTimezone, formatDate, matchManifest, parseExperimentDetails, parseManifest, radioDeploymentKey,
  type ExperimentDetails, type LogIdentity, type ManifestMatch, type StudyRecord,
} from '@tottag/schema';
import type { DeviceReport } from './deployment.ts';
import { summariseLog } from './logSummary.ts';
import type { LoadedRecordFile, LoadFailure } from '../ports/logSource.ts';

/** A deployment record open on the page. */
export interface OpenRecord {
  readonly id: string;
  readonly fileName: string;
  readonly record: StudyRecord;
}

const sameRecord = (a: StudyRecord, b: StudyRecord) =>
  a.timezone === b.timezone && a.startTime === b.startTime && a.detailsHex === b.detailsHex &&
  a.devices.map((d) => d.eui).join() === b.devices.map((d) => d.eui).join();

/**
 * Read record files, skipping any already open.
 *
 * A .json from a whole folder that is not a record at all is passed over quietly: a folder of downloads
 * holds all sorts. One chosen by hand that cannot be used is reported, with the reason.
 */
export function readRecordFiles(
  files: readonly LoadedRecordFile[],
  open: readonly OpenRecord[],
  fromFolder = false,
): { added: OpenRecord[]; failed: LoadFailure[] } {
  const added: OpenRecord[] = [];
  const failed: LoadFailure[] = [];
  for (const file of files) {
    const result = parseManifest(file.text);
    if (!result.ok) {
      if (!(fromFolder && /not a deployment record/.test(result.reason))) failed.push({ name: file.name, reason: result.reason });
      continue;
    }
    if ([...open, ...added].some((existing) => sameRecord(existing.record, result.record))) continue;
    added.push({ id: `record-${open.length + added.length}-${file.name}`, fileName: file.name, record: result.record });
  }
  return { added, failed };
}

/** What a log says about which deployment it came from, for matching it to a record. */
export function logIdentity(report: DeviceReport): LogIdentity {
  const summary = summariseLog(report);
  return {
    details: report.report?.details ?? null,
    experimentStartTime: report.radio?.experimentStartTime ?? null,
    tag: summary.eui ?? summary.uid,
  };
}

/** The logs of one deployment, and the clock to show them on. */
export interface Deployment {
  /** Stable while the same logs are open. */
  readonly key: string;
  /** Worst first, the order the cards are shown in. */
  readonly reports: readonly DeviceReport[];
  /** The record that set these tags up, when one is open. */
  readonly record: OpenRecord | null;
  readonly matchedBy: ManifestMatch['by'] | null;
  /** IANA zone every time for this deployment is shown in. */
  readonly timezone: string;
  /** False when no record matched and the zone is the one on the setup page. */
  readonly fromRecord: boolean;
  /** What the tags were told, from the first log's details block. */
  readonly scheduled: ExperimentDetails | null;
  readonly startTime: number | null;
  /** A name for the selector and headings. */
  readonly title: string;
}

function scheduledOf(report: DeviceReport): ExperimentDetails | null {
  try {
    const details = report.report?.details;
    if (!details || details.length < 18) return null;
    const parsed = parseExperimentDetails(details);
    return parsed.numDevices > 0 ? parsed : null;
  } catch {
    return null;
  }
}

/**
 * Split the open logs into deployments and give each its clock.
 *
 * Logs belong together when they carry the same start time and tag list, which every log of one
 * deployment does. Logs that could not be read have no deployment and are returned apart.
 */
export function groupDeployments(
  ordered: readonly DeviceReport[],
  records: readonly OpenRecord[],
  fallbackTimezone: string,
): { deployments: Deployment[]; unreadable: DeviceReport[] } {
  const unreadable: DeviceReport[] = [];
  const groups = new Map<string, DeviceReport[]>();
  for (const report of ordered) {
    if (report.error || !report.radio) {
      unreadable.push(report);
      continue;
    }
    const key = radioDeploymentKey(report.radio) ?? `log:${report.id}`;
    groups.set(key, [...(groups.get(key) ?? []), report]);
  }

  const studyRecords = records.map((open) => open.record);
  const deployments = [...groups].map(([key, reports]): Deployment => {
    // Every log of a deployment normally matches the same record. Should they disagree, the record most
    // of them match wins, and an exact match on the details outranks one on start time and tag.
    const votes = new Map<number, { count: number; by: ManifestMatch['by'] }>();
    for (const report of reports) {
      const match = matchManifest(logIdentity(report), studyRecords);
      if (!match) continue;
      const vote = votes.get(match.index) ?? { count: 0, by: match.by };
      votes.set(match.index, { count: vote.count + 1, by: vote.by === 'details' || match.by === 'details' ? 'details' : 'start-and-tag' });
    }
    const best = [...votes].sort((a, b) =>
      b[1].count - a[1].count || (a[1].by === 'details' ? -1 : 0) - (b[1].by === 'details' ? -1 : 0) || a[0] - b[0])[0];
    const record = best ? records[best[0]]! : null;
    const scheduled = reports.map(scheduledOf).find((details) => details !== null) ?? null;
    const timezone = record ? record.record.timezone : fallbackTimezone;
    const startTime = scheduled?.experimentStartTime ?? reports[0]!.radio?.experimentStartTime ?? null;
    const tags = `${reports.length} tag${reports.length === 1 ? '' : 's'}`;
    const base = record?.record.name
      ?? (startTime ? `Deployment starting ${formatDate(startTime, timezone)}` : 'Logs without a deployment');
    return {
      key, reports, record, matchedBy: best ? best[1].by : null, timezone, fromRecord: record !== null,
      scheduled, startTime, title: `${base} (${tags})`,
    };
  });
  return { deployments, unreadable };
}

/** "Central Time (America/Chicago)", or the zone's name alone where the browser has no everyday name for it. */
export function zoneName(timezone: string): string {
  const everyday = describeTimezone(timezone);
  return everyday === timezone ? timezone : `${everyday} (${timezone})`;
}

/** The sentence that says which clock a deployment's times are on, and why. */
export function zoneSentence(deployment: Deployment): { tone: 'info' | 'warn'; text: string } {
  const zone = zoneName(deployment.timezone);
  if (deployment.record) {
    const from = deployment.record.record.name ? `the deployment record for ${deployment.record.record.name}` : `the deployment record ${deployment.record.fileName}`;
    return { tone: 'info', text: `Times are in ${zone}, the study's time zone from ${from}.` };
  }
  return {
    tone: 'warn',
    text: `No deployment record for these logs is open, so times are shown in ${zone}, the time zone on the setup page. ` +
      'Add the record (.json) saved when these tags were set up to see them in the study\'s own time zone.',
  };
}
