type Summary = {
  min: number;
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

export type DashboardSnapshot = {
  queueName: string;
  windowSize: number;
  received: number;
  rejected: number;
  connected: boolean;
  status: string;
  lastMessageAt: string | null;
  generatedAt: string;
  metrics: MetricRow[];
};

type Props = {
  snapshot: DashboardSnapshot | null;
  eventStreamOpen: boolean;
};

const numberFormatter = new Intl.NumberFormat("en-US");
const latencyFormatter = new Intl.NumberFormat("en-US", {
  maximumFractionDigits: 2,
});

function formatLatency(value: number | undefined): string {
  return value === undefined ? "-" : latencyFormatter.format(value);
}

function formatTime(value: string | null | undefined): string {
  if (!value) return "No messages yet";
  return new Intl.DateTimeFormat("en-US", {
    hour: "numeric",
    minute: "2-digit",
    second: "2-digit",
  }).format(new Date(value));
}

export function App({ snapshot, eventStreamOpen }: Props) {
  const metrics = snapshot?.metrics ?? [];
  const connected = Boolean(snapshot?.connected && eventStreamOpen);

  return (
    <main className="shell">
      <header className="topbar">
        <div>
          <p className="eyebrow">Trading Infrastructure</p>
          <h1>Latency Dashboard</h1>
        </div>
        <div className={connected ? "status statusOk" : "status statusWarn"}>
          <span aria-hidden="true" />
          {snapshot?.status ?? "Connecting"}
        </div>
      </header>

      <section className="stats" aria-label="Dashboard status">
        <div>
          <span>Queue</span>
          <strong>{snapshot?.queueName ?? "-"}</strong>
        </div>
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
                <th>Metric</th>
                <th>Samples</th>
                <th>Min</th>
                <th>P50</th>
                <th>P99</th>
                <th>P99.9</th>
                <th>Max</th>
              </tr>
            </thead>
            <tbody>
              {metrics.map((metric) => (
                <tr key={metric.key}>
                  <td>
                    <div className="metricName">
                      <strong>{metric.label}</strong>
                      <span>{metric.unit}</span>
                    </div>
                  </td>
                  <td>{numberFormatter.format(metric.samples)}</td>
                  <td>{formatLatency(metric.summary?.min)}</td>
                  <td>{formatLatency(metric.summary?.p50)}</td>
                  <td>{formatLatency(metric.summary?.p99)}</td>
                  <td>{formatLatency(metric.summary?.p999)}</td>
                  <td>{formatLatency(metric.summary?.max)}</td>
                </tr>
              ))}
              {metrics.length === 0 && (
                <tr>
                  <td colSpan={7} className="empty">
                    Waiting for the metrics stream.
                  </td>
                </tr>
              )}
            </tbody>
          </table>
        </div>
      </section>
    </main>
  );
}
