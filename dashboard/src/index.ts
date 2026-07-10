import amqp, { type Channel, type ConsumeMessage } from "amqplib";

const rabbitMqUrl = process.env.RABBITMQ_URL ?? "amqp://localhost:5672";
const queueName = process.env.QUEUE_NAME ?? "trade_metrics_c";
const windowSize = readPositiveInteger("WINDOW_SIZE", 10_000);
const refreshMs = readPositiveInteger("REFRESH_MS", 1_000);

const metrics = [
  { key: "network_latency_us", label: "network_latency", divisor: 1 },
  { key: "parse_latency_ns", label: "parse_latency", divisor: 1_000 },
  { key: "order_book_latency_ns", label: "order_book_latency", divisor: 1_000 },
  { key: "feature_latency_ns", label: "feature_latency", divisor: 1_000 },
  { key: "strategy_latency_ns", label: "strategy_latency", divisor: 1_000 },
  { key: "total_local_latency_ns", label: "total_local_latency", divisor: 1_000 },
] as const;

type MetricKey = (typeof metrics)[number]["key"];
type TradeMetrics = Record<MetricKey, number>;

class SampleWindow {
  private readonly values: number[] = [];
  private nextIndex = 0;

  constructor(private readonly capacity: number) {}

  add(value: number): void {
    if (this.values.length < this.capacity) {
      this.values.push(value);
      return;
    }

    this.values[this.nextIndex] = value;
    this.nextIndex = (this.nextIndex + 1) % this.capacity;
  }

  summarize(): { min: number; p50: number; p99: number; p999: number; max: number } | null {
    if (this.values.length === 0) return null;

    const sorted = [...this.values].sort((a, b) => a - b);
    return {
      min: sorted[0],
      p50: percentile(sorted, 0.5),
      p99: percentile(sorted, 0.99),
      p999: percentile(sorted, 0.999),
      max: sorted[sorted.length - 1],
    };
  }

  get size(): number {
    return this.values.length;
  }
}

const samples = Object.fromEntries(
  metrics.map(({ key }) => [key, new SampleWindow(windowSize)]),
) as Record<MetricKey, SampleWindow>;

let received = 0;
let rejected = 0;

function readPositiveInteger(name: string, fallback: number): number {
  const raw = process.env[name];
  if (raw === undefined) return fallback;

  const value = Number(raw);
  if (!Number.isSafeInteger(value) || value <= 0) {
    throw new Error(`${name} must be a positive integer`);
  }
  return value;
}

function percentile(sorted: readonly number[], percentileValue: number): number {
  const index = Math.max(0, Math.ceil(sorted.length * percentileValue) - 1);
  return sorted[index];
}

function parseMetrics(message: ConsumeMessage): TradeMetrics {
  const parsed: unknown = JSON.parse(message.content.toString("utf8"));
  if (typeof parsed !== "object" || parsed === null || Array.isArray(parsed)) {
    throw new Error("payload must be a JSON object");
  }

  const record = parsed as Record<string, unknown>;
  for (const { key } of metrics) {
    if (typeof record[key] !== "number" || !Number.isFinite(record[key])) {
      throw new Error(`${key} must be a finite number`);
    }
  }

  return record as TradeMetrics;
}

function addMetrics(payload: TradeMetrics): void {
  for (const { key, divisor } of metrics) {
    samples[key].add(payload[key] / divisor);
  }
  received += 1;
}

function format(value: number): string {
  return value.toLocaleString("en-US", { maximumFractionDigits: 2 });
}

function render(): void {
  if (process.stdout.isTTY) process.stdout.write("\u001b[2J\u001b[H");

  const rows = metrics.map(({ key, label }) => {
    const summary = samples[key].summarize();
    return {
      metric: label,
      unit: "us",
      samples: samples[key].size.toLocaleString("en-US"),
      min: summary ? format(summary.min) : "-",
      p50: summary ? format(summary.p50) : "-",
      p99: summary ? format(summary.p99) : "-",
      "p99.9": summary ? format(summary.p999) : "-",
      max: summary ? format(summary.max) : "-",
    };
  });

  console.log(
    `Queue: ${queueName} | received: ${received.toLocaleString("en-US")} | rejected: ${rejected.toLocaleString("en-US")} | rolling window: ${windowSize.toLocaleString("en-US")}`,
  );
  console.table(rows);
}

function consume(channel: Channel, message: ConsumeMessage | null): void {
  if (message === null) return;

  try {
    addMetrics(parseMetrics(message));
    channel.ack(message);
  } catch (error) {
    rejected += 1;
    channel.nack(message, false, false);
    console.error("Rejected malformed message:", error);
  }
}

async function main(): Promise<void> {
  const connection = await amqp.connect(rabbitMqUrl);
  const channel = await connection.createChannel();

  await channel.checkQueue(queueName);
  await channel.prefetch(1_000);
  await channel.consume(queueName, (message) => consume(channel, message), {
    noAck: false,
  });

  render();
  const refreshTimer = setInterval(render, refreshMs);
  let shuttingDown = false;

  connection.on("error", (error) => console.error("RabbitMQ connection error:", error));
  connection.on("close", () => {
    clearInterval(refreshTimer);
    if (!shuttingDown) {
      console.error("RabbitMQ connection closed unexpectedly");
      process.exitCode = 1;
    }
  });

  const shutdown = async (): Promise<void> => {
    if (shuttingDown) return;
    shuttingDown = true;
    clearInterval(refreshTimer);
    await channel.close();
    await connection.close();
  };

  const handleSignal = (): void => {
    void shutdown().catch((error) => {
      console.error("Unable to shut down cleanly:", error);
      process.exitCode = 1;
    });
  };

  process.once("SIGINT", handleSignal);
  process.once("SIGTERM", handleSignal);
}

main().catch((error) => {
  console.error("Unable to start metrics consumer:", error);
  process.exitCode = 1;
});

/*
1. NAIVE IMPLEMENTATION
Queue: trade_metrics_c | received: 192,442 | rejected: 0 | rolling window: 10,000
┌─────────┬───────────────────────┬──────┬──────────┬──────────┬──────────┬────────────┬────────────┬────────────┐
│ (index) │ metric                │ unit │ samples  │ min      │ p50      │ p99        │ p99.9      │ max        │
├─────────┼───────────────────────┼──────┼──────────┼──────────┼──────────┼────────────┼────────────┼────────────┤
│ 0       │ 'network_latency'     │ 'us' │ '10,000' │ '15,226' │ '18,491' │ '65,208'   │ '116,635'  │ '126,159'  │
│ 1       │ 'parse_latency'       │ 'us' │ '10,000' │ '8.38'   │ '14.46'  │ '418.58'   │ '5,955.83' │ '9,553.46' │
│ 2       │ 'order_book_latency'  │ 'us' │ '10,000' │ '1.17'   │ '2.58'   │ '138.83'   │ '1,968.96' │ '4,531.58' │
│ 3       │ 'feature_latency'     │ 'us' │ '10,000' │ '0.25'   │ '0.38'   │ '2.42'     │ '15.54'    │ '85.33'    │
│ 4       │ 'strategy_latency'    │ 'us' │ '10,000' │ '0.33'   │ '1.38'   │ '165.08'   │ '2,539.38' │ '5,132.88' │
│ 5       │ 'total_local_latency' │ 'us' │ '10,000' │ '11.25'  │ '23.33'  │ '1,215.38' │ '6,174.38' │ '9,647.08' │
└─────────┴───────────────────────┴──────┴──────────┴──────────┴──────────┴────────────┴────────────┴────────────┘


*/