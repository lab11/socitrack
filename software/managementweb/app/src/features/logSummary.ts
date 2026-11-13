// What a log says about the study it came from, for the card that summarises it. Pure.
//
// The checks a researcher is asked to make before reusing tags — does each log cover the dates
// expected, and did every tag see every other — need the deployment's own schedule and roster, which
// every log carries in its details block. This lays them out against what the log actually recorded.

import { identifyRadioLog, parseExperimentDetails, tagName, type ExperimentDetails } from '@tottag/schema';
import type { DeviceReport } from './deployment.ts';

export interface PeerLine {
  readonly uid: number;
  /** The label it was given, or its TotTag name. */
  readonly name: string;
  /** Range records naming this peer. Zero for a peer it never ranged with. */
  readonly ranges: number;
  /** Whether the deployment listed it. A peer that was not is another study's tag nearby. */
  readonly scheduled: boolean;
}

export interface LogSummary {
  /** This tag's one-byte ID, where the log or its file name settles it. */
  readonly uid: number | null;
  /**
   * False when nothing settles which tag wrote the log. One of the scheduled peers is then the tag
   * itself, which can never range to itself, so "never" against it means nothing.
   */
  readonly selfKnown: boolean;
  /** "TotTag 00:0A", where the details block holds this tag's full address. */
  readonly tag: string | null;
  /** The label this deployment gave it. */
  readonly label: string | null;
  /** What the tag was told, from the details block. */
  readonly scheduled: ExperimentDetails | null;
  /** Unix seconds of the first and last record. */
  readonly firstRecord: number | null;
  readonly lastRecord: number | null;
  /** Every other tag in the deployment, then any it heard that were not in it. */
  readonly peers: readonly PeerLine[];
}

export function summariseLog(report: DeviceReport): LogSummary {
  let scheduled: ExperimentDetails | null = null;
  try {
    if (report.report?.details) scheduled = parseExperimentDetails(report.report.details);
    if (scheduled && scheduled.numDevices === 0) scheduled = null;
  } catch {
    scheduled = null;
  }

  const radio = report.radio;
  const uid = radio ? identifyRadioLog(radio, report.name, report.deviceUid) : report.deviceUid ?? null;
  const index = scheduled && uid !== null ? scheduled.uids.findIndex((eui) => eui[0] === uid) : -1;
  const own = index >= 0 ? scheduled!.uids[index]! : null;

  const start = radio?.experimentStartTime ?? null;
  const firstRecord = start !== null && radio?.firstMs !== null && radio?.firstMs !== undefined ? start + radio.firstMs / 1000 : null;
  const lastRecord = start !== null && radio?.lastMs !== null && radio?.lastMs !== undefined ? start + radio.lastMs / 1000 : null;

  const seen = report.health?.rangingPeers ?? new Map<number, number>();
  const peers: PeerLine[] = [];
  if (scheduled) {
    scheduled.uids.forEach((eui, i) => {
      const peer = eui[0]!;
      if (peer === uid) return;
      peers.push({ uid: peer, name: scheduled!.labels[i]?.trim() || tagName(eui), ranges: seen.get(peer) ?? 0, scheduled: true });
    });
  }
  for (const [peer, ranges] of seen) {
    if (peer === uid || peers.some((line) => line.uid === peer)) continue;
    peers.push({ uid: peer, name: `a tag ending ${peer.toString(16).padStart(2, '0').toUpperCase()}`, ranges, scheduled: false });
  }

  return {
    uid,
    selfKnown: uid !== null,
    tag: own ? tagName(own) : null,
    label: index >= 0 ? scheduled!.labels[index]?.trim() || null : null,
    scheduled,
    firstRecord,
    lastRecord,
    peers,
  };
}
