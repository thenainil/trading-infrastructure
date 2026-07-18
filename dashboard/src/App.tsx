import { useEffect, useMemo, useState } from "react";

type Summary = {
  min: number;
  mean: number;
  stdDev: number;
  p50: number;
  p99: number;
  p999: number;
  max: number;
};

type MetricRow = {
  key: string;
  label: string;
  unit: string;
  samples: number;
  summary: Summary | null;
};

type BookLevelTelemetry = {
  price: number;
  quantity: number;
};

type TelemetryPayload = {
  identifier: {
    monotonic_id: number;
    symbol: string;
  };
  book_telemetry: {
    bids: BookLevelTelemetry[];
    asks: BookLevelTelemetry[];
  };
  features_telemetry: Record<string, number>;
  strategy_telemetry: {
    decision: string;
  };
};

export type DashboardSnapshot = {
  streamName: string;
  windowSize: number;
  received: number;
  rejected: number;
  connected: boolean;
  status: string;
  lastMessageAt: string | null;
  generatedAt: string;
  latestTelemetry: TelemetryPayload | null;
  decisionHistory: DecisionSample[];
  metrics: MetricRow[];
};

type Props = {
  snapshot: DashboardSnapshot | null;
  eventStreamOpen: boolean;
};

type ChartPoint = {
  id: string;
  received: number;
  p99: number;
  p999: number;
};

type DecisionSample = {
  received: number;
  monotonic_id: number;
  decision: string;
};

type ChartSeries = "p99" | "p999";
type DecisionSide = "buy" | "sell" | "hold" | "unknown";

type ChartModel = {
  min: number;
  max: number;
  path: string;
};

type DepthPoint = {
  price: number;
  quantity: number;
};

const chartWidth = 720;
const chartHeight = 180;
const chartPadding = {
  top: 16,
  right: 16,
  bottom: 28,
  left: 48,
};
const depthChartWidth = 920;
const depthChartHeight = 250;
const depthChartPadding = {
  top: 16,
  right: 28,
  bottom: 34,
  left: 28,
};
const maxChartPoints = 240;
const numberFormatter = new Intl.NumberFormat("en-US");
const latencyFormatter = new Intl.NumberFormat("en-US", {
  maximumFractionDigits: 2,
});
const priceFormatter = new Intl.NumberFormat("en-US", {
  maximumFractionDigits: 2,
  minimumFractionDigits: 1,
});
const quantityFormatter = new Intl.NumberFormat("en-US", {
  maximumFractionDigits: 6,
});

function formatLatency(value: number | undefined): string {
  return value === undefined ? "-" : latencyFormatter.format(value);
}

function formatBookValue(value: number | undefined): string {
  return value === undefined ? "-" : quantityFormatter.format(value);
}

function formatFeatureValue(value: number | undefined): string {
  return value === undefined ? "-" : quantityFormatter.format(value);
}

function formatPrice(value: number | undefined): string {
  return value === undefined ? "-" : priceFormatter.format(value);
}

function formatFeatureName(value: string): string {
  return value.replaceAll("_", " ").replace(/\b\w/g, (character) => character.toUpperCase());
}

function decisionSide(decision: string | undefined): DecisionSide {
  if (decision === "BUY" || decision === "STRONG_BUY") return "buy";
  if (decision === "SELL" || decision === "STRONG_SELL") return "sell";
  if (decision === "WAIT" || decision === "HOLD") return "hold";
  return "unknown";
}

function decisionLabel(decision: string | undefined): string {
  const side = decisionSide(decision);
  if (side === "buy") return decision === "STRONG_BUY" ? "STRONG BUY" : "BUY";
  if (side === "sell") return decision === "STRONG_SELL" ? "STRONG SELL" : "SELL";
  if (side === "hold") return "WAIT";
  return "-";
}

function decisionScore(decision: string | undefined): number {
  if (decision === "STRONG_BUY") return 2;
  if (decision === "BUY") return 1;
  if (decision === "SELL") return -1;
  if (decision === "STRONG_SELL") return -2;
  return 0;
}

function decisionTimelineClass(decision: string): string {
  if (decision === "STRONG_BUY") return "decisionSegmentStrongBuy";
  if (decision === "BUY") return "decisionSegmentBuy";
  if (decision === "SELL") return "decisionSegmentSell";
  if (decision === "STRONG_SELL") return "decisionSegmentStrongSell";
  return "decisionSegmentWait";
}

function formatTime(value: string | null | undefined): string {
  if (!value) return "No messages yet";
  return new Intl.DateTimeFormat("en-US", {
    hour: "numeric",
    minute: "2-digit",
    second: "2-digit",
  }).format(new Date(value));
}

function totalPercentile(snapshot: DashboardSnapshot | null, percentile: ChartSeries): number | null {
  const value = snapshot?.metrics.find((metric) => metric.key === "total_local_latency_ns")?.summary?.[percentile];
  return typeof value === "number" && Number.isFinite(value) ? value : null;
}

function chartX(index: number, pointCount: number): number {
  const plotWidth = chartWidth - chartPadding.left - chartPadding.right;
  return chartPadding.left + (pointCount === 1 ? plotWidth : (index / (pointCount - 1)) * plotWidth);
}

function chartY(value: number, minValue: number, maxValue: number): number {
  const plotHeight = chartHeight - chartPadding.top - chartPadding.bottom;
  const valueRange = maxValue - minValue || 1;
  return chartPadding.top + plotHeight - ((value - minValue) / valueRange) * plotHeight;
}

function buildLinePath(points: ChartPoint[], key: ChartSeries, minValue: number, maxValue: number): string {
  if (points.length === 0) return "";

  return points
    .map((point, index) => {
      const x = chartX(index, points.length);
      const y = chartY(point[key], minValue, maxValue);
      return `${index === 0 ? "M" : "L"} ${x.toFixed(2)} ${y.toFixed(2)}`;
    })
    .join(" ");
}

function buildChart(points: ChartPoint[], series: ChartSeries): ChartModel {
  if (points.length === 0) {
    return { min: 0, max: 0, path: "" };
  }

  const values = points.map((point) => point[series]);
  const min = Math.min(...values);
  const max = Math.max(...values);
  return {
    min,
    max,
    path: buildLinePath(points, series, min, max),
  };
}

function seriesLabel(series: ChartSeries): string {
  if (series === "p99") return "P99";
  return "P99.9";
}

function seriesColorClass(series: ChartSeries): string {
  if (series === "p99") return "chartDotGreen";
  return "chartDotRed";
}

function seriesLineClass(series: ChartSeries): string {
  if (series === "p99") return "chartLineGreen";
  return "chartLineRed";
}

function LatencyChart({ points, chart, series }: { points: ChartPoint[]; chart: ChartModel; series: ChartSeries }) {
  const [hoveredIndex, setHoveredIndex] = useState<number | null>(null);
  const hoveredPoint = hoveredIndex === null ? null : points[hoveredIndex];
  const hoveredValue = hoveredPoint === null ? null : hoveredPoint[series];
  const latest = points.at(-1);
  const tooltipX =
    hoveredIndex === null ? 0 : Math.min(chartWidth - 148, Math.max(64, chartX(hoveredIndex, points.length) - 58));
  const tooltipY =
    hoveredValue === null ? 0 : Math.max(18, chartY(hoveredValue, chart.min, chart.max) - 38);

  return (
    <section className="panel chartPanel">
      <div className="panelHeader">
        <h2>Total Local {seriesLabel(series)}</h2>
        <p>{formatLatency(latest?.[series])} us</p>
      </div>
      <div className="chartWrap">
        <svg
          className="latencyChart"
          viewBox={`0 0 ${chartWidth} ${chartHeight}`}
          role="img"
          onMouseLeave={() => setHoveredIndex(null)}
        >
          <title>Total local latency {seriesLabel(series)} over time</title>
          <line
            className="chartGrid"
            x1={chartPadding.left}
            x2={chartWidth - chartPadding.right}
            y1={chartPadding.top}
            y2={chartPadding.top}
          />
          <line
            className="chartGrid"
            x1={chartPadding.left}
            x2={chartWidth - chartPadding.right}
            y1={chartHeight - chartPadding.bottom}
            y2={chartHeight - chartPadding.bottom}
          />
          <text x={chartPadding.left} y={12} className="chartLabel">
            {formatLatency(chart.max)} us
          </text>
          <text x={chartPadding.left} y={chartHeight - 8} className="chartLabel">
            {formatLatency(chart.min)} us
          </text>
          {chart.path ? <path className={`chartLine ${seriesLineClass(series)}`} d={chart.path} /> : null}
          {points.map((point, index) => (
            <circle
              key={`${point.id}-${series}`}
              className="chartHitPoint"
              cx={chartX(index, points.length)}
              cy={chartY(point[series], chart.min, chart.max)}
              r={7}
              onMouseEnter={() => setHoveredIndex(index)}
              onFocus={() => setHoveredIndex(index)}
              tabIndex={0}
            />
          ))}
          {hoveredPoint && hoveredValue !== null ? (
            <g className="chartTooltip">
              <line
                className="chartHoverLine"
                x1={chartX(hoveredIndex ?? 0, points.length)}
                x2={chartX(hoveredIndex ?? 0, points.length)}
                y1={chartPadding.top}
                y2={chartHeight - chartPadding.bottom}
              />
              <circle
                cx={chartX(hoveredIndex ?? 0, points.length)}
                cy={chartY(hoveredValue, chart.min, chart.max)}
                r={4}
                className={seriesColorClass(series)}
              />
              <rect x={tooltipX} y={tooltipY} width={118} height={34} rx={6} />
              <text x={tooltipX + 10} y={tooltipY + 18}>
                {seriesLabel(series)} {formatLatency(hoveredValue)} us
              </text>
            </g>
          ) : null}
        </svg>
      </div>
    </section>
  );
}

function DecisionHistoryChart({ points }: { points: DecisionSample[] }) {
  const latestEvent = [...points].reverse().find((point) => decisionScore(point.decision) !== 0);
  const waitCount = points.filter((point) => decisionScore(point.decision) === 0).length;
  const buyCount = points.filter((point) => decisionSide(point.decision) === "buy").length;
  const sellCount = points.filter((point) => decisionSide(point.decision) === "sell").length;

  return (
    <section className="panel decisionChartPanel">
      <div className="panelHeader">
        <h2>Decision History</h2>
        <p>{numberFormatter.format(points.length)} recent samples</p>
      </div>
      <div className="decisionTimelineWrap">
        <div className="decisionTimeline" aria-label="Decision timeline">
          {points.map((point, index) => (
            <span
              className={`decisionSegment ${decisionTimelineClass(point.decision)}`}
              key={`${point.received}-${point.monotonic_id}-${index}`}
              title={`${decisionLabel(point.decision)} at #${numberFormatter.format(point.monotonic_id)}`}
            />
          ))}
          {points.length === 0 ? <span className="decisionTimelineEmpty">Waiting for decisions.</span> : null}
        </div>
        <div className="decisionLegend">
          <span className="legendStrongBuy">Strong Buy</span>
          <span className="legendBuy">Buy</span>
          <span className="legendWait">Wait</span>
          <span className="legendSell">Sell</span>
          <span className="legendStrongSell">Strong Sell</span>
        </div>
        <div className="decisionSummary">
          <div>
            <span>Latest Event</span>
            <strong className={latestEvent ? decisionTimelineClass(latestEvent.decision) : ""}>
              {latestEvent ? decisionLabel(latestEvent.decision) : "-"}
            </strong>
          </div>
          <div>
            <span>Buy</span>
            <strong>{numberFormatter.format(buyCount)}</strong>
          </div>
          <div>
            <span>Wait</span>
            <strong>{numberFormatter.format(waitCount)}</strong>
          </div>
          <div>
            <span>Sell</span>
            <strong>{numberFormatter.format(sellCount)}</strong>
          </div>
        </div>
      </div>
    </section>
  );
}

function activeBookLevels(levels: BookLevelTelemetry[]): BookLevelTelemetry[] {
  return levels.filter((level) => level.price > 0 && level.quantity > 0);
}

function cumulativeBids(bids: BookLevelTelemetry[]): DepthPoint[] {
  const ascending = [...bids].reverse();
  return ascending.map((level, index) => ({
    price: level.price,
    quantity: ascending.slice(index).reduce((sum, item) => sum + item.quantity, 0),
  }));
}

function cumulativeAsks(asks: BookLevelTelemetry[]): DepthPoint[] {
  let cumulative = 0;
  return asks.map((level) => {
    cumulative += level.quantity;
    return {
      price: level.price,
      quantity: cumulative,
    };
  });
}

function depthX(price: number, minPrice: number, maxPrice: number): number {
  const plotWidth = depthChartWidth - depthChartPadding.left - depthChartPadding.right;
  const range = maxPrice - minPrice || 1;
  return depthChartPadding.left + ((price - minPrice) / range) * plotWidth;
}

function depthY(quantity: number, maxQuantity: number): number {
  const plotHeight = depthChartHeight - depthChartPadding.top - depthChartPadding.bottom;
  return depthChartPadding.top + plotHeight - (quantity / (maxQuantity || 1)) * plotHeight;
}

function buildDepthPath(points: DepthPoint[], minPrice: number, maxPrice: number, maxQuantity: number): string {
  return points
    .map((point, index) => {
      const x = depthX(point.price, minPrice, maxPrice);
      const y = depthY(point.quantity, maxQuantity);
      return `${index === 0 ? "M" : "L"} ${x.toFixed(2)} ${y.toFixed(2)}`;
    })
    .join(" ");
}

function buildDepthArea(points: DepthPoint[], minPrice: number, maxPrice: number, maxQuantity: number): string {
  if (points.length === 0) return "";

  const path = buildDepthPath(points, minPrice, maxPrice, maxQuantity);
  const first = points[0];
  const last = points[points.length - 1];
  const baseline = depthChartHeight - depthChartPadding.bottom;
  return `${path} L ${depthX(last.price, minPrice, maxPrice).toFixed(2)} ${baseline} L ${depthX(
    first.price,
    minPrice,
    maxPrice,
  ).toFixed(2)} ${baseline} Z`;
}

function depthRowAt(levels: BookLevelTelemetry[], index: number): BookLevelTelemetry | undefined {
  return levels[index];
}

function OrderBookPanel({ telemetry }: { telemetry: TelemetryPayload | null | undefined }) {
  const bids = activeBookLevels(telemetry?.book_telemetry.bids ?? []);
  const asks = activeBookLevels(telemetry?.book_telemetry.asks ?? []);
  const bestBid = bids[0]?.price;
  const bestAsk = asks[0]?.price;
  const midPrice = bestBid !== undefined && bestAsk !== undefined ? (bestBid + bestAsk) / 2 : undefined;
  const micropriceValue = telemetry?.features_telemetry.microprice;
  const microprice =
    typeof micropriceValue === "number" && Number.isFinite(micropriceValue) ? micropriceValue : undefined;
  const spread = bestBid !== undefined && bestAsk !== undefined ? bestAsk - bestBid : undefined;
  const bidDepth = cumulativeBids(bids);
  const askDepth = cumulativeAsks(asks);
  const allDepth = [...bidDepth, ...askDepth];
  const prices = allDepth.map((point) => point.price);
  const minPrice = prices.length > 0 ? Math.min(...prices) : 0;
  const maxPrice = prices.length > 0 ? Math.max(...prices) : 1;
  const maxQuantity = Math.max(0, ...allDepth.map((point) => point.quantity));
  const bidPath = buildDepthPath(bidDepth, minPrice, maxPrice, maxQuantity);
  const askPath = buildDepthPath(askDepth, minPrice, maxPrice, maxQuantity);
  const bidArea = buildDepthArea(bidDepth, minPrice, maxPrice, maxQuantity);
  const askArea = buildDepthArea(askDepth, minPrice, maxPrice, maxQuantity);
  const micropriceX = microprice === undefined ? null : depthX(microprice, minPrice, maxPrice);
  const rowCount = Math.max(bids.length, asks.length);

  return (
    <section className="panel orderBookPanel">
      <div className="panelHeader">
        <h2>Order Book</h2>
        <p>{telemetry?.identifier.symbol || "-"}</p>
      </div>
      <div className="depthBook">
        <div className="bookTopLine">
          <div>
            <span>Best Bid</span>
            <strong className="bidText">{formatPrice(bestBid)}</strong>
          </div>
          <div>
            <span>Microprice</span>
            <strong>{formatPrice(microprice)}</strong>
          </div>
          <div>
            <span>Best Ask</span>
            <strong className="askText">{formatPrice(bestAsk)}</strong>
          </div>
        </div>

        <div className="depthChartWrap">
          <svg className="depthChart" viewBox={`0 0 ${depthChartWidth} ${depthChartHeight}`} role="img">
            <title>Order book cumulative depth</title>
            <line
              className="depthGrid"
              x1={depthChartPadding.left}
              x2={depthChartWidth - depthChartPadding.right}
              y1={depthChartPadding.top}
              y2={depthChartPadding.top}
            />
            <line
              className="depthGrid"
              x1={depthChartPadding.left}
              x2={depthChartWidth - depthChartPadding.right}
              y1={depthChartHeight - depthChartPadding.bottom}
              y2={depthChartHeight - depthChartPadding.bottom}
            />
            {bidArea ? <path className="depthArea depthBidArea" d={bidArea} /> : null}
            {askArea ? <path className="depthArea depthAskArea" d={askArea} /> : null}
            {bidPath ? <path className="depthLine depthBidLine" d={bidPath} /> : null}
            {askPath ? <path className="depthLine depthAskLine" d={askPath} /> : null}
            {micropriceX !== null ? (
              <>
                <line
                  className="depthMidLine"
                  x1={micropriceX}
                  x2={micropriceX}
                  y1={depthChartPadding.top}
                  y2={depthChartHeight - depthChartPadding.bottom}
                />
                <text className="depthMidLabel" x={micropriceX} y={depthChartPadding.top + 14}>
                  {formatPrice(microprice)}
                </text>
              </>
            ) : null}
            <text className="depthAxisLabel" x={depthChartPadding.left} y={depthChartHeight - 9}>
              {formatPrice(minPrice)}
            </text>
            <text className="depthAxisLabel" x={depthChartWidth - depthChartPadding.right} y={depthChartHeight - 9}>
              {formatPrice(maxPrice)}
            </text>
          </svg>
        </div>

        <div className="depthStats">
          <span>Spread {formatPrice(spread)}</span>
          <span>Visible levels {numberFormatter.format(rowCount)}</span>
        </div>

        <div className="depthTable">
          <div className="depthTableHeader">
            <span>Size</span>
            <span>Bid</span>
            <span>Ask</span>
            <span>Size</span>
          </div>
          {Array.from({ length: rowCount }, (_, index) => {
            const bid = depthRowAt(bids, index);
            const ask = depthRowAt(asks, index);
            return (
              <div className="depthTableRow" key={`${bid?.price ?? "none"}-${ask?.price ?? "none"}-${index}`}>
                <span>{formatBookValue(bid?.quantity)}</span>
                <strong className="bidText">{formatPrice(bid?.price)}</strong>
                <strong className="askText">{formatPrice(ask?.price)}</strong>
                <span>{formatBookValue(ask?.quantity)}</span>
              </div>
            );
          })}
          {rowCount === 0 ? <div className="bookEmpty">Waiting for book levels.</div> : null}
        </div>
      </div>
    </section>
  );
}

function FeaturesPanel({ telemetry }: { telemetry: TelemetryPayload | null | undefined }) {
  const features = Object.entries(telemetry?.features_telemetry ?? {});

  return (
    <section className="panel featuresPanel">
      <div className="panelHeader">
        <h2>Features</h2>
        <p>{numberFormatter.format(features.length)} available</p>
      </div>
      <div className="featuresGrid">
        {features.map(([key, value]) => (
          <div className="featureItem" key={key}>
            <span>{formatFeatureName(key)}</span>
            <strong>{formatFeatureValue(value)}</strong>
          </div>
        ))}
        {features.length === 0 ? <div className="featuresEmpty">Waiting for features.</div> : null}
      </div>
    </section>
  );
}

export function App({ snapshot, eventStreamOpen }: Props) {
  const metrics = snapshot?.metrics ?? [];
  const connected = Boolean(snapshot?.connected && eventStreamOpen);
  const [tailLatencyHistory, setTailLatencyHistory] = useState<ChartPoint[]>([]);
  const latestTotalP99 = totalPercentile(snapshot, "p99");
  const latestTotalP999 = totalPercentile(snapshot, "p999");

  useEffect(() => {
    if (snapshot === null || latestTotalP99 === null || latestTotalP999 === null) return;

    setTailLatencyHistory((history) => {
      const last = history.at(-1);
      if (last?.id === snapshot.generatedAt || last?.received === snapshot.received) {
        return history;
      }

      return [
        ...history,
        {
          id: snapshot.generatedAt,
          received: snapshot.received,
          p99: latestTotalP99,
          p999: latestTotalP999,
        },
      ].slice(-maxChartPoints);
    });
  }, [latestTotalP99, latestTotalP999, snapshot]);

  const p99Chart = useMemo(() => buildChart(tailLatencyHistory, "p99"), [tailLatencyHistory]);
  const p999Chart = useMemo(() => buildChart(tailLatencyHistory, "p999"), [tailLatencyHistory]);

  return (
    <main className="shell">
      <header className="topbar">
        <div>
          <h1>Orderbook</h1>
          <a
            className="sourceLink"
            href="https://github.com/thenainil/order-book"
            target="_blank"
            rel="noreferrer"
          >
            &lt;source_code&gt;
          </a>
        </div>
        <div className={connected ? "status statusOk" : "status statusWarn"}>
          <span aria-hidden="true" />
          {snapshot?.status ?? "Connecting"}
        </div>
      </header>

      <section className="stats" aria-label="Dashboard status">
        <div>
          <span>Received</span>
          <strong>{numberFormatter.format(snapshot?.received ?? 0)}</strong>
        </div>
        <div>
          <span>Rejected</span>
          <strong>{numberFormatter.format(snapshot?.rejected ?? 0)}</strong>
        </div>
        <div>
          <span>Last Message</span>
          <strong>{formatTime(snapshot?.lastMessageAt)}</strong>
        </div>
      </section>

      <OrderBookPanel telemetry={snapshot?.latestTelemetry} />
      <DecisionHistoryChart points={snapshot?.decisionHistory ?? []} />
      <FeaturesPanel telemetry={snapshot?.latestTelemetry} />

      <section className="sectionGroup" aria-label="Latency metrics">
        <div className="sectionHeader">
          <h2>Latency Metrics</h2>
        </div>

        <LatencyChart points={tailLatencyHistory} chart={p99Chart} series="p99" />
        <LatencyChart points={tailLatencyHistory} chart={p999Chart} series="p999" />

        <section className="panel">
          <div className="panelHeader">
            <h2>Rolling Latency</h2>
            <p>
              Window: {numberFormatter.format(snapshot?.windowSize ?? 0)} samples
            </p>
          </div>

          <div className="tableWrap">
            <table>
              <thead>
              <tr>
                <th>Latency Metric</th>
                <th>Samples</th>
                <th>Min</th>
                <th>P50</th>
                <th>Mean</th>
                <th>P99</th>
                <th>P99.9</th>
                <th>Max</th>
                <th>Std Dev</th>
              </tr>
            </thead>
              <tbody>
                {metrics.map((metric) => (
                  <tr key={metric.key}>
                    <td>
                    <div className="metricName">
                      <strong>{metric.label}</strong>
                      {metric.key === "network_latency_us" ? (
                        <em>unsynced clock, do not trust</em>
                      ) : null}
                      <span>{metric.unit}</span>
                    </div>
                  </td>
                  <td>{numberFormatter.format(metric.samples)}</td>
                  <td>{formatLatency(metric.summary?.min)}</td>
                  <td>{formatLatency(metric.summary?.p50)}</td>
                  <td>{formatLatency(metric.summary?.mean)}</td>
                  <td>{formatLatency(metric.summary?.p99)}</td>
                  <td>{formatLatency(metric.summary?.p999)}</td>
                  <td>{formatLatency(metric.summary?.max)}</td>
                  <td>{formatLatency(metric.summary?.stdDev)}</td>
                </tr>
                ))}
                {metrics.length === 0 && (
                  <tr>
                    <td colSpan={9} className="empty">
                      Waiting for the metrics stream.
                    </td>
                  </tr>
                )}
              </tbody>
            </table>
          </div>
        </section>
      </section>
    </main>
  );
}
