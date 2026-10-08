import type { DeviceStatus } from '../features/deployment.ts';

const LABEL: Record<DeviceStatus, string> = {
  ok: 'Looks good',
  note: 'Worth a look',
  warning: 'Needs attention',
  error: 'Could not read',
};

/**
 * Status shown as a word, not only a colour.
 *
 * These reports get printed and put in study binders, where hue is the first thing lost. The word
 * is the signal; the colour is reinforcement.
 */
export function StatusBadge({ status }: { status: DeviceStatus }) {
  return <span className={`badge badge--${status}`}>{LABEL[status]}</span>;
}
