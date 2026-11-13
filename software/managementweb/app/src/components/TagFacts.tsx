// What a tag reported when it was last checked, in words.

import { formatDate, formatDateTime, formatTimeOfDay, localDailyWindow, MIN_RECORDING_BATTERY_MV, tagName } from '@tottag/schema';
import { batteryLow, CLOCK_TOLERANCE_S, storedState, type StoredDeployment, type TagCheck } from '../features/tagStatus.ts';

const volts = (mv: number) => `${(mv / 1000).toFixed(2)} V`;

export function describeStored(stored: StoredDeployment | null, now: number, timezone: string): string {
  if (!stored) return 'No deployment';
  const span = `${formatDate(stored.startTime, timezone)} – ${formatDate(stored.endTime, timezone)}`;
  switch (storedState(stored, now)) {
    case 'cancelled': return 'Its last deployment was cancelled';
    case 'upcoming': return `Scheduled for ${span}`;
    case 'running': return `Recording a deployment, ${span}`;
    default: return `Holds a finished deployment, ${span}`;
  }
}

export function describeClock(check: TagCheck): { text: string; ok: boolean } {
  if (check.clockSet === null) return { text: 'Clock not read', ok: true };
  if (!check.clockSet) return { text: 'Clock not set yet — writing a deployment sets it', ok: true };
  const offset = Math.abs(check.clockOffsetSeconds ?? 0);
  if (offset <= CLOCK_TOLERANCE_S) return { text: 'Clock is right', ok: true };
  const amount = offset < 120 ? `${Math.round(offset)} seconds` : `${Math.round(offset / 60)} minutes`;
  return { text: `Clock is ${amount} ${check.clockOffsetSeconds! > 0 ? 'fast' : 'slow'} — writing a deployment sets it`, ok: false };
}

const ago = (seconds: number) => {
  if (seconds < 90) return 'just now';
  if (seconds < 5400) return `${Math.round(seconds / 60)} minutes ago`;
  if (seconds < 129600) return `${Math.round(seconds / 3600)} hours ago`;
  return `${Math.round(seconds / 86400)} days ago`;
};

/** The short form shown on a tag's row while setting up a deployment. */
export function TagPills({ check, now, timezone }: { check: TagCheck | null; now: number; timezone: string }) {
  if (!check) return <span className="pill pill--quiet">Not checked yet</span>;
  const low = batteryLow(check.batteryMv);
  return (
    <>
      {check.batteryMv !== null && (
        <span className={`pill ${low ? 'pill--warn' : 'pill--ok'}`}
          title={low ? `A tag will not start recording below ${volts(MIN_RECORDING_BATTERY_MV)}` : 'Battery when last checked'}>
          Battery {volts(check.batteryMv)}{low ? ' — low' : ''}
        </span>
      )}
      <span className="pill pill--quiet">{describeStored(check.stored, now, timezone)}</span>
      <span className="pill pill--quiet" title={formatDateTime(check.checkedAt, timezone)}>Checked {ago(now - check.checkedAt)}</span>
    </>
  );
}

/** Everything a check found, for the page that checks one tag. */
export function TagDetails({ check, now, timezone, downloadedAt }: {
  check: TagCheck;
  now: number;
  timezone: string;
  downloadedAt: string | null;
}) {
  const clock = describeClock(check);
  const stored = check.stored;
  const low = batteryLow(check.batteryMv);
  const daily = stored?.useDailyTimes
    ? localDailyWindow({ start: stored.dailyStartTime, end: stored.dailyEndTime }, timezone, stored.startTime)
    : null;
  return (
    <dl className="facts">
      <div className="facts__row">
        <dt>Battery</dt>
        <dd>
          {check.batteryMv === null ? 'Could not be read' : volts(check.batteryMv)}
          {low && <span className="facts__warn"> Low: it will not start recording below {volts(MIN_RECORDING_BATTERY_MV)}. Leave it on its charger longer.</span>}
        </dd>
      </div>
      <div className="facts__row">
        <dt>Clock</dt>
        <dd className={clock.ok ? undefined : 'facts__warn'}>{clock.text}</dd>
      </div>
      {check.firmware && (
        <div className="facts__row"><dt>Firmware</dt><dd>{check.firmware}</dd></div>
      )}
      <div className="facts__row">
        <dt>Deployment</dt>
        <dd>
          {describeStored(stored, now, timezone)}
          {stored && !stored.cancelled && (
            <ul className="facts__list">
              <li>From {formatDateTime(stored.startTime, timezone)} to {formatDateTime(stored.endTime, timezone)}</li>
              <li>{daily ? `Records ${formatTimeOfDay(daily.start)} – ${formatTimeOfDay(daily.end)} each day` : 'Records around the clock'}</li>
              <li>
                With {stored.devices.length} tag{stored.devices.length === 1 ? '' : 's'}:{' '}
                {stored.devices.map((device) => `${tagName(device.eui)}${device.label ? ` (${device.label})` : ''}`).join(', ')}
              </li>
              {storedState(stored, now) !== 'upcoming' && (
                <li>{downloadedAt
                  ? `Its data was saved from this browser on ${new Date(downloadedAt).toLocaleDateString()}.`
                  : 'Its data has not been saved from this browser.'}</li>
              )}
            </ul>
          )}
        </dd>
      </div>
      <div className="facts__row"><dt>Checked</dt><dd>{formatDateTime(check.checkedAt, timezone)}</dd></div>
    </dl>
  );
}
