// File import. One of the adapters in this app allowed to touch a browser API.
//
// Uses a plain <input type="file"> rather than the File System Access API. That is a deliberate
// trade: File System Access would allow writing exports back to the same folder without a second
// prompt, but it exists only in Chromium, and file import is the one path that has to work
// everywhere — it is what makes the app usable on a locked-down study laptop with Safari.

import type { LoadedLog, LoadFailure, LoadResult, LogSource } from '../ports/logSource.ts';
import { canPickDirectory, canPickFiles, pickDirectory, pickFiles } from './fileSystemAccess.ts';

/** Extensions offered in the picker. Not a validation: the parser decides what a file really is. */
const ACCEPT = '.ttg,application/octet-stream';

/** Refuse absurd inputs before reading them into memory. A 1 GiB part cannot produce more. */
const MAX_BYTES = 1_200_000_000;

async function readOne(file: File): Promise<LoadedLog | LoadFailure> {
  if (file.size === 0) {
    return { name: file.name, reason: 'The file is empty — 0 bytes. The download may not have finished.' };
  }
  if (file.size > MAX_BYTES) {
    return {
      name: file.name,
      reason: `The file is ${(file.size / 1e9).toFixed(1)} GB, which is larger than any TotTag can produce.`,
    };
  }
  try {
    const bytes = new Uint8Array(await file.arrayBuffer());
    return {
      name: file.name,
      bytes,
      origin: 'file' as const,
      modifiedAt: file.lastModified ? new Date(file.lastModified) : undefined,
    };
  } catch (error) {
    return {
      name: file.name,
      reason: error instanceof Error ? error.message : 'The file could not be read.',
    };
  }
}

function partition(results: Array<LoadedLog | LoadFailure>): LoadResult {
  const loaded: LoadedLog[] = [];
  const failed: LoadFailure[] = [];
  for (const result of results) {
    if ('bytes' in result) loaded.push(result);
    else failed.push(result);
  }
  return { loaded, failed };
}

/** Read a set of files the user has already chosen — the drag-and-drop path. */
export async function readFiles(files: readonly File[]): Promise<LoadResult> {
  return partition(await Promise.all(files.map(readOne)));
}

export const fileLogSource: LogSource = {
  id: 'file',
  label: 'Open log files',
  isAvailable: () => true,
  unavailableReason: () => null,

  async load(): Promise<LoadResult> {
    // Prefer the File System Access picker where it exists: cancellation is reported properly
    // rather than inferred from a focus event and a timer, which is what the fallback below has to
    // do. Same result either way, so nothing downstream needs to know which ran.
    if (canPickFiles()) {
      const chosen = await pickFiles();
      return chosen === null ? { loaded: [], failed: [] } : readFiles(chosen);
    }

    const input = document.createElement('input');
    input.type = 'file';
    input.multiple = true;
    input.accept = ACCEPT;

    const files = await new Promise<File[]>((resolve) => {
      // `cancel` fires in current browsers; the focus fallback covers the rest. Without one of the
      // two, dismissing the picker would leave the caller awaiting forever.
      let settled = false;
      const finish = (chosen: File[]) => {
        if (settled) return;
        settled = true;
        resolve(chosen);
      };
      input.addEventListener('change', () => finish(Array.from(input.files ?? [])));
      input.addEventListener('cancel', () => finish([]));
      window.addEventListener('focus', () => setTimeout(() => finish([]), 500), { once: true });
      input.click();
    });

    return readFiles(files);
  },
};

/**
 * Open every .ttg in a folder.
 *
 * Only available on browsers with `showDirectoryPicker`. With a dozen tags this is the difference
 * between one gesture and a careful multi-select, which is why it is offered as its own action
 * rather than folded into the file picker.
 */
export const directoryLogSource: LogSource = {
  id: 'file',
  label: 'Open a folder of logs',
  isAvailable: canPickDirectory,
  unavailableReason: () =>
    canPickDirectory()
      ? null
      : 'This browser cannot open a whole folder. Chrome or Edge can; here, select the files instead.',

  async load(): Promise<LoadResult> {
    if (!canPickDirectory()) return { loaded: [], failed: [] };
    const chosen = await pickDirectory();
    if (chosen === null) return { loaded: [], failed: [] };
    if (chosen.length === 0) {
      return { loaded: [], failed: [{ name: 'that folder', reason: 'No .ttg files were found directly inside it.' }] };
    }
    return readFiles(chosen);
  },
};
