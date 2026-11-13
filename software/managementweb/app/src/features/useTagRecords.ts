// What this browser remembers about tags between visits, shared by every page.

import { useCallback } from 'react';
import { formatEui } from '@tottag/schema';
import { createSharedStore, useSharedStore } from '../lib/sharedStore.ts';
import { downloadKey, type TagCheck } from './tagStatus.ts';

const isObject = (raw: unknown): raw is Record<string, unknown> => !!raw && typeof raw === 'object' && !Array.isArray(raw);

// --- The last reading of each tag ------------------------------------------------------------------

const checks = createSharedStore<Record<string, TagCheck>>(
  'tottag.tag-checks',
  (raw) => (isObject(raw) ? (raw as Record<string, TagCheck>) : {}),
  () => ({}),
);

/** The most recent reading of each tag, by address, from whichever page took it. */
export function useTagChecks() {
  const [all, set] = useSharedStore(checks);
  const record = useCallback((eui: Uint8Array, check: TagCheck) => {
    set((current) => ({ ...current, [formatEui(eui)]: check }));
  }, [set]);
  const get = useCallback((eui: Uint8Array): TagCheck | null => all[formatEui(eui)] ?? null, [all]);
  return { get, record };
}

// --- Deployments whose data has been saved --------------------------------------------------------

const downloads = createSharedStore<Record<string, string>>(
  'tottag.downloaded',
  (raw) => (isObject(raw) ? (raw as Record<string, string>) : {}),
  () => ({}),
);

/**
 * Deployments whose log has been saved to disk, or opened from disk, in this browser.
 *
 * Keyed by the tag's one-byte ID and the deployment's start, which is everything a log is sure to
 * carry. Used to tell someone about to replace a tag's deployment that its data is already safe, so
 * the usual case — download everything, then set up the next study — needs no extra confirmation.
 */
export function useDownloaded() {
  const [all, set] = useSharedStore(downloads);
  const markDownloaded = useCallback((shortUid: number, startTime: number) => {
    const key = downloadKey(shortUid, startTime);
    set((current) => (current[key] ? current : { ...current, [key]: new Date().toISOString() }));
  }, [set]);
  const downloadedAt = useCallback(
    (shortUid: number, startTime: number): string | null => all[downloadKey(shortUid, startTime)] ?? null,
    [all],
  );
  return { markDownloaded, downloadedAt };
}

// --- The last deployment record --------------------------------------------------------------------

export interface LastRecord {
  /** File name without extension. */
  readonly base: string;
  readonly json: string;
  readonly text: string;
  readonly writtenAt: string;
  readonly saved: boolean;
}

const lastRecord = createSharedStore<LastRecord | null>(
  'tottag.last-record',
  (raw) => (isObject(raw) && typeof raw.json === 'string' && typeof raw.text === 'string' ? (raw as unknown as LastRecord) : null),
  () => null,
);

/**
 * The record of the most recent write, kept until it is saved and after.
 *
 * It used to live only in the page, so switching to another page or reloading before saving it lost
 * the one answer to "what did we actually deploy?" — and getting it back meant writing every tag
 * again.
 */
export function useLastRecord() {
  const [record, set] = useSharedStore(lastRecord);
  const markSaved = useCallback(() => set((current) => (current ? { ...current, saved: true } : current)), [set]);
  return { record, setRecord: set, markSaved };
}
