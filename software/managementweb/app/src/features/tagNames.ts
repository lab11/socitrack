// What to call each tag in a deployment, everywhere a person reads it. Pure.
//
// A specific tag is "TotTag" and the last two parts of its address, with the label it was given in this
// deployment beside it when it has one. Labels come from the deployment's own details block, so they
// belong to that study and are never carried over from another.

import { tagName, type ExperimentDetails } from '@tottag/schema';
import type { DeviceReport } from './deployment.ts';
import { summariseLog } from './logSummary.ts';

export interface TagRef {
  /** The one-byte ID tags know each other by. */
  readonly uid: number;
  /** "TotTag 02:0A", or a description for a tag not in the deployment. */
  readonly name: string;
  /** The label this deployment gave it, if any. */
  readonly label: string | null;
  /** "Child (TotTag 02:0A)", or the name alone. */
  readonly display: string;
}

const display = (name: string, label: string | null) => (label ? `${label} (${name})` : name);

/** A tag by its one-byte ID, named from the deployment that lists it. */
export function tagRef(uid: number, scheduled: ExperimentDetails | null): TagRef {
  const index = scheduled ? scheduled.uids.findIndex((eui) => eui[0] === uid) : -1;
  if (index < 0) {
    const name = `a tag ending ${uid.toString(16).padStart(2, '0').toUpperCase()}, not in this deployment`;
    return { uid, name, label: null, display: name };
  }
  const name = tagName(scheduled!.uids[index]!);
  const label = scheduled!.labels[index]?.trim() || null;
  return { uid, name, label, display: display(name, label) };
}

/** The tag a log came from, or null where the log cannot say. */
export function logTag(report: DeviceReport, scheduled: ExperimentDetails | null): TagRef | null {
  const summary = summariseLog(report);
  if (summary.uid === null) return null;
  return tagRef(summary.uid, scheduled ?? summary.scheduled);
}

/** What to call a log: its tag, or its file name where the log cannot say which tag wrote it. */
export function logName(report: DeviceReport, scheduled: ExperimentDetails | null): string {
  return logTag(report, scheduled)?.display ?? report.name;
}
