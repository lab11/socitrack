import { useCallback } from 'react';
import { formatEui, tagName } from '@tottag/schema';
import { createSharedStore, useSharedStore } from '../lib/sharedStore.ts';
import type { TagIdentity, TagTransport } from '../ports/tagTransport.ts';

export interface KnownTag {
  /** Browser-assigned, stable within this profile. What `connect()` takes. */
  readonly handle: string;
  /** Which transport `handle` is for. Absent on entries saved before USB existed, which were all Bluetooth. */
  readonly transport?: TagTransport['id'];
  /** Full 6-byte EUI, least-significant byte first. */
  readonly eui: readonly number[];
  readonly firstSeen: string;
  readonly lastSeen: string;
}

/**
 * Deliberately no label. Labels belong to a study — "10051_CG1" means one family's caregiver — and a
 * tag remembered with last month's label would offer it to next month's family, which is exactly the
 * kind of slip that ends up in the data.
 */
const store = createSharedStore<KnownTag[]>(
  'tottag.known-tags',
  (raw) => (Array.isArray(raw) ? raw : [])
    .filter((entry): entry is KnownTag => !!entry && typeof entry.handle === 'string' && Array.isArray(entry.eui))
    .map(({ handle, transport, eui, firstSeen, lastSeen }) => ({ handle, transport: transport ?? 'bluetooth', eui, firstSeen, lastSeen })),
  () => [],
);

/**
 * Tags this browser has been granted access to, remembered across sessions.
 *
 * Exists because identifying a tag costs a connection: Web Bluetooth never exposes a MAC, so the
 * EUI has to be read from GATT System ID. Without a cache, listing known tags would mean connecting
 * to every one of them just to find out what they are — which on ten tags is slower than the
 * dialogs it was meant to replace.
 *
 * So each tag is identified once, and the EUI is remembered against the browser's handle for it.
 * After that a known tag can be added to a deployment instantly, and reached again whenever there is
 * something to write — including after switching pages or reloading.
 */
export function useKnownTags() {
  const [tags, setTags] = useSharedStore(store);

  const remember = useCallback((identity: TagIdentity) => {
    const now = new Date().toISOString();
    setTags((current) => {
      const existing = current.find((tag) => tag.handle === identity.handle);
      if (!existing) {
        return [...current, { handle: identity.handle, transport: identity.transport, eui: Array.from(identity.eui), firstSeen: now, lastSeen: now }];
      }
      return current.map((tag) => (tag.handle === identity.handle ? { ...tag, eui: Array.from(identity.eui), lastSeen: now } : tag));
    });
  }, [setTags]);

  const forget = useCallback((handle: string) => {
    setTags((current) => current.filter((tag) => tag.handle !== handle));
  }, [setTags]);

  return { tags, remember, forget };
}

export const knownTagTransport = (tag: KnownTag): TagTransport['id'] => tag.transport ?? 'bluetooth';

/**
 * How to reach a tag by its address: Bluetooth when this browser knows it that way, else USB.
 *
 * Bluetooth first, because a tag on its charger is the normal case and USB needs a cable to this
 * computer that is probably no longer there.
 */
export function routeFor(tags: readonly KnownTag[], eui: Uint8Array | readonly number[]): KnownTag | null {
  const key = formatEui(Uint8Array.from(eui));
  const matching = tags.filter((tag) => formatEui(Uint8Array.from(tag.eui)) === key);
  return matching.find((tag) => knownTagTransport(tag) === 'bluetooth') ?? matching[0] ?? null;
}

/**
 * How a known tag is named on screen: "TotTag 00:0A", and for one reached over USB a note saying so.
 *
 * The same tag can be known both ways, and the two are reached differently — one needs it on its
 * charger and in range, the other plugged into this computer — so they must not look identical.
 */
export const describeKnownTag = (tag: KnownTag): string =>
  `${tagName(tag.eui)}${knownTagTransport(tag) === 'serial' ? ' (USB)' : ''}`;
