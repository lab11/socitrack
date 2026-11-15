import { useCallback, useEffect, useState } from 'react';
import { defaultDraft, migrateV1Draft, nextDraft, type DeploymentDraft, type DraftDevice } from './schedule.ts';

const STORAGE_KEY = 'tottag.deployment-draft';

/** 2: wall-clock readings and a time zone, where 1 held Unix times and daily hours on the tag's clock. */
const DRAFT_VERSION = 2;

interface StoredDraft {
  readonly version: number;
  readonly draft: Omit<DeploymentDraft, 'devices'> & {
    devices: Array<{ eui: number[]; label: string; replaceAllowed?: number | null }>;
  };
}

function restore(): DeploymentDraft | null {
  try {
    const raw = localStorage.getItem(STORAGE_KEY);
    if (!raw) return null;
    const stored = JSON.parse(raw) as { version: number; draft?: StoredDraft['draft']; config?: Parameters<typeof migrateV1Draft>[0] };
    if (stored.version === 1 && stored.config) return migrateV1Draft(stored.config);
    if (stored.version !== DRAFT_VERSION || !stored.draft) return null;
    return {
      ...defaultDraft(Date.now() / 1000),
      ...stored.draft,
      devices: stored.draft.devices.map((device) => ({
        eui: Uint8Array.from(device.eui),
        label: device.label,
        replaceAllowed: device.replaceAllowed ?? null,
      })),
    };
  } catch {
    return null;
  }
}

const sameEui = (a: Uint8Array, b: Uint8Array) => a.length === b.length && a.every((byte, i) => byte === b[i]);

export type DraftApi = ReturnType<typeof useDeploymentDraft>;

/**
 * The deployment being configured.
 *
 * Written to local storage on every change. A researcher entering ten tags and a schedule has done
 * ten minutes of careful work, and losing it to a stray reload or a closed tab is the kind of thing
 * that makes people go back to the desktop tool.
 *
 * Because the draft is autosaved, there is deliberately NO "you have unsaved changes" prompt for it.
 * Such a prompt would warn about a risk that does not exist and would train people to click through
 * prompts that do.
 *
 * Held by the app shell rather than by the setup page, so that the other pages can read the study's
 * time zone and labels, and so that switching pages never discards it.
 */
export function useDeploymentDraft() {
  const [draft, setDraft] = useState<DeploymentDraft>(() => restore() ?? defaultDraft(Date.now() / 1000));
  /** Bumped by startNew(), so anything describing the previous deployment knows to let it go. */
  const [generation, setGeneration] = useState(0);

  useEffect(() => {
    try {
      const storable: StoredDraft = {
        version: DRAFT_VERSION,
        draft: {
          ...draft,
          devices: draft.devices.map((d) => ({ eui: Array.from(d.eui), label: d.label, replaceAllowed: d.replaceAllowed ?? null })),
        },
      };
      localStorage.setItem(STORAGE_KEY, JSON.stringify(storable));
    } catch {
      // A full or disabled store must not break editing; the draft simply stops surviving reloads.
    }
  }, [draft]);

  const update = useCallback((patch: Partial<DeploymentDraft>) => {
    setDraft((current) => ({ ...current, ...patch }));
  }, []);

  /** Ignores a tag already in the deployment: adding it twice is a slip, and callers say so. */
  const addDevice = useCallback((device: DraftDevice) => {
    setDraft((current) =>
      current.devices.some((existing) => sameEui(existing.eui, device.eui))
        ? current
        : { ...current, devices: [...current.devices, device] });
  }, []);

  const updateDevice = useCallback((eui: Uint8Array, patch: Partial<DraftDevice>) => {
    setDraft((current) => ({
      ...current,
      devices: current.devices.map((device) => (sameEui(device.eui, eui) ? { ...device, ...patch } : device)),
    }));
  }, []);

  const removeDevice = useCallback((eui: Uint8Array) => {
    setDraft((current) => ({ ...current, devices: current.devices.filter((device) => !sameEui(device.eui, eui)) }));
  }, []);

  /** Clears dates, tags and labels; keeps the time zone and daily hours, which a lab reuses study after study. */
  const startNew = useCallback(() => {
    setDraft((current) => nextDraft(current, Date.now() / 1000));
    setGeneration((current) => current + 1);
  }, []);

  return { draft, generation, update, addDevice, updateDevice, removeDevice, startNew };
}
