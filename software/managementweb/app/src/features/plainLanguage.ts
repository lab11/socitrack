// What each finding about a log means for the study, and what to do about it. Pure.
//
// The analysis in @tottag/schema explains every finding precisely, in the terms of the firmware that
// produced it — watchdog shapes, flush timeouts, buffer pools. That precision is worth keeping, and the
// card still offers it as "Technical detail". But the person reading the card first needs two things
// the technical text does not lead with: is my data all right, and is there anything I should do
// before these tags go out again?
//
// Every finding the analysis can raise has an entry here; a test reads health.ts and fails when one
// is added without a plain version.

import { formatSpan, WATCHDOG_RESET_WINDOW_S, type Anomaly, type DeploymentHealth, type RebootEvent } from '@tottag/schema';

export interface PlainFinding {
  /** What happened and what it means for the data. */
  readonly summary: string;
  /** What to do about it, or that nothing needs doing. */
  readonly action: string;
}

const NOTHING = 'Nothing to do.';
const times = (count: number) => (count === 1 ? 'once' : count === 2 ? 'twice' : `${count} times`);
const minutes = Math.max(1, Math.round(WATCHDOG_RESET_WINDOW_S / 60));

type Explainer = (health: DeploymentHealth) => PlainFinding;

export const PLAIN_FINDINGS: Readonly<Record<string, Explainer>> = {
  'pages-lost': (health) => {
    const lost = health.integrity.holes + health.integrity.crcFailures;
    return {
      summary: `${lost} piece${lost === 1 ? '' : 's'} of this tag's data did not come through in the download.`,
      action: 'Download it again from the tag before setting the tag up for another study. A second try usually recovers them.',
    };
  },
  truncated: () => ({
    summary: 'The download stopped before it had everything.',
    action: 'Download it again from the tag before setting the tag up for another study.',
  }),
  'time-rebased': () => ({
    summary: "The tag's timeline jumps backwards partway through. A complete download is unaffected, but one limited to certain dates may have missed some data.",
    action: 'If this file came from a download limited to certain dates, download the whole log instead.',
  }),
  'watchdog-reset': (health) => ({
    summary: `The tag got stuck and restarted itself ${times(health.watchdogResets)}. Each restart loses up to about ${minutes} minute${minutes === 1 ? '' : 's'} of recording; the rest of the data is fine.`,
    action: 'If this tag does it often, keep it out of studies until it has been looked at.',
  }),
  'rtc-restarted': () => ({
    summary: "The tag's clock restarted partway through, most likely because its battery ran completely flat. Times after that point cannot be lined up with times before it.",
    action: 'Charge it fully before its next study.',
  }),
  'time-base-step': () => ({
    summary: "The tag's clock was a few seconds off when it started, and it corrected itself as soon as it met the other tags. Distances between tags are not affected.",
    action: 'Nothing to do: the clock is set again whenever a deployment is written to the tag.',
  }),
  'network-clock-drift': () => ({
    summary: "The tag's clock drifted a little compared with the other tags. That affects clock times only, not distances between tags.",
    action: NOTHING,
  }),
  'clock-after-restart': () => ({
    summary: "After the tag restarted, the last few entries use its own clock, which had drifted a few seconds from the other tags' over the study. Nothing was lost, and distances between tags are not affected.",
    action: NOTHING,
  }),
  'cadence-shortfall': (health) => {
    const missing = Math.round((1 - health.cadence.captured) * 100);
    return {
      summary: `The tag is missing about ${missing}% of its regular check-ins, so there are stretches when it was probably not recording.`,
      action: 'Check whether it went back on its charger, or ran low on battery, during the study.',
    };
  },
  'charging-event-storm': () => ({
    summary: 'The tag recorded going on and off its charger far more often than normal.',
    action: 'Check its charging contacts, and that it sits firmly on the charger.',
  }),
  'charger-reboots': () => ({
    summary: 'The tag restarted each time it went on or off its charger, which is normal.',
    action: NOTHING,
  }),
  'watchdog-near-miss': () => ({
    summary: 'The tag nearly got stuck, but recovered each time. No data was lost.',
    action: 'If it keeps happening with this tag, have it looked at.',
  }),
  'charger-pin-chatter': () => ({
    summary: "The tag's charger connection flickered. Its data is fine.",
    action: 'Check its charging contacts.',
  }),
  'pool-figures-implausible': () => ({
    summary: "Some of the tag's internal diagnostic numbers do not add up. The recorded data is not affected.",
    action: NOTHING,
  }),
  'wsf-pool-exhausted': () => ({
    summary: "The tag's Bluetooth ran short of memory, which can cut a download short.",
    action: 'If anything else on this card says data is missing, download it again.',
  }),
  'wsf-pool-tight': () => ({
    summary: "The tag's Bluetooth came close to running short of memory. Nothing was lost.",
    action: NOTHING,
  }),
  'records-dropped': (health) => ({
    summary: `${health.nearMisses.recordsDropped} measurement${health.nearMisses.recordsDropped === 1 ? ' was' : 's were'} lost because the tag could not save them fast enough.`,
    action: 'If it happens with this tag again, have it looked at.',
  }),
  'uwb-radio-trouble': () => ({
    summary: "The tag's distance radio had to be restarted. Each time costs a moment of distance measurements.",
    action: 'If it happens a lot, run a radio check on this tag.',
  }),
  'ble-controller-restarts': () => ({
    summary: "The tag's Bluetooth restarted itself, which may have slowed it down in finding the other tags.",
    action: 'Nothing to do unless it happens often.',
  }),
  'stack-nearly-exhausted': () => ({
    summary: "Part of the tag's software came close to running out of memory. The data is fine.",
    action: "Mention it to whoever maintains your tags' firmware.",
  }),
  'flash-blocks-retired': () => ({
    summary: 'The tag set aside some worn-out storage, which is normal wear. Nothing was lost.',
    action: NOTHING,
  }),
};

/** The plain version of a finding, or null for one this table does not know (shown technical-only). */
export function plainFinding(anomaly: Anomaly, health: DeploymentHealth): PlainFinding | null {
  return PLAIN_FINDINGS[anomaly.code]?.(health) ?? null;
}

// --- One restart at a time ---------------------------------------------------------------------------

export interface RestartStory {
  /** What happened, in a few words, for a timeline or a list. */
  readonly title: string;
  /** What it means for the data. */
  readonly detail: string;
  /** `ok` for a restart that is part of normal use, `warn` for one worth knowing about. */
  readonly tone: 'ok' | 'warn';
}

/** "a few seconds", "40 seconds", "4 minutes", "2 days, 3 hours". */
const lasting = (seconds: number) =>
  seconds < 30 ? 'a few seconds' : seconds < 90 ? `${Math.round(seconds)} seconds` : formatSpan(seconds);

/**
 * Why one restart happened, in plain words.
 *
 * Read entirely from the analysis's own account of the restart (health.ts's RebootEvent): its kind, the
 * shape of a stall, whether the charger caused it, and the silences either side. Nothing here looks at
 * records, so the card's restart count, this explanation, and the technical finding cannot disagree.
 */
export function explainRestart(reboot: RebootEvent): RestartStory {
  const off = reboot.outageSeconds !== null && reboot.outageSeconds >= 30 ? ` It was off for about ${lasting(reboot.outageSeconds)}.` : '';
  const silence = lasting(reboot.silenceSeconds);
  if (reboot.chargerTransition) {
    return { tone: 'ok', title: 'Went on or off its charger', detail: `Plugging a tag in or unplugging it restarts it. This is normal.${off}` };
  }
  if (reboot.kind === 'watchdog') {
    const part = reboot.diagnosticLabel?.replace(/^stalled: /, '') ?? null;
    switch (reboot.shape) {
      case 'single-task':
        return {
          tone: 'warn', title: 'Got stuck and restarted itself',
          detail: `One part of its software${part ? ` (${part})` : ''} stopped responding, so the tag restarted itself. ` +
            `Everything else kept recording; at most ${silence} of data was lost.${off}`,
        };
      case 'whole-system':
        return {
          tone: 'warn', title: 'Stopped and restarted itself',
          detail: `The tag stopped completely and recorded nothing for ${silence} before it restarted itself.${off}`,
        };
      case 'fault':
        return {
          tone: 'warn', title: 'Hit a software fault and restarted',
          detail: `Its software ran into a fault${reboot.diagnosticLabel ? ` (${reboot.diagnosticLabel})` : ''} and the tag restarted itself. ` +
            `About ${silence} of recording was lost.${off}`,
        };
      default:
        return {
          tone: 'warn', title: 'Got stuck and restarted itself',
          detail: `The tag restarted itself after ${silence} without recording. What it reported and how long it was quiet do not quite agree, so the exact cause is unclear.${off}`,
        };
    }
  }
  if (reboot.kind === 'graceful') {
    return { tone: 'ok', title: 'Restarted itself in an orderly way', detail: `The tag chose to restart, which costs a few seconds of recording.${off}` };
  }
  const causes = reboot.causes.join(', ');
  if (/Brown/i.test(causes)) {
    return { tone: 'warn', title: 'Battery dipped and it restarted', detail: `The battery's voltage dropped too low for a moment, so the tag restarted.${off}` };
  }
  if (/^Power-On$/.test(causes)) {
    return { tone: 'warn', title: 'Powered on again', detail: `The tag lost power and came back, as it does after its battery runs flat.${off}` };
  }
  return { tone: 'warn', title: 'Restarted', detail: `The tag restarted and gave this reason: ${causes}.${off}` };
}
