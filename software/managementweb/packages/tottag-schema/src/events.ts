// A researcher's own notes of what happened when, to lay over the charts. Pure.
//
// The format is the one the desktop tool's analysis scripts read: one event per line,
// `YYYY-MM-DD HH:MM:SS message`, written on the study's own clock. A line that does not start that way
// is passed over and counted, so a header or a stray note does not stop the rest of the file being read.

import { wallClockToUnix } from './timezone.ts';

export interface NotedEvent {
  /** Unix seconds, as read on the study's clock. */
  readonly t: number;
  readonly message: string;
  /** Line number in the file, from 1. */
  readonly line: number;
}

export interface EventLog {
  readonly events: readonly NotedEvent[];
  /** Lines with something on them that were not an event, by line number. */
  readonly skipped: readonly number[];
}

const EVENT_LINE = /^(\d{4}-\d{2}-\d{2}) (\d{2}):(\d{2}):(\d{2})(?: (.*))?$/;

/** Read an event file, placing each time on `timezone`'s clock. Events come back in time order. */
export function parseEventLog(text: string, timezone: string): EventLog {
  const events: NotedEvent[] = [];
  const skipped: number[] = [];
  text.replace(/^﻿/, '').split(/\r\n|\r|\n/).forEach((raw, index) => {
    const line = raw.trimEnd();
    if (!line.trim()) return;
    const match = EVENT_LINE.exec(line);
    const seconds = match ? Number(match[4]) : Number.NaN;
    const minute = match ? wallClockToUnix(`${match[1]}T${match[2]}:${match[3]}`, timezone) : Number.NaN;
    if (!match || !Number.isFinite(minute) || seconds > 59) {
      skipped.push(index + 1);
      return;
    }
    events.push({ t: minute + seconds, message: (match[5] ?? '').trim(), line: index + 1 });
  });
  events.sort((a, b) => a.t - b.t || a.line - b.line);
  return { events, skipped };
}
