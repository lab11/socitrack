// What a tag reports about itself, reduced to what someone setting up a study needs to know. Pure.
//
// Three questions, each of which used to go unasked until it had already cost a study:
//
//   1. Is the battery charged enough to start? Below MIN_RECORDING_BATTERY_MV a tag taken off its
//      charger powers off instead of recording.
//   2. Is its clock right? It is set whenever a deployment is written, so this is reassurance more
//      than a problem, but a tag with no clock at all has never been set up.
//   3. Does it still hold a deployment whose data could be lost? A tag's log is divided by deployment,
//      and writing a new one — or cancelling the current one — leaves everything recorded before it
//      out of reach of both tools. So a deployment that may have recorded must be downloaded, or
//      knowingly given up, before it is replaced.

import { diffReadback, MIN_RECORDING_BATTERY_MV, type DeploymentConfig, type ExperimentDetails } from '@tottag/schema';
import type { TagSnapshot } from '../ports/tagTransport.ts';

/** 1 January 2020. A clock reading earlier than this has never been set. */
const EARLIEST_SET_CLOCK = 1_577_836_800;
/** A clock within this many seconds of this computer's is right, given the time a read takes. */
export const CLOCK_TOLERANCE_S = 3;

export interface StoredDeployment {
  readonly startTime: number;
  readonly endTime: number;
  readonly useDailyTimes: boolean;
  /** On the tag's own clock, as stored. */
  readonly dailyStartTime: number;
  readonly dailyEndTime: number;
  readonly devices: ReadonlyArray<{ readonly eui: readonly number[]; readonly label: string }>;
  readonly cancelled: boolean;
}

/** One reading of a tag. Plain data, so it can be kept between visits. */
export interface TagCheck {
  /** Unix seconds. */
  readonly checkedAt: number;
  readonly batteryMv: number | null;
  /** The tag's clock minus this computer's, in seconds. Null when it could not be read. */
  readonly clockOffsetSeconds: number | null;
  readonly clockSet: boolean | null;
  readonly firmware: string | null;
  /** Null when it holds no deployment at all. */
  readonly stored: StoredDeployment | null;
}

export function storedFromDetails(details: ExperimentDetails | null): StoredDeployment | null {
  if (!details || details.numDevices === 0 || details.experimentStartTime === 0) return null;
  return {
    startTime: details.experimentStartTime,
    endTime: details.experimentEndTime,
    useDailyTimes: details.useDailyTimes,
    dailyStartTime: details.dailyStartTime,
    dailyEndTime: details.dailyEndTime,
    devices: details.uids.map((uid, index) => ({ eui: Array.from(uid), label: details.labels[index] ?? '' })),
    cancelled: details.isTerminated,
  };
}

export function assessSnapshot(snapshot: TagSnapshot, firmware: string | null, now: number): TagCheck {
  const timestamp = snapshot.timestamp;
  const clockSet = timestamp === null ? null : timestamp >= EARLIEST_SET_CLOCK;
  return {
    checkedAt: Math.floor(now),
    batteryMv: snapshot.batteryMv,
    clockOffsetSeconds: timestamp === null || !clockSet ? null : timestamp - Math.round(now),
    clockSet,
    firmware,
    stored: storedFromDetails(snapshot.experiment),
  };
}

export const batteryLow = (mv: number | null | undefined): boolean =>
  typeof mv === 'number' && mv > 0 && mv < MIN_RECORDING_BATTERY_MV;

export type StoredState = 'upcoming' | 'running' | 'finished' | 'cancelled';

export function storedState(stored: StoredDeployment, now: number): StoredState {
  if (stored.cancelled) return 'cancelled';
  if (now < stored.startTime) return 'upcoming';
  if (now < stored.endTime) return 'running';
  return 'finished';
}

/** Whether a tag holding this deployment has had the chance to record anything under it. */
export const mayHoldData = (stored: StoredDeployment | null, now: number): boolean =>
  !!stored && !stored.cancelled && stored.startTime <= now;

/** True when the tag already holds exactly this deployment, so writing it again replaces nothing. */
export function holdsSameDeployment(stored: StoredDeployment | null, config: DeploymentConfig): boolean {
  if (!stored) return false;
  return diffReadback(config, {
    experimentStartTime: stored.startTime,
    experimentEndTime: stored.endTime,
    dailyStartTime: stored.dailyStartTime,
    dailyEndTime: stored.dailyEndTime,
    useDailyTimes: stored.useDailyTimes,
    numDevices: stored.devices.length,
    uids: stored.devices.map((device) => Uint8Array.from(device.eui)),
    labels: stored.devices.map((device) => device.label),
    isTerminated: stored.cancelled,
  }).length === 0;
}

/**
 * Whether writing `config` to this tag needs someone to say its current deployment may go.
 *
 *   none        holds nothing that could be lost
 *   same        already holds this deployment
 *   downloaded  holds one whose data was saved from this browser
 *   allowed     holds one, and the person said it may be replaced
 *   ask         holds one that may have recorded, and nobody has said
 */
export type ReplaceDecision = 'none' | 'same' | 'downloaded' | 'allowed' | 'ask';

export function replaceDecision(
  check: TagCheck | null,
  config: DeploymentConfig,
  now: number,
  downloaded: boolean,
  replaceAllowed: number | null | undefined,
): ReplaceDecision {
  const stored = check?.stored ?? null;
  if (!mayHoldData(stored, now)) return 'none';
  if (holdsSameDeployment(stored, config)) return 'same';
  if (downloaded) return 'downloaded';
  if (replaceAllowed === stored!.startTime) return 'allowed';
  return 'ask';
}

/** The key a deployment's data is remembered under once downloaded: its tag's one-byte ID and its start. */
export const downloadKey = (shortUid: number, startTime: number): string =>
  `${shortUid.toString(16).padStart(2, '0').toUpperCase()}@${startTime}`;
