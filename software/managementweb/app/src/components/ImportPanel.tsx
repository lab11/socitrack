import { useCallback, useRef, useState } from 'react';
import { directoryLogSource, fileLogSource, readFiles } from '../adapters/fileLogSource.ts';
import type { LoadResult } from '../ports/logSource.ts';

interface Props {
  onLoaded: (result: LoadResult) => void;
  onOpenFolder: () => void;
  busy: boolean;
  /** Compact form, for when logs are already open and this moves out of the way. */
  compact?: boolean;
}

/**
 * The way logs get in.
 *
 * Drag-and-drop plus a button, because the files arrive in a folder and dragging eight of them is
 * one gesture. The drop target is the whole panel rather than a small inner rectangle: a narrow
 * target is the difference between "this is easy" and "this is fiddly" when the files are the point
 * of the whole exercise.
 */
export function ImportPanel({ onLoaded, onOpenFolder, busy, compact = false }: Props) {
  const folderAvailable = directoryLogSource.isAvailable();
  const [dragging, setDragging] = useState(false);
  const depth = useRef(0);

  const onDrop = useCallback(
    async (event: React.DragEvent) => {
      event.preventDefault();
      depth.current = 0;
      setDragging(false);
      const files = Array.from(event.dataTransfer.files);
      if (files.length) onLoaded(await readFiles(files));
    },
    [onLoaded],
  );

  // dragenter/dragleave fire for every child element crossed, so a naive boolean flickers. Counting
  // depth is the standard fix and the reason this is not just useState(false).
  const onDragEnter = useCallback((event: React.DragEvent) => {
    event.preventDefault();
    depth.current += 1;
    setDragging(true);
  }, []);

  const onDragLeave = useCallback((event: React.DragEvent) => {
    event.preventDefault();
    depth.current -= 1;
    if (depth.current <= 0) {
      depth.current = 0;
      setDragging(false);
    }
  }, []);

  const openPicker = useCallback(async () => {
    onLoaded(await fileLogSource.load());
  }, [onLoaded]);

  return (
    <section
      className={`import${dragging ? ' import--dragging' : ''}${compact ? ' import--compact' : ''}`}
      onDrop={onDrop}
      onDragEnter={onDragEnter}
      onDragLeave={onDragLeave}
      onDragOver={(event) => event.preventDefault()}
      aria-busy={busy}
    >
      {!compact && <h2 className="import__title">Open your deployment logs</h2>}
      <p className="import__body">
        {compact ? 'Drop more log files here, or ' : 'Drag the .ttg files from your tags here, or '}
        <button type="button" className="link" onClick={openPicker} disabled={busy}>
          browse for them
        </button>
        {folderAvailable && (
          <>
            ,{' '}or{' '}
            <button type="button" className="link" onClick={onOpenFolder} disabled={busy}>
              open a whole folder
            </button>
          </>
        )}
        .
      </p>
      {!compact && (
        <p className="import__note">
          Nothing leaves this computer. Files are read in the browser and never uploaded.
        </p>
      )}
    </section>
  );
}
