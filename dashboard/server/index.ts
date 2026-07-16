import { createReadStream } from "node:fs";
import { stat } from "node:fs/promises";
import { createServer, type ServerResponse } from "node:http";
import { extname, join, normalize, relative } from "node:path";
import { fileURLToPath } from "node:url";
import { WebSocketServer, type WebSocket } from "ws";

const ingestPath = process.env.METRICS_INGEST_PATH ?? "/ingest";
const windowSize = readPositiveInteger("WINDOW_SIZE", 10_000);
const refreshMs = readPositiveInteger("REFRESH_MS", 1_000);
const ingestBatchSize = readPositiveInteger("INGEST_BATCH_SIZE", 1_000);
const decisionHistorySize = readPositiveInteger("DECISION_HISTORY_SIZE", 240);
const port = readPositiveInteger("PORT", 3_000);
const host = process.env.HOST ?? "0.0.0.0";

const metrics = [
  { key: "network_latency_us", label: "Network", divisor: 1 },
  { key: "parse_latency_ns", label: "Parse", divisor: 1_000 },
  { key: "order_book_latency_ns", label: "Order Book", divisor: 1_000 },
  { key: "feature_latency_ns", label: "Features", divisor: 1_000 },
  { key: "strategy_latency_ns", label: "Strategy", divisor: 1_000 },
  { key: "total_local_latency_ns", label: "Total Local", divisor: 1_000 },
] as const;

type MetricKey = (typeof metrics)[number]["key"];
type TradeMetrics = Record<MetricKey, number>;
type BookLevelTelemetry = {
  price: number;
  quantity: number;
};
type TelemetryPayload = {
  identifier: {
    monotonic_id: number;
    symbol: string;
  };
  latency_metrics: TradeMetrics;
  book_telemetry: {
    bids: BookLevelTelemetry[];
    asks: BookLevelTelemetry[];
  };
  features_telemetry: Record<string, number>;
  strategy_telemetry: {
    decision: string;
  };
};
type DecisionSample = {
  received: number;
  monotonic_id: number;
  decision: string;
};

class SampleWindow {
  private readonly values: number[] = [];
  private nextIndex = 0;
  private sum = 0;
  private sumSquares = 0;

  constructor(private readonly capacity: number) {}

  add(value: number): void {
    if (this.values.length < this.capacity) {
      this.values.push(value);
      this.sum += value;
      this.sumSquares += value * value;
      return;
    }

    const previous = this.values[this.nextIndex];
    this.sum += value - previous;
    this.sumSquares += value * value - previous * previous;
    this.values[this.nextIndex] = value;
    this.nextIndex = (this.nextIndex + 1) % this.capacity;
  }

  summarize(): {
    min: number;
    mean: number;
    stdDev: number;
    p50: number;
    p99: number;
    p999: number;
    max: number;
  } | null {
    if (this.values.length === 0) return null;

    const mean = this.sum / this.values.length;
    const variance = Math.max(0, this.sumSquares / this.values.length - mean * mean);
    const sorted = [...this.values].sort((a, b) => a - b);
    return {
      min: sorted[0],
      mean,
      stdDev: Math.sqrt(variance),
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
const ingestClients = new Set<WebSocket>();
const clientDist = normalize(join(fileURLToPath(new URL("..", import.meta.url)), "client"));

let received = 0;
let rejected = 0;
let connected = false;
let status = "Waiting for telemetry WebSocket";
let lastMessageAt: string | null = null;
let latestTelemetry: TelemetryPayload | null = null;
let decisionHistory: DecisionSample[] = [];
let snapshotDirty = true;
let queuedMessages: string[] = [];
let queuedMessageOffset = 0;
let queueProcessingScheduled = false;

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

function isFiniteNumber(value: unknown): value is number {
  return typeof value === "number" && Number.isFinite(value);
}

function parseMetricsRecord(record: Record<string, unknown>): TradeMetrics {
  for (const { key } of metrics) {
    if (!isFiniteNumber(record[key])) {
      throw new Error(`${key} must be a finite number`);
    }
  }

  return record as TradeMetrics;
}

function parseTelemetryPayload(record: Record<string, unknown>): TelemetryPayload | null {
  const latencyMetrics = record.latency_metrics;
  if (typeof latencyMetrics !== "object" || latencyMetrics === null || Array.isArray(latencyMetrics)) {
    return null;
  }

  const identifier = record.identifier;
  const bookTelemetry = record.book_telemetry;
  const featuresTelemetry = record.features_telemetry;
  const strategyTelemetry = record.strategy_telemetry;

  if (typeof identifier !== "object" || identifier === null || Array.isArray(identifier)) {
    throw new Error("identifier must be a JSON object");
  }
  if (typeof bookTelemetry !== "object" || bookTelemetry === null || Array.isArray(bookTelemetry)) {
    throw new Error("book_telemetry must be a JSON object");
  }
  if (typeof featuresTelemetry !== "object" || featuresTelemetry === null || Array.isArray(featuresTelemetry)) {
    throw new Error("features_telemetry must be a JSON object");
  }
  if (typeof strategyTelemetry !== "object" || strategyTelemetry === null || Array.isArray(strategyTelemetry)) {
    throw new Error("strategy_telemetry must be a JSON object");
  }

  const identifierRecord = identifier as Record<string, unknown>;
  const bookRecord = bookTelemetry as Record<string, unknown>;
  const strategyRecord = strategyTelemetry as Record<string, unknown>;

  if (!isFiniteNumber(identifierRecord.monotonic_id)) {
    throw new Error("identifier.monotonic_id must be a finite number");
  }
  if (typeof identifierRecord.symbol !== "string") {
    throw new Error("identifier.symbol must be a string");
  }
  if (!Array.isArray(bookRecord.bids) || !Array.isArray(bookRecord.asks)) {
    throw new Error("book_telemetry bids and asks must be arrays");
  }
  if (typeof strategyRecord.decision !== "string") {
    throw new Error("strategy_telemetry.decision must be a string");
  }

  return {
    identifier: {
      monotonic_id: identifierRecord.monotonic_id,
      symbol: identifierRecord.symbol,
    },
    latency_metrics: parseMetricsRecord(latencyMetrics as Record<string, unknown>),
    book_telemetry: {
      bids: parseBookLevels(bookRecord.bids),
      asks: parseBookLevels(bookRecord.asks),
    },
    features_telemetry: parseNumberRecord(featuresTelemetry as Record<string, unknown>, "features_telemetry"),
    strategy_telemetry: {
      decision: strategyRecord.decision,
    },
  };
}

function parseMetrics(message: string): { metrics: TradeMetrics; telemetry: TelemetryPayload | null } {
  const parsed: unknown = JSON.parse(message);
  if (typeof parsed !== "object" || parsed === null || Array.isArray(parsed)) {
    throw new Error("payload must be a JSON object");
  }

  const record = parsed as Record<string, unknown>;
  const telemetry = parseTelemetryPayload(record);
  const metricsPayload = telemetry?.latency_metrics ?? parseMetricsRecord(record);

  return { metrics: metricsPayload, telemetry };
}

function parseBookLevels(value: unknown[]): BookLevelTelemetry[] {
  return value.map((level, index) => {
    if (typeof level !== "object" || level === null || Array.isArray(level)) {
      throw new Error(`book level ${index} must be a JSON object`);
    }

    const record = level as Record<string, unknown>;
    if (!isFiniteNumber(record.price) || !isFiniteNumber(record.quantity)) {
      throw new Error(`book level ${index} price and quantity must be finite numbers`);
    }

    return {
      price: record.price,
      quantity: record.quantity,
    };
  });
}

function parseNumberRecord(record: Record<string, unknown>, name: string): Record<string, number> {
  return Object.fromEntries(
    Object.entries(record).map(([key, value]) => {
      if (!isFiniteNumber(value)) {
        throw new Error(`${name}.${key} must be a finite number`);
      }
      return [key, value];
    }),
  );
}

function addMetrics(payload: TradeMetrics): void {
  for (const { key, divisor } of metrics) {
    samples[key].add(payload[key] / divisor);
  }
  received += 1;
  lastMessageAt = new Date().toISOString();
  snapshotDirty = true;
}

function addDecisionSample(telemetry: TelemetryPayload | null): void {
  if (telemetry === null) return;

  decisionHistory.push({
    received: received + 1,
    monotonic_id: telemetry.identifier.monotonic_id,
    decision: telemetry.strategy_telemetry.decision,
  });

  if (decisionHistory.length > decisionHistorySize) {
    decisionHistory = decisionHistory.slice(-decisionHistorySize);
  }
}

function snapshot() {
  return {
    streamName: `ws:${ingestPath}`,
    windowSize,
    received,
    rejected,
    connected,
    status,
    lastMessageAt,
    latestTelemetry,
    decisionHistory,
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

let cachedSnapshot: ReturnType<typeof snapshot> | null = null;

function markSnapshotDirty(): void {
  snapshotDirty = true;
}

function getSnapshot(): ReturnType<typeof snapshot> {
  if (cachedSnapshot === null || snapshotDirty) {
    cachedSnapshot = snapshot();
    snapshotDirty = false;
  }

  return cachedSnapshot;
}

function sendEvent(response: ServerResponse): void {
  response.write(`data: ${JSON.stringify(getSnapshot())}\n\n`);
}

function broadcast(): void {
  for (const client of clients) sendEvent(client);
}

function compactQueuedMessages(): void {
  if (queuedMessageOffset > 10_000 && queuedMessageOffset * 2 > queuedMessages.length) {
    queuedMessages = queuedMessages.slice(queuedMessageOffset);
    queuedMessageOffset = 0;
  }
}

function processQueuedMessages(): void {
  queueProcessingScheduled = false;

  const end = Math.min(queuedMessageOffset + ingestBatchSize, queuedMessages.length);
  for (; queuedMessageOffset < end; queuedMessageOffset += 1) {
    consume(queuedMessages[queuedMessageOffset]);
  }

  compactQueuedMessages();

  if (queuedMessageOffset < queuedMessages.length) {
    scheduleQueueProcessing();
  }
}

function scheduleQueueProcessing(): void {
  if (queueProcessingScheduled) return;
  queueProcessingScheduled = true;
  setImmediate(processQueuedMessages);
}

function enqueueMessage(message: string): void {
  queuedMessages.push(message);
  scheduleQueueProcessing();
}

function consume(message: string): void {
  try {
    const payload = parseMetrics(message);
    latestTelemetry = payload.telemetry;
    addDecisionSample(payload.telemetry);
    addMetrics(payload.metrics);
  } catch (error) {
    rejected += 1;
    markSnapshotDirty();
    console.error("Rejected malformed message:", error);
  }
}

function handleIngestSocket(socket: WebSocket): void {
  ingestClients.add(socket);
  connected = true;
  status = "Live";
  markSnapshotDirty();
  broadcast();

  socket.on("message", (data, isBinary) => {
    if (isBinary) {
      rejected += 1;
      markSnapshotDirty();
      return;
    }

    enqueueMessage(data.toString());
  });

  socket.on("close", () => {
    ingestClients.delete(socket);
    connected = ingestClients.size > 0;
    status = connected ? "Live" : "Waiting for telemetry WebSocket";
    markSnapshotDirty();
    broadcast();
  });

  socket.on("error", (error) => {
    console.error("Telemetry WebSocket error:", error);
    socket.close();
  });
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

const ingestServer = new WebSocketServer({
  noServer: true,
  maxPayload: 64 * 1024,
  perMessageDeflate: false,
});

server.on("upgrade", (request, socket, head) => {
  const url = new URL(request.url ?? "/", `http://${request.headers.host ?? "localhost"}`);

  if (url.pathname !== ingestPath) {
    socket.write("HTTP/1.1 404 Not Found\r\n\r\n");
    socket.destroy();
    return;
  }

  ingestServer.handleUpgrade(request, socket, head, (webSocket) => {
    ingestServer.emit("connection", webSocket, request);
  });
});

ingestServer.on("connection", handleIngestSocket);

server.listen(port, host, () => {
  console.log(`Dashboard listening on http://${host}:${port}`);
  console.log(`Telemetry ingest listening on ws://${host}:${port}${ingestPath}`);
});

const refreshTimer = setInterval(broadcast, refreshMs);

function handleSignal(): void {
  clearInterval(refreshTimer);
  for (const client of clients) client.end();
  for (const client of ingestClients) client.close();
  ingestServer.close();
  server.close(() => process.exit(0));
}

process.once("SIGINT", handleSignal);
process.once("SIGTERM", handleSignal);
