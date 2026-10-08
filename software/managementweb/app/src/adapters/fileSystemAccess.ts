// The File System Access API, used when the browser has it.
//
// Why this exists, and why it is not the only path:
//
// The import and export adapters are built on two APIs that work everywhere — `<input type="file">`
// and a Blob object-URL download. Those are the floor. The File System Access API is strictly
// better where it exists, and this module is the feature-detected upgrade:
//
//   showDirectoryPicker()  — pick the FOLDER the tags were downloaded into and read every .ttg in
//                            it. With twelve tags this is one gesture instead of twelve selections
//                            or one careful multi-select, and it is the single biggest usability
//                            difference between the two APIs for this app.
//   showOpenFilePicker()   — same as the input element, but cancellation throws AbortError instead
//                            of being undetectable. The fallback has to infer cancellation from a
//                            window focus event and a timeout, which is a guess; this is not.
//   showSaveFilePicker()   — the user chooses where an export goes and can overwrite the previous
//                            one in place. The fallback drops the file in the browser's download
//                            folder with a timestamped name and no say in it.
//
// The cost of using it EXCLUSIVELY would be Safari and Firefox: as of writing neither implements
// the picker entry points (both implement the Origin Private File System, which is a different
// thing and useless here — it is a sandbox the user cannot browse to). A study laptop running
// Safari is a realistic deployment target, so the fallback is not optional.
//
// Everything here is behind a runtime check rather than a build-time one, so the same bundle serves
// both. Nothing else in the app imports this module directly; it is reached through fileLogSource
// and download, which choose.

interface FileSystemAccessWindow {
  showOpenFilePicker?: (options?: unknown) => Promise<Array<{ getFile: () => Promise<File> }>>;
  showDirectoryPicker?: (options?: unknown) => Promise<AsyncIterable<[string, FileSystemHandleLike]>>;
  showSaveFilePicker?: (options?: unknown) => Promise<FileSystemFileHandleLike>;
}

interface FileSystemHandleLike {
  kind: 'file' | 'directory';
  name: string;
  getFile?: () => Promise<File>;
}

interface FileSystemFileHandleLike {
  createWritable: () => Promise<{ write: (data: string | Blob) => Promise<void>; close: () => Promise<void> }>;
}

const api = (): FileSystemAccessWindow => window as unknown as FileSystemAccessWindow;

export const canPickFiles = (): boolean => typeof api().showOpenFilePicker === 'function';
export const canPickDirectory = (): boolean => typeof api().showDirectoryPicker === 'function';
export const canPickSaveLocation = (): boolean => typeof api().showSaveFilePicker === 'function';

/** Thrown by every picker when the user dismisses it. Not an error worth showing. */
export function isAbort(error: unknown): boolean {
  return error instanceof DOMException && error.name === 'AbortError';
}

const TTG_TYPES = [{ description: 'TotTag logs', accept: { 'application/octet-stream': ['.ttg'] } }];

/** Returns null when the user cancelled. */
export async function pickFiles(): Promise<File[] | null> {
  try {
    const handles = await api().showOpenFilePicker!({ multiple: true, types: TTG_TYPES });
    return await Promise.all(handles.map((handle) => handle.getFile()));
  } catch (error) {
    if (isAbort(error)) return null;
    throw error;
  }
}

/**
 * Every .ttg directly inside a chosen folder. Returns null when the user cancelled.
 *
 * Deliberately not recursive. A researcher picking their Downloads folder should get the logs they
 * just downloaded, not a descent through every subfolder — and a silent recursive walk of an
 * arbitrary directory is a surprising amount of reading to do on someone's behalf.
 */
export async function pickDirectory(): Promise<File[] | null> {
  try {
    const directory = await api().showDirectoryPicker!({ mode: 'read' });
    const files: File[] = [];
    for await (const [name, handle] of directory) {
      if (handle.kind === 'file' && name.toLowerCase().endsWith('.ttg') && handle.getFile) {
        files.push(await handle.getFile());
      }
    }
    return files;
  } catch (error) {
    if (isAbort(error)) return null;
    throw error;
  }
}

/** What kind of file is being written, for the picker's filter and the blob's type. */
export interface SaveKind {
  readonly description: string;
  readonly mimeType: string;
  readonly extension: string;
}

/**
 * Write to a location the user chooses. Returns false when they cancelled.
 *
 * Note the two failure modes that are NOT cancellation and do propagate: the picker throws
 * `SecurityError` when it is called without transient user activation — which is why every caller
 * has to be reached directly from a click and not from the completion of a long transfer — and
 * `createWritable` throws if permission to write was refused after the location was chosen.
 */
export async function saveFile(suggestedName: string, data: string | Blob, kind: SaveKind): Promise<boolean> {
  try {
    const handle = await api().showSaveFilePicker!({
      suggestedName,
      types: [{ description: kind.description, accept: { [kind.mimeType]: [kind.extension] } }],
    });
    const writable = await handle.createWritable();
    await writable.write(data);
    await writable.close();
    return true;
  } catch (error) {
    if (isAbort(error)) return false;
    throw error;
  }
}
