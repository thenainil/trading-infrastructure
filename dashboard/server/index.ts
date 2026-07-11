import { createReadStream } from "node:fs";
import { stat } from "node:fs/promises";
import { createServer, type ServerResponse } from "node:http";
import { extname, join, normalize, relative } from "node:path";
import { fileURLToPath } from "node:url";
import amqp, { type Channel, type ConsumeMessage } from "amqplib";

const rabbitMqUrl = process.env.RABBITMQ_URL ?? "amqp://localhost:5672";
const queueName = process.env.QUEUE_NAME ?? "trade_metrics";
const windowSize = readPositiveInteger("WINDOW_SIZE", 10_000);
const refreshMs = readPositiveInteger("REFRESH_MS", 1_000);
const reconnectMs = readPositiveInteger("RABBITMQ_RECONNECT_MS", 5_000);
const port = readPositiveInteger("PORT", 3_000);
const host = process.env.HOST ?? "0.0.0.0";

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

const clients = new Set<ServerResponse>();
const clientDist = normalize(join(fileURLToPath(new URL("..", import.meta.url)), "client"));

let received = 0;
let rejected = 0;
let connected = false;
let status = "Starting";
let lastMessageAt: string | null = null;
let shuttingDown = false;

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
  lastMessageAt = new Date().toISOString();
}

function snapshot() {
  return {
    queueName,
    windowSize,
    received,
    rejected,
    connected,
    status,
    lastMessageAt,
    generatedAt: new Date().toISOString(),
    metrics: metrics.map(({ key, label }) => ({
      key,
      label,
      unit: "us",
      samples: samples[key].size,
      summary: samples[key].summarize(),
    })),
  };
}

function sendEvent(response: ServerResponse): void {
  response.write(`data: ${JSON.stringify(snapshot())}\n\n`);
}

function broadcast(): void {
  for (const client of clients) sendEvent(client);
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

async function connectRabbitMq(): Promise<void> {
  while (!shuttingDown) {
    try {
      status = "Connecting to RabbitMQ";
      connected = false;
      const connection = await amqp.connect(rabbitMqUrl);
      const channel = await connection.createChannel();

      await channel.checkQueue(queueName);
      await channel.prefetch(1_000);
      await channel.consume(queueName, (message) => consume(channel, message), {
        noAck: false,
      });

      connected = true;
      status = "Live";
      broadcast();

      await new Promise<void>((resolve) => {
        connection.once("close", resolve);
        connection.once("error", (error) => {
          console.error("RabbitMQ connection error:", error);
          resolve();
        });
      });

      connected = false;
      status = "RabbitMQ disconnected";
    } catch (error) {
      connected = false;
      status = "Waiting for RabbitMQ";
      console.error("Unable to connect to RabbitMQ:", error);
    }

    broadcast();
    await new Promise((resolve) => setTimeout(resolve, reconnectMs));
  }
}

function contentType(pathname: string): string {
  switch (extname(pathname)) {
    case ".css":
      return "text/css; charset=utf-8";
    case ".js":
      return "text/javascript; charset=utf-8";
    case ".html":
      return "text/html; charset=utf-8";
    case ".svg":
      return "image/svg+xml";
    default:
      return "application/octet-stream";
  }
}

async function serveStatic(pathname: string, response: ServerResponse): Promise<void> {
  const requestedPath = pathname === "/" ? "/index.html" : pathname;
  const filePath = normalize(join(clientDist, requestedPath));

  if (relative(clientDist, filePath).startsWith("..")) {
    response.writeHead(403).end("Forbidden");
    return;
  }

  try {
    const fileStat = await stat(filePath);
    if (!fileStat.isFile()) throw new Error("not a file");

    response.writeHead(200, { "content-type": contentType(filePath) });
    createReadStream(filePath).pipe(response);
  } catch {
    response.writeHead(200, { "content-type": "text/html; charset=utf-8" });
    createReadStream(join(clientDist, "index.html")).pipe(response);
  }
}

const server = createServer((request, response) => {
  const url = new URL(request.url ?? "/", `http://${request.headers.host ?? "localhost"}`);

  if (url.pathname === "/health") {
    response.writeHead(200, { "content-type": "application/json" });
    response.end(JSON.stringify({ ok: true, connected }));
    return;
  }

  if (url.pathname === "/events") {
    response.writeHead(200, {
      "content-type": "text/event-stream",
      "cache-control": "no-cache",
      connection: "keep-alive",
    });
    clients.add(response);
    sendEvent(response);
    request.on("close", () => clients.delete(response));
    return;
  }

  void serveStatic(url.pathname, response);
});

server.listen(port, host, () => {
  console.log(`Dashboard listening on http://${host}:${port}`);
});

const refreshTimer = setInterval(broadcast, refreshMs);
void connectRabbitMq();

function handleSignal(): void {
  shuttingDown = true;
  clearInterval(refreshTimer);
  for (const client of clients) client.end();
  server.close(() => process.exit(0));
}

process.once("SIGINT", handleSignal);
process.once("SIGTERM", handleSignal);
