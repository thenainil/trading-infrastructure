import { StrictMode, useEffect, useState } from "react";
import { createRoot } from "react-dom/client";
import { App, type DashboardSnapshot } from "./App";
import "./styles.css";

function Root() {
  const [snapshot, setSnapshot] = useState<DashboardSnapshot | null>(null);
  const [eventStreamOpen, setEventStreamOpen] = useState(false);

  useEffect(() => {
    const events = new EventSource("/events");

    events.onopen = () => setEventStreamOpen(true);
    events.onerror = () => setEventStreamOpen(false);
    events.onmessage = (event) => {
      setSnapshot(JSON.parse(event.data) as DashboardSnapshot);
    };

    return () => events.close();
  }, []);

  return <App snapshot={snapshot} eventStreamOpen={eventStreamOpen} />;
}

createRoot(document.getElementById("root")!).render(
  <StrictMode>
    <Root />
  </StrictMode>,
);
