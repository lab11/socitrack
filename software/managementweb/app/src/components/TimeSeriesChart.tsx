import { useId, useLayoutEffect, useMemo, useRef, useState, type ReactNode } from 'react';
import { localClock, lowerBound, seriesRange, thinSeries, type Series } from '@tottag/schema';
import { formatInstant, timeTicks, valueTicks } from '../features/timeAxis.ts';

/** One line on the chart. */
export interface ChartLine {
  readonly id: string;
  readonly label: string;
  /** A CSS color, normally a token such as `var(--primary)`. */
  readonly color: string;
  readonly data: Series;
  /** Hold each value until the next, for states like moving or still. */
  readonly step?: boolean;
  /** Shade beneath the line. */
  readonly fill?: boolean;
}

/** A stretch of time shaded behind the lines, such as time on the charger. */
export interface ChartSpan {
  readonly from: number;
  readonly to: number;
  readonly kind: string;
}

/** A moment marked across the chart, such as a restart or a noted event. */
export interface ChartMarker {
  readonly t: number;
  readonly kind: string;
  readonly label: string;
}

/** How a kind of span or marker is drawn and named in the key. */
export interface ChartKind {
  readonly label: string;
  readonly color: string;
  readonly dashed?: boolean;
}

interface Props {
  readonly title: string;
  readonly lines: readonly ChartLine[];
  readonly spans?: readonly ChartSpan[];
  readonly markers?: readonly ChartMarker[];
  readonly kinds?: Readonly<Record<string, ChartKind>>;
  /** The whole span the chart can show, in Unix seconds: normally the deployment's recording. */
  readonly extent: readonly [number, number];
  /** The part being shown, when the zoom is shared with other charts. Null shows the whole extent. */
  readonly view?: readonly [number, number] | null;
  readonly onViewChange?: (view: [number, number] | null) => void;
  /** The study's time zone: every tick and readout is on its clock. */
  readonly timezone: string;
  readonly formatValue: (value: number) => string;
  readonly yDomain?: readonly [number, number];
  /** Named levels instead of numbers, such as Still and Moving. */
  readonly yLabels?: ReadonlyArray<{ readonly value: number; readonly label: string }>;
  readonly height?: number;
  /** A stretch drawn highlighted, such as the window a closer chart is showing. */
  readonly highlight?: readonly [number, number] | null;
  /** A click (not a drag) on the chart, with the time under the pointer. */
  readonly onPick?: (t: number) => void;
  /** Said under the chart, after the zoom hint. */
  readonly footnote?: ReactNode;
  /** No zoom: for a small overview whose clicks do something else. */
  readonly fixed?: boolean;
}

const GUTTER_LEFT = 62;
const GUTTER_RIGHT = 14;
const PAD_TOP = 10;
const GUTTER_BOTTOM = 36;
/** Shortest stretch a drag can zoom to, so a twitch of the mouse does not zoom to nothing. */
const MIN_ZOOM_SECONDS = 5;

/**
 * A time series on a time axis that is true to time.
 *
 * Ported from the A3EM dashboard's chart, with the one change that matters for TotTag: a point's place
 * across the chart is its TIME, not its position in the list. A tag sleeps outside its daily hours and
 * sits on a charger for a night; drawn by index those stretches vanish and the evening runs straight
 * into the next morning. Here they are as wide as they were long, and the line stops at them rather
 * than drawing across.
 *
 * Every series arrives already reduced to a few thousand points and is thinned again here to about one
 * slice of time per pixel, keeping each slice's extremes. Dragging across zooms; thinning then runs over
 * the narrower window, which is what reveals detail.
 */
export function TimeSeriesChart(props: Props) {
  const {
    title, lines, spans = [], markers = [], kinds = {}, extent, timezone, formatValue, yDomain, yLabels,
    height = 200, highlight = null, onPick, footnote, fixed = false,
  } = props;
  const clipId = useId();
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

  // Zoom is the caller's when it shares one across charts, otherwise this chart's own.
  const [ownView, setOwnView] = useState<readonly [number, number] | null>(null);
  const controlled = props.onViewChange !== undefined;
  const view = controlled ? props.view ?? null : ownView;
  const setView = (next: [number, number] | null) => {
    if (controlled) props.onViewChange!(next);
    else setOwnView(next);
  };

  const [extentFrom, extentTo] = extent[1] - extent[0] >= 1 ? extent : [extent[0] - 30, extent[0] + 30];
  const x0 = view ? Math.max(extentFrom, view[0]) : extentFrom;
  const x1 = view ? Math.min(extentTo, view[1]) : extentTo;
  const zoomed = view !== null && (x0 > extentFrom || x1 < extentTo);

  const width = Math.max(320, measured || 640);
  const plotWidth = width - GUTTER_LEFT - GUTTER_RIGHT;
  const plotHeight = height - PAD_TOP - GUTTER_BOTTOM;
  const plotBottom = PAD_TOP + plotHeight;

  const thinned = useMemo(
    () => lines.map((line) => ({ line, points: thinSeries(line.data, x0, x1, plotWidth) })),
    [lines, x0, x1, plotWidth],
  );

  let [low, high] = yDomain ?? (() => {
    let lo = Number.POSITIVE_INFINITY;
    let hi = Number.NEGATIVE_INFINITY;
    for (const { points } of thinned) {
      const range = seriesRange(points);
      if (range) {
        lo = Math.min(lo, range[0]);
        hi = Math.max(hi, range[1]);
      }
    }
    return lo <= hi ? [lo, hi] : [0, 1];
  })();
  if (!yDomain) {
    const pad = (high - low) * 0.06 || Math.max(Math.abs(high) * 0.05, 0.5);
    low -= pad;
    high += pad;
  }
  const hasData = thinned.some(({ points }) => seriesRange(points) !== null);

  const x = (t: number) => GUTTER_LEFT + ((t - x0) / (x1 - x0)) * plotWidth;
  const y = (value: number) => PAD_TOP + (1 - (value - low) / (high - low || 1)) * plotHeight;
  const timeAt = (clientX: number) => {
    const rect = svgRef.current?.getBoundingClientRect();
    if (!rect) return x0;
    const within = ((clientX - rect.left) / rect.width) * width - GUTTER_LEFT;
    return x0 + Math.max(0, Math.min(1, within / plotWidth)) * (x1 - x0);
  };

  const clock = useMemo(() => localClock(timezone, extentFrom, extentTo), [timezone, extentFrom, extentTo]);
  const ticks = useMemo(
    () => timeTicks(x0, x1, timezone, Math.max(2, Math.floor(plotWidth / 120)), clock),
    [x0, x1, timezone, plotWidth, clock],
  );
  const levels = yLabels ?? valueTicks(low, high, Math.max(2, Math.floor(plotHeight / 40))).map((value) => ({ value, label: formatValue(value) }));

  const paths = thinned.map(({ line, points }) => {
    let stroke = '';
    let area = '';
    let pen = false;
    let segmentStart = 0;
    let lastX = 0;
    const base = y(Math.max(low, Math.min(high, 0)));
    const close = () => {
      if (pen && line.fill) area += `V${base.toFixed(1)}H${segmentStart.toFixed(1)}Z`;
      pen = false;
    };
    for (let i = 0; i < points.t.length; i += 1) {
      const value = points.v[i]!;
      const px = x(points.t[i]!);
      if (Number.isNaN(value)) {
        if (pen && line.step) {
          stroke += `H${px.toFixed(1)}`;
          if (line.fill) area += `H${px.toFixed(1)}`;
        }
        close();
        continue;
      }
      const py = y(value).toFixed(1);
      if (!pen) {
        stroke += `M${px.toFixed(1)},${py}`;
        if (line.fill) area += `M${px.toFixed(1)},${base.toFixed(1)}V${py}`;
        segmentStart = px;
        pen = true;
      } else if (line.step) {
        stroke += `H${px.toFixed(1)}V${py}`;
        if (line.fill) area += `H${px.toFixed(1)}V${py}`;
      } else {
        stroke += `L${px.toFixed(1)},${py}`;
        if (line.fill) area += `L${px.toFixed(1)},${py}`;
      }
      lastX = px;
    }
    if (pen && line.fill) area += `H${lastX.toFixed(1)}`;
    close();
    return { line, stroke, area };
  });

  // --- pointer: hover readout, drag to zoom, click to pick --------------------------------------------
  const [hover, setHover] = useState<number | null>(null);
  const [drag, setDrag] = useState<{ from: number; to: number } | null>(null);
  const hoverT = hover;

  const readout = hoverT === null ? null : (() => {
    const pxPerSecond = plotWidth / (x1 - x0);
    const values = thinned.map(({ line, points }) => {
      if (!points.t.length) return { line, text: null as string | null };
      const after = lowerBound(points.t, hoverT);
      let index: number;
      if (line.step) index = points.t[after] === hoverT ? after : after - 1;
      else {
        const before = after - 1;
        index = before < 0 ? after : after >= points.t.length ? before
          : hoverT - points.t[before]! <= points.t[after]! - hoverT ? before : after;
      }
      const value = points.v[index];
      if (index < 0 || index >= points.t.length || value === undefined || Number.isNaN(value)) return { line, text: null };
      if (!line.step && Math.abs(points.t[index]! - hoverT) * pxPerSecond > 12) return { line, text: null };
      return { line, text: formatValue(value) };
    });
    const near = markers.filter((marker) => Math.abs(marker.t - hoverT) * pxPerSecond <= 6);
    const within = spans.filter((span) => span.from <= hoverT && hoverT <= span.to);
    return { values, near, within };
  })();

  const span = x1 - x0;
  const withSeconds = span < 2 * 3600;
  const hoverX = hoverT === null ? 0 : x(hoverT);
  const tipLeft = hoverX + 14 > width - 250 ? Math.max(0, hoverX - 264) : hoverX + 14;
  const visibleKinds = Object.entries(kinds).filter(([kind]) =>
    spans.some((s) => s.kind === kind && s.to >= x0 && s.from <= x1) || markers.some((m) => m.kind === kind && m.t >= x0 && m.t <= x1));

  return (
    <figure className="chart">
      <figcaption className="chart__head">
        <span className="chart__title">{title}</span>
        <span className="chart__key">
          {lines.length > 1 && lines.map((line) => (
            <span key={line.id} className="chart__key-item"><i style={{ background: line.color }} />{line.label}</span>
          ))}
          {visibleKinds.map(([kind, style]) => (
            <span key={kind} className="chart__key-item">
              <i className={style.dashed ? 'chart__key-mark--dashed' : undefined} style={{ background: style.color }} />{style.label}
            </span>
          ))}
        </span>
      </figcaption>
      <div ref={boxRef} className="chart__box" style={drag ? { userSelect: 'none' } : undefined}>
        <svg
          ref={svgRef}
          viewBox={`0 0 ${width} ${height}`}
          width="100%"
          height={height}
          role="img"
          aria-label={`${title}, ${formatInstant(x0, timezone, false)} to ${formatInstant(x1, timezone, false)}`}
          className="chart__svg"
          style={{ cursor: fixed ? (onPick ? 'pointer' : 'default') : 'col-resize' }}
          onPointerDown={(event) => {
            if (event.button !== 0) return;
            event.preventDefault();
            event.currentTarget.setPointerCapture(event.pointerId);
            const at = timeAt(event.clientX);
            setDrag({ from: at, to: at });
          }}
          onPointerMove={(event) => {
            const at = timeAt(event.clientX);
            setHover(at);
            if (drag) setDrag({ ...drag, to: at });
          }}
          onPointerLeave={() => { if (!drag) setHover(null); }}
          onPointerUp={(event) => {
            if (!drag) return;
            const from = Math.min(drag.from, drag.to);
            const to = Math.max(drag.from, drag.to);
            const pixels = ((to - from) / (x1 - x0)) * plotWidth;
            setDrag(null);
            if (!fixed && pixels >= 6) {
              const middle = (from + to) / 2;
              const half = Math.max(MIN_ZOOM_SECONDS, to - from) / 2;
              setView([middle - half, middle + half]);
            } else {
              onPick?.(timeAt(event.clientX));
            }
          }}
        >
          <defs>
            <clipPath id={clipId}><rect x={GUTTER_LEFT} y={PAD_TOP} width={plotWidth} height={plotHeight} /></clipPath>
          </defs>

          {levels.map(({ value, label }) => (
            <g key={`${value}`}>
              <line x1={GUTTER_LEFT} x2={GUTTER_LEFT + plotWidth} y1={y(value)} y2={y(value)} className="chart__grid" />
              <text x={GUTTER_LEFT - 8} y={y(value)} textAnchor="end" dominantBaseline="middle" className="chart__tick">{label}</text>
            </g>
          ))}

          <g clipPath={`url(#${clipId})`}>
            {spans.filter((s) => s.to >= x0 && s.from <= x1).map((s, i) => (
              <rect key={`${s.kind}-${i}`} x={x(s.from)} width={Math.max(1, x(s.to) - x(s.from))} y={PAD_TOP} height={plotHeight}
                fill={kinds[s.kind]?.color ?? 'var(--sunken)'} opacity={0.22} />
            ))}
            {highlight && (
              <rect x={x(highlight[0])} width={Math.max(2, x(highlight[1]) - x(highlight[0]))} y={PAD_TOP} height={plotHeight} className="chart__highlight" />
            )}
            {paths.map(({ line, stroke, area }) => (
              <g key={line.id}>
                {line.fill && area && <path d={area} fill={line.color} opacity={0.16} />}
                <path d={stroke} fill="none" stroke={line.color} strokeWidth={1.6} strokeLinejoin="round" />
              </g>
            ))}
            {markers.filter((m) => m.t >= x0 && m.t <= x1).map((m, i) => {
              const style = kinds[m.kind];
              return (
                <g key={`${m.kind}-${i}`}>
                  <line x1={x(m.t)} x2={x(m.t)} y1={PAD_TOP} y2={plotBottom} stroke={style?.color ?? 'var(--ink-3)'}
                    strokeWidth={1.4} strokeDasharray={style?.dashed ? '4 3' : undefined} />
                  <circle cx={x(m.t)} cy={PAD_TOP + 3} r={3} fill={style?.color ?? 'var(--ink-3)'} />
                </g>
              );
            })}
            {drag && Math.abs(x(drag.to) - x(drag.from)) >= 2 && !fixed && (
              <rect x={Math.min(x(drag.from), x(drag.to))} width={Math.abs(x(drag.to) - x(drag.from))} y={PAD_TOP} height={plotHeight} className="chart__selection" />
            )}
            {hoverT !== null && <line x1={hoverX} x2={hoverX} y1={PAD_TOP} y2={plotBottom} className="chart__crosshair" />}
          </g>

          <line x1={GUTTER_LEFT} x2={GUTTER_LEFT + plotWidth} y1={plotBottom} y2={plotBottom} className="chart__axis" />
          {ticks.map((tick) => (
            <g key={tick.t}>
              <line x1={x(tick.t)} x2={x(tick.t)} y1={plotBottom} y2={plotBottom + 4} className="chart__axis" />
              <text x={x(tick.t)} y={plotBottom + 15} textAnchor="middle" className="chart__tick">{tick.label}</text>
              {tick.date && <text x={x(tick.t)} y={plotBottom + 29} textAnchor="middle" className="chart__tick chart__tick--date">{tick.date}</text>}
            </g>
          ))}
          {!hasData && (
            <text x={GUTTER_LEFT + plotWidth / 2} y={PAD_TOP + plotHeight / 2} textAnchor="middle" className="chart__empty">
              Nothing was recorded in this stretch.
            </text>
          )}
        </svg>

        {readout && hoverT !== null && !drag && (
          <div className="chart__tip" style={{ left: tipLeft }} role="status">
            <strong>{formatInstant(hoverT, timezone, withSeconds)}</strong>
            {readout.values.map(({ line, text }) => (
              <span key={line.id}>
                {lines.length > 1 && <i style={{ background: line.color }} />}
                {lines.length > 1 ? `${line.label}: ` : ''}{text ?? 'not recorded'}
              </span>
            ))}
            {readout.within.map((s, i) => <span key={`s${i}`} className="chart__tip-note">{kinds[s.kind]?.label ?? s.kind}</span>)}
            {readout.near.map((m, i) => <span key={`m${i}`} className="chart__tip-note">{m.label}</span>)}
          </div>
        )}
      </div>
      <div className="chart__foot">
        <span>
          {fixed
            ? footnote
            : zoomed
              ? <>Showing {formatInstant(x0, timezone, withSeconds)} to {formatInstant(x1, timezone, withSeconds)}. {footnote}</>
              : <>Drag across the chart to zoom in. {footnote}</>}
        </span>
        {zoomed && !fixed && (
          <button type="button" className="button button--quiet button--small" onClick={() => setView(null)}>
            Show the whole deployment
          </button>
        )}
      </div>
    </figure>
  );
}
