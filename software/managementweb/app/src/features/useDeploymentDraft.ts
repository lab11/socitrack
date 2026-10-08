import { useCallback, useEffect, useState } from 'react';
import type { DeploymentConfig, DeploymentDevice } from '@tottag/schema';

const STORAGE_KEY = 'tottag.deployment-draft';

interface StoredDraft {
  readonly version: number;
  readonly config: Omit<DeploymentConfig, 'devices'> & {
    devices: Array<{ eui: number[]; label: string }>;
  };
}

const DRAFT_VERSION = 1;

function defaultConfig(): DeploymentConfig {
  const startOfTomorrow = new Date();
  startOfTomorrow.setHours(0, 0, 0, 0);
  startOfTomorrow.setDate(startOfTomorrow.getDate() + 1);
  const start = Math.floor(startOfTomorrow.getTime() / 1000);
  return {
    startTime: start,
    endTime: start + 7 * 86400,
    timezone: Intl.DateTimeFormat().resolvedOptions().timeZone || 'UTC',
    useDailyTimes: false,
    dailyStartTime: 13 * 3600,
    dailyEndTime: 3 * 3600,
    devices: [],
  };
}

function restore(): DeploymentConfig | null {
  try {
    const raw = localStorage.getItem(STORAGE_KEY);
    if (!raw) return null;
    const stored = JSON.parse(raw) as StoredDraft;
    if (stored.version !== DRAFT_VERSION) return null;
    return {
      ...stored.config,
      devices: stored.config.devices.map((device) => ({
        eui: Uint8Array.from(device.eui),
        label: device.label,
      })),
    };
  } catch {
    return null;
  }
}

/**
 * The deployment being configured.
 *
 * Written to local storage on every change. A researcher entering ten tags and a schedule has done
 * ten minutes of careful work with no undo, and losing it to a stray reload or a closed tab is the
 * kind of thing that makes people go back to the desktop tool.
 *
 * Because the draft is autosaved, there is deliberately NO "you have unsaved changes" prompt
 * anywhere in this app. Such a prompt would warn about a risk that does not exist and would train
 * people to click through prompts that do.
 */
export function useDeploymentDraft() {
  const [config, setConfig] = useState<DeploymentConfig>(() => restore() ?? defaultConfig());

  useEffect(() => {
    try {
      const storable: StoredDraft = {
        version: DRAFT_VERSION,
        config: { ...config, devices: config.devices.map((d) => ({ eui: Array.from(d.eui), label: d.label })) },
      };
      localStorage.setItem(STORAGE_KEY, JSON.stringify(storable));
    } catch {
      // A full or disabled store must not break editing; the draft simply stops surviving reloads.
    }
  }, [config]);

  const update = useCallback((patch: Partial<DeploymentConfig>) => {
    setConfig((current) => ({ ...current, ...patch }));
  }, []);

  const addDevice = useCallback((device: DeploymentDevice) => {
    setConfig((current) =>
      // Adding the same tag twice is a slip, not an intent, and it would read as a UID conflict
      // with itself — confusing, and hiding whatever the real problem was.
      current.devices.some((existing) => existing.eui[0] === device.eui[0] && existing.eui.every((b, i) => b === device.eui[i]))
        ? current
        : { ...current, devices: [...current.devices, device] },
    );
  }, []);

  const updateDevice = useCallback((index: number, patch: Partial<DeploymentDevice>) => {
    setConfig((current) => ({
      ...current,
      devices: current.devices.map((device, i) => (i === index ? { ...device, ...patch } : device)),
    }));
  }, []);

  const removeDevice = useCallback((index: number) => {
    setConfig((current) => ({ ...current, devices: current.devices.filter((_, i) => i !== index) }));
  }, []);

  const reset = useCallback(() => setConfig(defaultConfig()), []);

  return { config, update, addDevice, updateDevice, removeDevice, reset };
}
