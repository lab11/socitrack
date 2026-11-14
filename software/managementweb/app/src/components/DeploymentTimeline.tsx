import { useLayoutEffect, useMemo, useRef, useState } from 'react';
import { localClock, scheduledIntervals } from '@tottag/schema';
import type { DeploymentStory, TagStory } from '../features/deploymentStory.ts';
import { formatInstant, timeTicks } from '../features/timeAxis.ts';

const GUTTER_LEFT = 210;
const GUTTER_RIGHT = 14;
const ROW = 30;
const BAR = 14;
const AXIS = 38;

const inside = (pairs: ArrayLike<number>, t: number) => {
  for (let i = 0; i + 1 < pairs.length; i += 2) if (t >= pairs[i]! && t <= pairs[i + 1]!) return true;
  return false;
};

/** What a tag was doing at an instant, in words. */
function stateAt(story: TagStory, scheduled: number[], t: number): string {
  if (inside(story.recording, t)) return 'Recording';
  if (inside(story.charging, t)) return 'On the charger';
  if (inside(story.missed, t)) return 'Not recording, though scheduled to';
  if (t < story.first || t > story.last) return 'Outside its log';
  return inside(scheduled, t) ? 'Not recording' : 'Outside its daily hours';
}

/**
 * Every tag's deployment on one time axis: when it was scheduled, recording, charging, or missing, and
 * where it restarted or corrected its clock. Drag across it to look closer.
 */
export function DeploymentTimeline({ story, timezone }: { story: DeploymentStory; timezone: string }) {
  const boxRef = useRef<HTMLDivElement>(null);
  const svgRef = useRef<SVGSVGElement>(null);
  const [measured, setMeasured] = useState(0);
  useLayoutEffect(() => {
    const box = boxRef.current;
    if (!box) return undefined;
    const update = () => setMeasured(box.clientWidth);
    update();
    const observer = new ResizeObserver(update);
    observer.observe(box);
    return () => observer.disconnect();
  }, []);
  const [view, setView] = useState<[number, number] | null>(null);
  const [drag, setDrag] = useState<{ from: number; to: number } | null>(null);
  const [hover, setHover] = useState<{ t: number; row: number } | null>(null);

  const x0 = view?.[0] ?? story.from;
  const x1 = view?.[1] ?? Math.max(story.to, story.from + 60);
  const width = Math.max(480, measured || 900);
  const plotWidth = width - GUTTER_LEFT - GUTTER_RIGHT;
  const height = story.tags.length * ROW + AXIS + 6;
  const plotBottom = story.tags.length * ROW + 4;
  const x = (t: number) => GUTTER_LEFT + ((Math.max(x0, Math.min(x1, t)) - x0) / (x1 - x0)) * plotWidth;
  const scheduled = useMemo(
    () => (story.scheduled ? scheduledIntervals(story.scheduled, story.from, story.to) : [story.from, story.to]),
    [story],
  );
  const clock = useMemo(() => localClock(timezone, story.from, story.to), [timezone, story.from, story.to]);
  const ticks = timeTicks(x0, x1, timezone, Math.max(2, Math.floor(plotWidth / 120)), clock);

  const at = (clientX: number, clientY: number) => {
    const rect = svgRef.current?.getBoundingClientRect();
    if (!rect) return { t: x0, row: -1 };
    const px = ((clientX - rect.left) / rect.width) * width - GUTTER_LEFT;
    const t = x0 + Math.max(0, Math.min(1, px / plotWidth)) * (x1 - x0);
    return { t, row: Math.floor((clientY - rect.top) / ROW) };
  };
  const bars = (pairs: ArrayLike<number>, className: string, row: number) => {
    const out = [];
    for (let i = 0; i + 1 < pairs.length; i += 2) {
      if (pairs[i + 1]! < x0 || pairs[i]! > x1) continue;
      out.push(<rect key={`${className}${i}`} className={className} x={x(pairs[i]!)} width={Math.max(1.5, x(pairs[i + 1]!) - x(pairs[i]!))}
        y={row * ROW + (ROW - BAR) / 2} height={BAR} rx={3} />);
    }
    return out;
  };

  const hovered = hover && hover.row >= 0 && hover.row < story.tags.length ? story.tags[hover.row]! : null;
  const pxPerSecond = plotWidth / (x1 - x0);
  const nearby = hovered && hover ? hovered.events.filter((event) => Math.abs(event.t - hover.t) * pxPerSecond <= 6) : [];

  return (
    <div className="timeline">
      <div ref={boxRef} className="chart__box">
        <svg ref={svgRef} viewBox={`0 0 ${width} ${height}`} width="100%" height={height} className="chart__svg"
          role="img" aria-label={`What each tag did, ${formatInstant(x0, timezone, false)} to ${formatInstant(x1, timezone, false)}`}
          style={{ cursor: 'col-resize' }}
          onPointerDown={(event) => {
            if (event.button !== 0) return;
            event.preventDefault();
            event.currentTarget.setPointerCapture(event.pointerId);
            const { t } = at(event.clientX, event.clientY);
            setDrag({ from: t, to: t });
          }}
          onPointerMove={(event) => {
            const point = at(event.clientX, event.clientY);
            setHover(point);
            if (drag) setDrag({ ...drag, to: point.t });
          }}
          onPointerLeave={() => setHover(null)}
          onPointerUp={() => {
            if (!drag) return;
            const from = Math.min(drag.from, drag.to);
            const to = Math.max(drag.from, drag.to);
            setDrag(null);
            if ((to - from) * pxPerSecond >= 6) setView([from, Math.max(to, from + 60)]);
          }}>
          {story.tags.map((tag, row) => (
            <g key={tag.report.id}>
              <text x={GUTTER_LEFT - 10} y={row * ROW + ROW / 2} textAnchor="end" dominantBaseline="middle" className="timeline__name">
                {tag.name.length > 30 ? `${tag.name.slice(0, 29)}…` : tag.name}
              </text>
              <line x1={GUTTER_LEFT} x2={GUTTER_LEFT + plotWidth} y1={row * ROW + ROW / 2} y2={row * ROW + ROW / 2} className="timeline__track" />
              {bars(scheduled, 'timeline__scheduled', row)}
              {bars(tag.recording, 'timeline__recording', row)}
              {bars(tag.charging, 'timeline__charging', row)}
              {bars(tag.missed, 'timeline__missed', row)}
              {tag.restarts.filter((r) => r.t >= x0 && r.t <= x1).map((restart, i) => (
                <line key={`r${i}`} x1={x(restart.t)} x2={x(restart.t)} y1={row * ROW + 3} y2={row * ROW + ROW - 3}
                  className={restart.tone === 'warn' ? 'timeline__restart timeline__restart--warn' : 'timeline__restart'} />
              ))}
              {tag.clockSteps.filter((s) => s.t >= x0 && s.t <= x1).map((step, i) => (
                <circle key={`c${i}`} cx={x(step.t)} cy={row * ROW + 4} r={3} className="timeline__clock" />
              ))}
            </g>
          ))}
          {drag && Math.abs(drag.to - drag.from) * pxPerSecond >= 2 && (
            <rect x={x(Math.min(drag.from, drag.to))} width={Math.abs(x(drag.to) - x(drag.from))} y={0} height={plotBottom} className="chart__selection" />
          )}
          {hover && !drag && <line x1={x(hover.t)} x2={x(hover.t)} y1={0} y2={plotBottom} className="chart__crosshair" />}
          <line x1={GUTTER_LEFT} x2={GUTTER_LEFT + plotWidth} y1={plotBottom} y2={plotBottom} className="chart__axis" />
          {ticks.map((tick) => (
            <g key={tick.t}>
              <text x={x(tick.t)} y={plotBottom + 15} textAnchor="middle" className="chart__tick">{tick.label}</text>
              {tick.date && <text x={x(tick.t)} y={plotBottom + 29} textAnchor="middle" className="chart__tick chart__tick--date">{tick.date}</text>}
            </g>
          ))}
        </svg>
        {hover && hovered && !drag && (
          <div className="chart__tip" style={{ left: Math.min(x(hover.t) + 14, width - 260), top: hover.row * ROW + ROW }} role="status">
            <strong>{hovered.name}</strong>
            <span>{formatInstant(hover.t, timezone, (x1 - x0) < 7200)}</span>
            <span>{stateAt(hovered, scheduled, hover.t)}</span>
            {nearby.map((event, i) => <span key={i} className="chart__tip-note">{event.title}</span>)}
          </div>
        )}
      </div>
      <div className="chart__foot">
        <span className="chart__key">
          <span className="chart__key-item"><i className="timeline__swatch timeline__swatch--recording" />Recording</span>
          <span className="chart__key-item"><i className="timeline__swatch timeline__swatch--charging" />On the charger</span>
          <span className="chart__key-item"><i className="timeline__swatch timeline__swatch--missed" />Scheduled, not recording</span>
          <span className="chart__key-item"><i className="timeline__swatch timeline__swatch--scheduled" />Scheduled hours</span>
          <span className="chart__key-item"><i className="timeline__swatch timeline__swatch--restart" />Restart</span>
          <span className="chart__key-item"><i className="timeline__swatch timeline__swatch--clock" />Clock corrected</span>
        </span>
        {view ? (
          <button type="button" className="button button--quiet button--small" onClick={() => setView(null)}>Show the whole deployment</button>
        ) : <span>Drag across it to look closer.</span>}
      </div>
    </div>
  );
}
