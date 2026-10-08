// Turning loaded bytes into something the UI can render. Pure: no browser APIs, no I/O.
//
// Kept out of components so it can be reasoned about and, later, tested without a DOM. The
// components below it render what this returns and make no decisions of their own.

import type { DeploymentHealth, ParseReport, RadioSummary } from '@tottag/schema';
import { parseLogAsync } from '../lib/parseAsync.ts';
import { canonicalLogName } from './logFilename.ts';
import type { LoadedLog } from '../ports/logSource.ts';

export type DeviceStatus = 'ok' | 'note' | 'warning' | 'error';

export interface DeviceReport {
  readonly id: string;
  readonly name: string;
  readonly origin: LoadedLog['origin'];
  readonly bytes: number;
  /**
   * The stream itself, retained ONLY for a log that exists nowhere but this page.
   *
   * A live offload takes minutes and leaves no file behind, so until the user saves it the browser's
   * heap is the only copy in existence and dropping it would be destroying data. An imported file is
   * already on disk, so its bytes are released once it has been parsed — re-reading a megabyte from
   * disk is free, and holding a dozen of them is not.
   */
  readonly data: Uint8Array | null;
  /** What `saveAs` should suggest for `data`. Matches what `tottag.py` would have written. */
  readonly saveName: string;
  /** Null when the file could not be parsed at all. */
  readonly report: ParseReport | null;
  readonly recordCount: number;
  readonly health: DeploymentHealth | null;
  /** The log reduced for the radio check. Null when parsing failed. */
  readonly radio: RadioSummary | null;
  /** Low EUI byte of the device, when the transport that fetched the log knew it. */
  readonly deviceUid: number | undefined;
  /** Present only when parsing failed outright. */
  readonly error: string | null;
  readonly status: DeviceStatus;
}

/**
 * The worst severity present, which is what the summary line reports.
 *
 * Deliberately does NOT collapse counts: "3 warnings" and "1 warning" are the same status but very
 * different situations, and the card shows both. This is only for sorting and for the status label.
 */
function severityOf(health: DeploymentHealth | null, error: string | null): DeviceStatus {
  // `error` is reserved for a file that could not be read at all. The analysis itself only ever
  // reports 'warning' or 'note': it describes a deployment, and a deployment that happened is never
  // an error, however badly it went.
  if (error || !health) return 'error';
  if (health.anomalies.some((anomaly) => anomaly.severity === 'warning')) return 'warning';
  if (health.anomalies.length > 0) return 'note';
  return 'ok';
}

/**
 * Parse and analyse one log.
 *
 * A parse failure is captured rather than thrown: importing eight tags and having one corrupt file
 * abort the whole batch is precisely the behaviour that makes a tool feel hostile after a field
 * season. Every log gets a card; a failed one says why.
 */
export async function buildReport(log: LoadedLog, index: number): Promise<DeviceReport> {
  let report: ParseReport | null = null;
  let health: DeploymentHealth | null = null;
  let radio: RadioSummary | null = null;
  let recordCount = 0;
  let experimentStartTime = 0;
  let error: string | null = null;

  try {
    const parsed = await parseLogAsync(log.bytes);
    report = parsed.report;
    health = parsed.health;
    radio = parsed.radio;
    recordCount = parsed.recordCount;
    experimentStartTime = parsed.experimentStartTime;
  } catch (caught) {
    error = caught instanceof Error ? caught.message : String(caught);
  }

  // A log that failed to parse still gets retained and still gets a name. That is the case where
  // keeping the bytes matters MOST: something is wrong with the transfer and the only way anyone
  // will find out what is to have the file to look at.
  const live = log.origin !== 'file';
  const saveName = live
    ? canonicalLogName({
        details: report?.details ?? null,
        experimentStartTime,
        deviceUid: log.deviceUid,
        fallback: log.name,
      })
    : log.name;

  return {
    id: `${index}-${log.name}`,
    name: live ? saveName : log.name,
    origin: log.origin,
    bytes: log.bytes.length,
    data: live ? log.bytes : null,
    saveName,
    report,
    recordCount,
    health,
    radio,
    deviceUid: log.deviceUid,
    error,
    status: severityOf(health, error),
  };
}

const STATUS_ORDER: Record<DeviceStatus, number> = { error: 0, warning: 1, note: 2, ok: 3 };

/** Worst first. Someone importing twelve tags wants the broken one at the top, not in alphabetical order. */
export function sortByUrgency(reports: readonly DeviceReport[]): DeviceReport[] {
  return [...reports].sort(
    (a, b) => STATUS_ORDER[a.status] - STATUS_ORDER[b.status] || a.name.localeCompare(b.name),
  );
}

export function formatBytes(bytes: number): string {
  if (bytes < 1024) return `${bytes} B`;
  if (bytes < 1024 * 1024) return `${(bytes / 1024).toFixed(0)} KB`;
  return `${(bytes / (1024 * 1024)).toFixed(1)} MB`;
}

export function formatDuration(seconds: number): string {
  if (!Number.isFinite(seconds) || seconds <= 0) return '—';
  const days = Math.floor(seconds / 86400);
  const hours = Math.floor((seconds % 86400) / 3600);
  const minutes = Math.floor((seconds % 3600) / 60);
  if (days > 0) return `${days}d ${hours}h`;
  if (hours > 0) return `${hours}h ${minutes}m`;
  return `${minutes}m`;
}

/**
 * Why a device restarted, in one short phrase.
 *
 * A card that says "Restarts: 1" and "Looks good" side by side with nothing joining them invites
 * exactly the wrong question. Every restart has a known cause in the log — that is what the reset
 * record and the charger heuristic are for — so say it rather than leaving the reader to reconcile
 * two numbers that look contradictory.
 */
export function describeReboots(health: DeploymentHealth): string | undefined {
  const reboots = health.reboots;
  if (reboots.length === 0) return undefined;

  const stalls = health.watchdogResets;
  const charger = reboots.filter((reboot) => reboot.chargerTransition).length;
  const graceful = reboots.filter((reboot) => reboot.kind === 'graceful').length;

  const parts: string[] = [];
  if (stalls > 0) parts.push(`${stalls} from a stall`);
  if (charger > 0) parts.push(`${charger} from the charger`);
  const explained = stalls + charger;
  const remaining = reboots.length - explained;
  // A graceful restart with no charger transition is an ordinary power-up, not a mystery.
  if (remaining > 0) parts.push(`${remaining} ${graceful >= remaining ? 'ordinary' : 'unexplained'}`);
  return parts.join(', ');
}
