// What happened during a deployment, tag by tag and in order. Pure.
//
// Everything here is assembled from what the analysis already decided — health.ts's restarts and clock
// corrections, the worker's recording and charging stretches, and the schedule in each log's details —
// so the timeline, the list of events, the verdict, and the cards cannot disagree with each other.

import { formatSpan, missedStretches, ROUNDS_PER_MINUTE, type ExperimentDetails } from '@tottag/schema';
import type { DeviceReport } from './deployment.ts';
import { explainRestart } from './plainLanguage.ts';
import type { Deployment } from './studyClock.ts';
import { logTag, type TagRef } from './tagNames.ts';

/** A stretch this long, scheduled and neither recorded nor charging, is worth reporting. */
export const MISSED_REPORT_SECONDS = 15 * 60;
/** A tag whose log ends this long before the others' is said to have stopped early. */
export const STOPPED_EARLY_SECONDS = 60 * 60;

export type StoryKind = 'restart' | 'clock' | 'charging' | 'missed' | 'low-battery' | 'stopped';

export interface StoryEvent {
  readonly t: number;
  /** The end, for something that lasted. */
  readonly until: number | null;
  /** Which tag: its display name. */
  readonly tag: string;
  readonly kind: StoryKind;
  readonly title: string;
  readonly detail: string;
  readonly tone: 'ok' | 'warn';
}

export interface TagStory {
  readonly report: DeviceReport;
  readonly tag: TagRef | null;
  /** What to call it. */
  readonly name: string;
  readonly first: number;
  readonly last: number;
  readonly recording: Float64Array;
  readonly charging: Float64Array;
  /** Scheduled, but neither recording nor charging, for at least MISSED_REPORT_SECONDS: [from, to, …]. */
  readonly missed: readonly number[];
  readonly restarts: ReadonlyArray<{ readonly t: number; readonly tone: 'ok' | 'warn'; readonly title: string }>;
  readonly clockSteps: ReadonlyArray<{ readonly t: number; readonly stepMs: number }>;
  readonly secondsRecorded: number;
  readonly secondsMissed: number;
  /** Rounds that measured another tag, as minutes. */
  readonly minutesInRange: number;
  /** Lowest and highest chip temperature, °C. */
  readonly temperature: readonly [number, number] | null;
  readonly events: readonly StoryEvent[];
}

export interface DeploymentStory {
  /** The first and last record across every log. */
  readonly from: number;
  readonly to: number;
  readonly scheduled: ExperimentDetails | null;
  readonly tags: readonly TagStory[];
  /** Every tag's events, in time order. */
  readonly events: readonly StoryEvent[];
  readonly verdict: { readonly tone: 'ok' | 'warn'; readonly headline: string; readonly findings: readonly string[] };
}

const sum = (pairs: ArrayLike<number>) => {
  let total = 0;
  for (let i = 0; i + 1 < pairs.length; i += 2) total += pairs[i + 1]! - pairs[i]!;
  return total;
};
const plural = (count: number, word: string) => `${count} ${word}${count === 1 ? '' : 's'}`;

function tagStory(report: DeviceReport, scheduled: ExperimentDetails | null, deploymentLast: number): TagStory | null {
  const { charts, health } = report;
  if (!charts || !health || charts.first === null || charts.last === null) return null;
  const tag = logTag(report, scheduled);
  const name = tag?.display ?? report.name;
  const details = scheduled;
  const missed = details
    ? missedStretches(details, Math.max(charts.first, details.experimentStartTime), Math.min(charts.last, details.experimentEndTime),
      charts.recording, charts.charging, MISSED_REPORT_SECONDS)
    : [];
  const restarts = health.reboots.map((reboot) => {
    const story = explainRestart(reboot);
    return { t: charts.start + reboot.ms / 1000, tone: story.tone, title: story.title, detail: story.detail };
  });
  const clockSteps = health.clock.offsetSteps.map((step) => ({ t: charts.start + step.ms / 1000, stepMs: step.stepMs }));

  const events: StoryEvent[] = [];
  for (const restart of restarts) {
    events.push({ t: restart.t, until: null, tag: name, kind: 'restart', title: restart.title, detail: restart.detail, tone: restart.tone });
  }
  for (const step of clockSteps) {
    events.push({
      t: step.t, until: null, tag: name, kind: 'clock', tone: 'ok',
      title: `Clock corrected by ${(Math.abs(step.stepMs) / 1000).toFixed(1)} seconds`,
      detail: 'The tag set its clock to match the other tags. Distances between tags are not affected.',
    });
  }
  for (let i = 0; i + 1 < charts.charging.length; i += 2) {
    const from = charts.charging[i]!;
    const to = charts.charging[i + 1]!;
    // A stretch still open when the log ends has no known length: the log stops where it began.
    const open = to - from < 60 && to >= charts.last - 1;
    events.push(open
      ? { t: from, until: null, tag: name, kind: 'charging', tone: 'ok', title: 'Put on the charger', detail: 'Its log ends here, as a tag does not record while it charges.' }
      : { t: from, until: to, tag: name, kind: 'charging', tone: 'ok', title: `On the charger for ${formatSpan(to - from)}`, detail: 'A tag does not record while it charges.' });
  }
  for (let i = 0; i + 1 < missed.length; i += 2) {
    events.push({
      t: missed[i]!, until: missed[i + 1]!, tag: name, kind: 'missed', tone: 'warn',
      title: `Not recording for ${formatSpan(missed[i + 1]! - missed[i]!)} of its scheduled hours`,
      detail: 'It was not on its charger either. It may have been switched off, out of battery, or restarting.',
    });
  }
  for (const t of charts.lowBattery) {
    events.push({ t, until: null, tag: name, kind: 'low-battery', tone: 'warn', title: 'Battery critically low', detail: 'The tag stops recording to protect its battery until it is charged.' });
  }
  const endsBy = details ? Math.min(details.experimentEndTime, deploymentLast) : deploymentLast;
  if (endsBy - charts.last >= STOPPED_EARLY_SECONDS) {
    events.push({
      t: charts.last, until: endsBy, tag: name, kind: 'stopped', tone: 'warn',
      title: `Stopped recording ${formatSpan(endsBy - charts.last)} before the ${details && endsBy === details.experimentEndTime ? 'deployment ended' : 'other tags'}`,
      detail: 'Its last record is here. If its battery ran out, the battery chart shows it.',
    });
  }
  events.sort((a, b) => a.t - b.t);

  let low = Number.POSITIVE_INFINITY;
  let high = Number.NEGATIVE_INFINITY;
  for (const value of charts.temperature.v) {
    if (Number.isNaN(value)) continue;
    low = Math.min(low, value);
    high = Math.max(high, value);
  }
  let rounds = 0;
  for (const count of charts.rangedPerMinute.v) rounds += count;

  return {
    report, tag, name, first: charts.first, last: charts.last, recording: charts.recording, charging: charts.charging,
    missed, restarts, clockSteps, secondsRecorded: sum(charts.recording), secondsMissed: sum(missed),
    minutesInRange: rounds / ROUNDS_PER_MINUTE, temperature: low <= high ? [low, high] : null, events,
  };
}

/** The story of one deployment, from its open logs. */
export function deploymentStory(deployment: Deployment): DeploymentStory | null {
  const withCharts = deployment.reports.filter((report) => report.charts?.first != null && report.charts.last != null);
  if (!withCharts.length) return null;
  const from = Math.min(...withCharts.map((report) => report.charts!.first!));
  const to = Math.max(...withCharts.map((report) => report.charts!.last!));
  const tags = withCharts
    .map((report) => tagStory(report, deployment.scheduled, to))
    .filter((story): story is TagStory => story !== null)
    .sort((a, b) => a.name.localeCompare(b.name));
  const events = tags.flatMap((story) => story.events).sort((a, b) => a.t - b.t || a.tag.localeCompare(b.tag));

  const findings: string[] = [];
  for (const story of tags) {
    const stalls = story.restarts.filter((restart) => restart.tone === 'warn').length;
    if (stalls) findings.push(`${story.name} restarted unexpectedly ${stalls === 1 ? 'once' : `${stalls} times`}.`);
    if (story.secondsMissed > 0) {
      const count = story.missed.length / 2;
      findings.push(`${story.name} was not recording for ${formatSpan(story.secondsMissed)} of its scheduled hours, ${count === 1 ? 'all at once' : `in ${count} stretches`}.`);
    }
    const stopped = story.events.find((event) => event.kind === 'stopped');
    if (stopped) findings.push(`${story.name} ${stopped.title.charAt(0).toLowerCase()}${stopped.title.slice(1)}.`);
    if (story.events.some((event) => event.kind === 'low-battery')) findings.push(`${story.name}'s battery ran critically low.`);
    const health = story.report.health;
    if (health && (health.integrity.holes || health.integrity.crcFailures || health.integrity.truncated)) {
      findings.push(`${story.name}'s download is incomplete; its card on the Tags tab says what to do.`);
    }
    if (story.minutesInRange === 0 && deployment.reports.length > 1) findings.push(`${story.name} never measured a distance to another tag.`);
  }
  const headline = findings.length
    ? `${plural(findings.length, 'thing')} worth a look`
    : `Every tag recorded through its scheduled hours${tags.length > 1 ? ' and measured the others' : ''}.`;
  return { from, to, scheduled: deployment.scheduled, tags, events, verdict: { tone: findings.length ? 'warn' : 'ok', headline, findings } };
}
