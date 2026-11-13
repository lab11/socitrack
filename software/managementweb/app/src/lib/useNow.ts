import { useEffect, useState } from 'react';

/**
 * The current time in Unix seconds, refreshed on an interval.
 *
 * For judgements that change as time passes — a deployment's end slipping into the past while the
 * page sits open, a tag's study moving from upcoming to running — without re-rendering every second.
 */
export function useNow(intervalMs = 30_000): number {
  const [now, setNow] = useState(() => Date.now() / 1000);
  useEffect(() => {
    const timer = setInterval(() => setNow(Date.now() / 1000), intervalMs);
    return () => clearInterval(timer);
  }, [intervalMs]);
  return now;
}
