import { useCallback, useEffect, useState } from 'react';
import { describeRate, describeRemaining, extractPageFrames, formatEui, mergeRepairs, tagName,
  NANDLOG_MAX_RETRANSMIT_PAGES, V2_RETRANSMIT_PAGE_ATTEMPTS, V2_RETRANSMIT_RETRY_ROUNDS } from '@tottag/schema';
import { parseLogAsync } from '../lib/parseAsync.ts';
import { webBluetoothTransport } from '../adapters/webBluetooth.ts';
import { webSerialTransport } from '../adapters/webSerial.ts';
import { describeKnownTag, knownTagTransport, useKnownTags, type KnownTag } from '../features/useKnownTags.ts';
import type { DownloadProgress, TagConnection, TagTransport } from '../ports/tagTransport.ts';
import type { LoadedLog } from '../ports/logSource.ts';

interface Props {
  onDownloaded: (log: LoadedLog) => void;
  busy: boolean;
}

/**
 * Pulling a log off a tag.
 *
 * The result is the offload stream verbatim — byte for byte what a `.ttg` on disk holds — so it goes
 * through exactly the same reader as an imported file. A live download and a file cannot disagree,
 * because there is only one parser.
 *
 * The tag must be on its charger. That is not a UI nicety: `nandlog` only serves a log in
 * maintenance mode, which the firmware enters when it detects the charger.
 */
export function DownloadFromTag({ onDownloaded, busy }: Props) {
  const { tags: knownTags, remember } = useKnownTags();
  const [progress, setProgress] = useState<DownloadProgress | null>(null);
  const [status, setStatus] = useState<string | null>(null);
  const [error, setError] = useState<string | null>(null);
  const reason = webBluetoothTransport.unavailableReason();

  // Until its stream header, the tag is reading through its whole log to total its size. How long that takes
  // is not known in advance, so the wait counts up rather than pretending to a percentage.
  const calculating = progress !== null && (progress.phase === 'starting' || progress.phase === 'header');
  const [calculatingSince, setCalculatingSince] = useState<number | null>(null);
  const [now, setNow] = useState(() => Date.now());
  useEffect(() => {
    if (!calculating) {
      setCalculatingSince(null);
      return undefined;
    }
    const since = Date.now();
    setCalculatingSince(since);
    setNow(since);
    const timer = setInterval(() => setNow(Date.now()), 1000);
    return () => clearInterval(timer);
  }, [calculating]);
  const serialReason = webSerialTransport.unavailableReason();

  const run = useCallback(
    async (transport: TagTransport, open: () => Promise<TagConnection | null>, label: string) => {
      setError(null);
      setStatus(null);
      setProgress(null);
      let connection: TagConnection | null = null;
      try {
        connection = await open();
        if (!connection) {
          // Distinguish the reasons, because they need different actions from the user.
          setError(
            transport.id === 'serial'
              ? 'Could not reach that tag. Plug it into this computer with its USB cable, then try again.'
              : webBluetoothTransport.reconnectAcrossReloadsSupported()
              ? 'Could not reach that tag. Make sure it is on its charger and close to this computer, then try again.'
              : 'After a reload, this browser needs you to pick the tag again. Use "Download from a tag" and pick it from the list.',
          );
          return;
        }
        let bytes = await connection.downloadLog(null, setProgress);
        if (bytes.length === 0) {
          setError('The tag sent no data. It has to be on its charger to serve a log.');
          return;
        }

        // Ask for whatever did not survive the transfer, up to a fixed number of rounds. Every page the
        // device still holds is one it can simply send again, and the transfer is the only part of this
        // path that loses data -- the flash itself has never lost a byte across any run.
        //
        // Each round MERGES what came back into the stream rather than keeping it alongside, so the
        // bytes that get saved are the repaired ones. A file that still lacks a page the tool went and
        // fetched would report holes on re-import that had already been fixed, and the recovery would
        // survive only in whatever was on screen at the time.
        let repaired = 0;
        const attempts = new Map<number, number>();
        for (let round = 1; round <= V2_RETRANSMIT_RETRY_ROUNDS; ++round) {
          const parsed = await parseLogAsync(bytes);
          const wanted = parsed.missingSeqs;
          // No page at all means there is no sequence number to count from, so there is nothing to
          // name and the whole transfer has to be repeated rather than patched.
          const nothingArrived = !!parsed.report.totalPages && parsed.report.pagesRead === 0;
          if (!wanted.length && !nothingArrived) break;

          // Only pages with attempts left are worth asking for, and only as many as the device can
          // hold. Everything else keeps its own remaining attempts for a later round.
          const askable = wanted.filter((seq) => (attempts.get(seq) ?? 0) < V2_RETRANSMIT_PAGE_ATTEMPTS);
          const batch = askable.slice(0, NANDLOG_MAX_RETRANSMIT_PAGES);
          if (!batch.length && !nothingArrived) break;   // everything still missing has spent its attempts

          setStatus(nothingArrived
            ? `No pages arrived; repeating the download, attempt ${round}…`
            : `Requesting ${batch.length} of ${wanted.length} missing page${wanted.length === 1 ? '' : 's'}, round ${round}…`);

          if (nothingArrived) {
            // A repeated transfer REPLACES what came before rather than patching it.
            bytes = await connection.downloadLog(null, setProgress);
            repaired = 0;
            attempts.clear();
          } else {
            for (const seq of batch) attempts.set(seq, (attempts.get(seq) ?? 0) + 1);
            const frames = extractPageFrames(await connection.retransmitPages(batch, setProgress));
            const merged = mergeRepairs(bytes, frames);
            // A round that recovered nothing is NOT a reason to stop: those pages have each spent one
            // of their own attempts, and the loop ends when they run out rather than on one bad round.
            if (merged !== bytes) {
              repaired += frames.size;
              bytes = merged;
            }
          }
          setProgress(null);
        }
        const eui = formatEui(connection.identity.eui);
        const shown = tagName(connection.identity.eui);
        onDownloaded({
          // A name only for the case where the log carries no details block to derive a better one
          // from; `buildReport` replaces it with what tottag.py would have written. The EUI's low
          // byte is what identifies this device inside that block.
          name: `${label || eui.replace(/:/g, '')}_${Math.floor(Date.now() / 1000)}.ttg`,
          bytes,
          origin: connection.identity.transport,
          deviceUid: connection.identity.eui[0],
        });
        setStatus(
          `Downloaded ${(bytes.length / 1e6).toFixed(2)} MB from ${shown}` +
            (repaired ? `, ${repaired} page${repaired === 1 ? '' : 's'} recovered by retransmission` : '') +
            '. It is not on your computer yet: save it from its card below.',
        );
      } catch (caught) {
        setError(caught instanceof Error ? caught.message : String(caught));
      } finally {
        await connection?.disconnect();
        setProgress(null);
      }
    },
    [onDownloaded],
  );

  const fromNew = useCallback(async (transport: TagTransport) => {
    // Use the connection the chooser just returned rather than dropping it and reconnecting. The
    // round trip through getDevices() was what produced "could not reach that tag" for a tag the
    // user had only just picked, on any Chrome where that API is still behind a flag.
    await run(transport, async () => {
      const connection = await transport.requestDevice();
      if (connection) remember(connection.identity);
      return connection;
    }, '');
  }, [remember, run]);

  const fromKnown = useCallback(
    async (tag: KnownTag) => {
      const transport = knownTagTransport(tag) === 'serial' ? webSerialTransport : webBluetoothTransport;
      return run(transport, () => transport.connect(tag.handle), '');
    },
    [run],
  );

  if (reason && serialReason) {
    return (
      <section className="panel">
        <h2 className="panel__title">Download from a tag</h2>
        <p className="banner banner--warn">{reason}</p>
      </section>
    );
  }

  const active = progress !== null;
  return (
    <section className="panel">
      <h2 className="panel__title">Download from a tag</h2>
      <p className="panel__note">
        Put the tag on its charger first (it only hands over its data while charging), or plug it into this
        computer with its USB cable. A download takes a few minutes.
      </p>
      <div className="actions">
        <button type="button" className="button" onClick={() => void fromNew(webBluetoothTransport)}
          disabled={!!reason || busy || active} title={reason ?? undefined}>
          Download from a tag
        </button>
        <button type="button" className="button button--quiet" onClick={() => void fromNew(webSerialTransport)}
          disabled={!!serialReason || busy || active} title={serialReason ?? undefined}>
          Download from a tag plugged in by USB
        </button>
      </div>
      {knownTags.length > 0 && (
        <>
          <h3 className="panel__subtitle">Or download from a tag this browser knows</h3>
          <ul className="chips">
            {knownTags.map((tag) => (
              <li key={tag.handle} className="chip">
                <button type="button" className="chip__button" onClick={() => void fromKnown(tag)}
                  disabled={busy || active || !!(knownTagTransport(tag) === 'serial' ? serialReason : reason)}>
                  {describeKnownTag(tag)}
                </button>
              </li>
            ))}
          </ul>
        </>
      )}

      {progress && (
        <div className="transfer">
          {calculating ? (
            <>
              <div className="transfer__bar"><div className="transfer__fill transfer__fill--busy" /></div>
              <p className="transfer__text">
                Calculating data size…
                {calculatingSince !== null && ` (${describeElapsed(Math.floor((now - calculatingSince) / 1000))})`}
              </p>
            </>
          ) : (
            <>
              <div className="transfer__bar">
                <div className="transfer__fill"
                  style={{ width: `${progress.bytesExpected ? Math.min(100, (progress.bytesReceived / progress.bytesExpected) * 100) : 3}%` }} />
              </div>
              <p className="transfer__text">
                {(progress.bytesReceived / 1e6).toFixed(2)} MB
                {progress.bytesExpected ? ` of ${(progress.bytesExpected / 1e6).toFixed(2)} MB` : ''}
                {' · '}{describeRate(progress.bytesPerSecond)}
                {' · '}{describeRemaining(progress.secondsRemaining)}
              </p>
            </>
          )}
        </div>
      )}
      {status && <p className="banner banner--info" role="status">{status}</p>}
      {error && <p className="banner banner--crit" role="alert">{error}</p>}
    </section>
  );
}

/** Whole seconds as "12 s", or "2 min 05 s" past a minute. */
function describeElapsed(seconds: number): string {
  return seconds < 60 ? `${seconds} s` : `${Math.floor(seconds / 60)} min ${String(seconds % 60).padStart(2, '0')} s`;
}
