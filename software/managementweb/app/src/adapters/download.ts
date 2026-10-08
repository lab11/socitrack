// Handing a generated file to the user. One of the adapters allowed to touch browser APIs.
//
// Two paths, chosen at runtime. Where `showSaveFilePicker` exists the user picks the name and
// location and can overwrite a previous save in place; everywhere else — Safari and Firefox, at time
// of writing — it is an object URL and a synthetic anchor click, which lands the file in the
// browser's download folder under the suggested name with no say in it. The fallback is not
// optional: a study laptop running Safari is a realistic target.
//
// Both paths must be reached directly from a click. `showSaveFilePicker` requires transient user
// activation and throws without it, so nothing here may be called from the completion of a
// long-running transfer — the activation from the button that STARTED the transfer is long gone by
// the time it finishes.

import { canPickSaveLocation, saveFile, type SaveKind } from './fileSystemAccess.ts';

// Keyed by extension so the caller names the file and nothing else. The picker's filter and the
// blob's type then cannot disagree with it, which they previously did: every save went through a
// picker filtered to `.csv` regardless of what was being written.
const KINDS: Readonly<Record<string, SaveKind>> = {
  '.csv': { description: 'CSV', mimeType: 'text/csv', extension: '.csv' },
  '.json': { description: 'JSON', mimeType: 'application/json', extension: '.json' },
  '.txt': { description: 'Text', mimeType: 'text/plain', extension: '.txt' },
  '.ttg': { description: 'TotTag log', mimeType: 'application/octet-stream', extension: '.ttg' },
};
const FALLBACK: SaveKind = { description: 'File', mimeType: 'application/octet-stream', extension: '' };

function kindOf(filename: string): SaveKind {
  const dot = filename.lastIndexOf('.');
  return (dot < 0 ? undefined : KINDS[filename.slice(dot).toLowerCase()]) ?? FALLBACK;
}

/**
 * Offer generated text — a summary export, a deployment manifest — to the user as a file.
 *
 * False means the user dismissed the save dialog. The fallback path cannot tell and reports true.
 */
export async function downloadText(filename: string, content: string): Promise<boolean> {
  const kind = kindOf(filename);
  return offer(filename, new Blob([content], { type: `${kind.mimeType};charset=utf-8` }), kind);
}

/**
 * Offer a log's raw stream to the user as a `.ttg`.
 *
 * Written verbatim, not re-encoded: what lands on disk is byte-for-byte what the tag sent, so a file
 * saved here and one saved by `tottag.py` from the same device are the same file, and re-importing
 * it exercises exactly the path an imported file exercises.
 */
export async function downloadBytes(filename: string, bytes: Uint8Array): Promise<boolean> {
  const kind = kindOf(filename);
  // A fresh copy of the buffer: `bytes` may be a view onto a larger allocation, and Blob would
  // otherwise capture the whole of it.
  return offer(filename, new Blob([bytes.slice()], { type: kind.mimeType }), kind);
}

async function offer(filename: string, blob: Blob, kind: SaveKind): Promise<boolean> {
  if (canPickSaveLocation()) return saveFile(filename, blob, kind);
  const url = URL.createObjectURL(blob);
  const anchor = document.createElement('a');
  anchor.href = url;
  anchor.download = filename;
  document.body.appendChild(anchor);
  anchor.click();
  anchor.remove();
  // Revoking immediately races the download in some browsers; a turn of the event loop is enough.
  setTimeout(() => URL.revokeObjectURL(url), 0);
  return true;
}
