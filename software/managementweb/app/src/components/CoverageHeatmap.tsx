import { ROUNDS_PER_MINUTE, type CoverageCell, type CoverageDay } from '@tottag/schema';
import { formatShortDate, formatClock } from '../features/timeAxis.ts';

const DESCRIBE: Record<CoverageCell['state'], string> = {
  ranging: 'recording, near other tags',
  alone: 'recording, no other tag in range',
  charging: 'on the charger',
  missed: 'scheduled, but not recording',
  idle: 'outside its daily hours',
  after: 'after the logs end',
  outside: 'outside the deployment',
};

function describe(cell: CoverageCell, timezone: string): string {
  const when = `${formatShortDate(cell.start, timezone)}, ${formatClock(cell.start, timezone)}`;
  if (cell.end <= cell.start) return `${when}: the clocks skipped this hour`;
  const share = cell.state === 'ranging' && cell.share !== null ? ` (measured another tag in ${Math.round(cell.share * 100)}% of its rounds)` : '';
  return `${when}: ${DESCRIBE[cell.state]}${share}`;
}

/**
 * Every hour of one tag's deployment, day by day on the study's calendar.
 *
 * Adapted from the A3EM dashboard's coverage grid. The distinction it draws is the one that matters
 * here: an hour the schedule never asked for, or spent on the charger, is not a fault; an hour that was
 * scheduled and has nothing in it is the thing to look for; and an hour recorded with nobody nearby is
 * simply a child on their own, which the shading shows apart from time spent near others.
 */
export function CoverageHeatmap({ days, timezone }: { days: readonly CoverageDay[]; timezone: string }) {
  if (!days.length) return <p className="empty">Nothing in this log has a time to lay out.</p>;
  const hourLabel = (hour: number) => (hour % 3 === 0 ? formatClock(Date.UTC(2026, 0, 1, hour) / 1000, 'UTC').replace(':00', '') : '');
  return (
    <>
      <div className="table-scroll">
        <table className="heatmap">
          <thead>
            <tr>
              <th className="heatmap__date" />
              {Array.from({ length: 24 }, (_, hour) => <th key={hour}>{hourLabel(hour)}</th>)}
              <th className="heatmap__total">Hours near others</th>
            </tr>
          </thead>
          <tbody>
            {days.map((day) => {
              // Every round that measured someone is half a second near them.
              const near = day.cells.reduce((sum, cell) => sum + cell.rounds, 0) / (ROUNDS_PER_MINUTE * 60);
              return (
                <tr key={day.date}>
                  <th className="heatmap__date">{formatShortDate(day.cells[12]!.start || day.cells[0]!.start, timezone)}</th>
                  {day.cells.map((cell) => (
                    <td key={cell.hour}>
                      <span className={`heat heat--${cell.state}`} title={describe(cell, timezone)}
                        style={cell.state === 'ranging' && cell.share !== null ? { opacity: 0.3 + 0.7 * cell.share } : undefined} />
                    </td>
                  ))}
                  <td className="heatmap__total">{near.toFixed(1)}</td>
                </tr>
              );
            })}
          </tbody>
        </table>
      </div>
      <div className="heat-legend">
        <span><i className="heat heat--ranging" /> near other tags (darker is more of the hour)</span>
        <span><i className="heat heat--alone" /> recording, nobody in range</span>
        <span><i className="heat heat--charging" /> on the charger</span>
        <span><i className="heat heat--missed" /> scheduled, not recording</span>
        <span><i className="heat heat--idle" /> outside its daily hours</span>
        <span><i className="heat heat--after" /> after the logs end</span>
      </div>
    </>
  );
}
