import { useCallback, useEffect, useState } from 'react';
import { formatEui } from '@tottag/schema';
import type { TagIdentity, TagTransport } from '../ports/tagTransport.ts';

const STORAGE_KEY = 'tottag.known-tags';

export interface KnownTag {
  /** Browser-assigned, stable within this profile. What `connect()` takes. */
  readonly handle: string;
  /** Which transport `handle` is for. Absent on entries saved before USB existed, which were all Bluetooth. */
  readonly transport?: TagTransport['id'];
  /** Full 6-byte EUI, least-significant byte first. */
  readonly eui: readonly number[];
  /** Whatever this tag was last labelled, offered as a default next time. */
  readonly lastLabel: string;
  readonly firstSeen: string;
  readonly lastSeen: string;
}

/**
 * Tags this browser has been granted access to, remembered across sessions.
 *
 * Exists because identifying a tag costs a connection: Web Bluetooth never exposes a MAC, so the
 * EUI has to be read from GATT System ID. Without a cache, listing known tags would mean connecting
 * to every one of them just to find out what they are — which on ten tags is slower than the
 * dialogs it was meant to replace.
 *
 * So each tag is identified once, and the EUI is remembered against the browser's handle for it.
 * After that a known tag can be added to a deployment instantly, and the connection happens only
 * when there is something to write.
 */
export function useKnownTags() {
  const [tags, setTags] = useState<KnownTag[]>(() => {
    try {
      const raw = localStorage.getItem(STORAGE_KEY);
      return raw ? (JSON.parse(raw) as KnownTag[]) : [];
    } catch {
      return [];
    }
  });

  useEffect(() => {
    try {
      localStorage.setItem(STORAGE_KEY, JSON.stringify(tags));
    } catch {
      // A full or disabled store only costs the convenience of remembering between sessions.
    }
  }, [tags]);

  const remember = useCallback((identity: TagIdentity, label: string): KnownTag => {
    const now = new Date().toISOString();
    const entry: KnownTag = {
      handle: identity.handle,
      transport: identity.transport,
      eui: Array.from(identity.eui),
      lastLabel: label,
      firstSeen: now,
      lastSeen: now,
    };
    setTags((current) => {
      const existing = current.find((tag) => tag.handle === identity.handle);
      if (!existing) return [...current, entry];
      return current.map((tag) =>
        tag.handle === identity.handle
          ? { ...tag, eui: entry.eui, lastLabel: label || tag.lastLabel, lastSeen: now }
          : tag,
      );
    });
    return entry;
  }, []);

  const forget = useCallback((handle: string) => {
    setTags((current) => current.filter((tag) => tag.handle !== handle));
  }, []);

  return { tags, remember, forget };
}

export const knownTagTransport = (tag: KnownTag): TagTransport['id'] => tag.transport ?? 'bluetooth';

/**
 * How a tag is named on screen: its address, and for a tag reached over USB a prefix saying so.
 *
 * The same tag can be known both ways, and the two are reached differently — one needs it on its
 * charger and in range, the other plugged into this computer — so they must not look identical.
 * Matches the desktop tool, which lists a USB tag as "USB-Connected XX:XX:XX:XX:XX:XX".
 */
export const tagDisplayName = (eui: Uint8Array, transport: TagTransport['id']): string =>
  `${transport === 'serial' ? 'USB-Connected ' : ''}${formatEui(eui)}`;

export const describeKnownTag = (tag: KnownTag): string =>
  `${tagDisplayName(Uint8Array.from(tag.eui), knownTagTransport(tag))}${tag.lastLabel ? ` — ${tag.lastLabel}` : ''}`;
