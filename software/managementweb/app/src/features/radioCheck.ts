// Grouping loaded logs into the deployments they came from, for the radio check. Pure: no browser APIs.
//
// The devices under test are exactly the ones the deployment selected, read from the details block every log
// carries, so a three-device deployment is a three-device test and a ten-device one is a ten-device test. A
// selected device whose log was not loaded is still listed, as missing, rather than silently shrinking the
// fleet the others are compared against.

import {
  analyseRadio, identifyRadioLog, radioDeploymentKey,
  type RadioCheckResult, type RadioSummary,
} from '@tottag/schema';
import type { DeviceReport } from './deployment.ts';

export interface RadioDevice {
  readonly uid: number;
  readonly label: string;
}

export interface RadioDeployment {
  readonly key: string;
  /** Unix seconds. */
  readonly startTime: number;
  readonly devices: readonly RadioDevice[];
  /** The loaded log of each selected device, by short ID. */
  readonly logs: ReadonlyMap<number, { readonly name: string; readonly summary: RadioSummary }>;
  /** Logs from this deployment that could not be tied to one selected device, with why. */
  readonly unmatched: readonly string[];
}

export type Positions = ReadonlyMap<number, { readonly x: number; readonly y: number }>;

export const shortId = (uid: number) => uid.toString(16).toUpperCase().padStart(2, '0');

/** Every deployment among the loaded logs, the one with the most logs first. */
export function groupRadioDeployments(reports: readonly DeviceReport[]): RadioDeployment[] {
  const groups = new Map<string, DeviceReport[]>();
  for (const report of reports) {
    const key = report.radio ? radioDeploymentKey(report.radio) : null;
    if (!key) continue;
    groups.set(key, [...(groups.get(key) ?? []), report]);
  }

  const deployments = [...groups].map(([key, members]): RadioDeployment => {
    const first = members[0]!.radio!;
    const devices = first.deploymentUids.map((uid, i) => ({ uid, label: first.deploymentLabels[i]?.trim() || shortId(uid) }));
    const logs = new Map<number, { name: string; summary: RadioSummary }>();
    const unmatched: string[] = [];
    for (const report of members) {
      const uid = identifyRadioLog(report.radio!, report.name, report.deviceUid);
      if (uid === null) {
        unmatched.push(`${report.name}: could not tell which device wrote it`);
        continue;
      }
      // The same device loaded twice, most often a re-download: keep the copy that covers more time
      const existing = logs.get(uid);
      const span = (summary: RadioSummary) => (summary.lastMs ?? 0) - (summary.firstMs ?? 0);
      if (existing && span(existing.summary) >= span(report.radio!)) {
        unmatched.push(`${report.name}: a second log for ${shortId(uid)}; the longer one is used`);
        continue;
      }
      if (existing) unmatched.push(`${existing.name}: a second log for ${shortId(uid)}; the longer one is used`);
      logs.set(uid, { name: report.name, summary: report.radio! });
    }
    return { key, startTime: first.experimentStartTime!, devices, logs, unmatched };
  });
  return deployments.sort((a, b) => b.logs.size - a.logs.size || b.startTime - a.startTime);
}

export function runRadioCheck(deployment: RadioDeployment, positions: Positions): RadioCheckResult {
  return analyseRadio({
    devices: deployment.devices.map((device) => ({ ...device, summary: deployment.logs.get(device.uid)?.summary ?? null })),
    positions,
  });
}

/** Evenly around a circle of the given radius, in metres, starting due east. */
export function circlePositions(uids: readonly number[], radius: number): Map<number, { x: number; y: number }> {
  return new Map(uids.map((uid, i) => {
    const angle = (2 * Math.PI * i) / uids.length;
    return [uid, { x: round(radius * Math.cos(angle)), y: round(radius * Math.sin(angle)) }];
  }));
}

/** In a straight line at the given spacing, in metres. */
export function linePositions(uids: readonly number[], spacing: number): Map<number, { x: number; y: number }> {
  return new Map(uids.map((uid, i) => [uid, { x: round(i * spacing), y: 0 }]));
}

const round = (value: number) => Math.round(value * 1000) / 1000;

export const formatPercent = (fraction: number | null) => (fraction === null ? '—' : `${Math.round(fraction * 100)}%`);
export const formatMm = (millimetres: number | null) => (millimetres === null ? '—' : `${Math.round(millimetres)} mm`);
