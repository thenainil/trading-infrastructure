import { useEffect, useRef, useState } from "react";

const HELLO_ENDPOINT = "http://localhost:8000/hello";
const POLL_INTERVAL_MS = 10_000;

type FetchState = "idle" | "loading" | "ok" | "error";

function formatTime(date: Date | null) {
  if (!date) {
    return "Never";
  }

  return date.toLocaleTimeString([], {
    hour: "2-digit",
    minute: "2-digit",
    second: "2-digit"
  });
}

export default function App() {
  const [message, setMessage] = useState("Waiting for first response");
  const [status, setStatus] = useState<FetchState>("idle");
  const [lastUpdated, setLastUpdated] = useState<Date | null>(null);
  const [requestCount, setRequestCount] = useState(0);
  const [error, setError] = useState("");
  const inFlight = useRef(false);

  async function fetchHello() {
    if (inFlight.current) {
      return;
    }

    inFlight.current = true;
    setStatus("loading");
    setError("");

    try {
      const response = await fetch(HELLO_ENDPOINT);
      const body = await response.text();

      if (!response.ok) {
        throw new Error(`${response.status} ${body.trim() || response.statusText}`);
      }

      setMessage(body.trim());
      setLastUpdated(new Date());
      setRequestCount((count) => count + 1);
      setStatus("ok");
    } catch (err) {
      setStatus("error");
      setError(err instanceof Error ? err.message : "Request failed");
    } finally {
      inFlight.current = false;
    }
  }

  useEffect(() => {
    fetchHello();
    const interval = window.setInterval(fetchHello, POLL_INTERVAL_MS);
    return () => window.clearInterval(interval);
  }, []);

  return (
    <main className="shell">
      <section className="status-panel">
        <div className="status-line">
          <span className={`status-dot status-dot-${status}`} />
          <span>{status === "error" ? "Python service unavailable" : "Python service polling"}</span>
        </div>

        <h1>{message}</h1>

        <div className="metrics-grid">
          <div>
            <span className="label">Endpoint</span>
            <strong>{HELLO_ENDPOINT}</strong>
          </div>
          <div>
            <span className="label">Refresh</span>
            <strong>10 seconds</strong>
          </div>
          <div>
            <span className="label">Last response</span>
            <strong>{formatTime(lastUpdated)}</strong>
          </div>
          <div>
            <span className="label">Requests</span>
            <strong>{requestCount}</strong>
          </div>
        </div>

        {error && <p className="error">{error}</p>}
      </section>
    </main>
  );
}
