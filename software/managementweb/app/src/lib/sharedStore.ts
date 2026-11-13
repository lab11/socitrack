// State that more than one page reads, kept in this browser's local storage.
//
// Every page stays mounted now, so the same list read through two separate useState hooks would
// diverge: a tag added on the setup page would not appear on the check page until a reload. One store
// per key, shared by every component that asks for it, is what keeps them saying the same thing.

import { useSyncExternalStore } from 'react';

export interface SharedStore<T> {
  get(): T;
  set(next: T | ((current: T) => T)): void;
  subscribe(listener: () => void): () => void;
}

/**
 * A value persisted under `key`. `read` turns whatever is stored into a valid value, tolerating
 * entries written by earlier versions; anything it cannot read falls back to `initial`.
 */
export function createSharedStore<T>(key: string, read: (raw: unknown) => T, initial: () => T): SharedStore<T> {
  let loaded = false;
  let value: T;
  const listeners = new Set<() => void>();

  const get = (): T => {
    if (!loaded) {
      loaded = true;
      try {
        const raw = globalThis.localStorage?.getItem(key);
        value = raw ? read(JSON.parse(raw)) : initial();
      } catch {
        value = initial();
      }
    }
    return value;
  };

  return {
    get,
    set(next) {
      value = typeof next === 'function' ? (next as (current: T) => T)(get()) : next;
      try {
        globalThis.localStorage?.setItem(key, JSON.stringify(value));
      } catch {
        // A full or disabled store costs only the convenience of remembering between sessions.
      }
      for (const listener of listeners) listener();
    },
    subscribe(listener) {
      listeners.add(listener);
      return () => listeners.delete(listener);
    },
  };
}

export function useSharedStore<T>(store: SharedStore<T>): [T, SharedStore<T>['set']] {
  const value = useSyncExternalStore(store.subscribe, store.get, store.get);
  return [value, store.set];
}
