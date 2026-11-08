"use client";

import Link from "next/link";
import { useCallback, useEffect, useState } from "react";
import { api, loadHost, type Now, type Piece } from "@/lib/api";

export default function NowPage() {
  const [now, setNow] = useState<Now | null>(null);
  const [pieces, setPieces] = useState<Piece[]>([]);
  const [status, setStatus] = useState("");
  const [error, setError] = useState(false);
  const [busy, setBusy] = useState(false);
  const [cleared, setCleared] = useState(false);

  const refresh = useCallback(async () => {
    const [current, library] = await Promise.all([
      api<Now>("/api/now"),
      api<{ pieces: Piece[] }>("/api/library"),
    ]);
    setNow(current);
    setPieces(library.pieces);
  }, []);

  useEffect(() => {
    refresh().catch((err: Error) => {
      setError(true);
      setStatus(err.message);
    });
  }, [refresh]);

  async function run(path: string, body: unknown, waiting: string, done: string) {
    setBusy(true);
    setError(false);
    setStatus(waiting);
    try {
      await api(path, {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify(body),
      });
      await refresh();
      setCleared(path === "/api/clear");
      setStatus(done);
    } catch (err) {
      setError(true);
      setStatus(err instanceof Error ? err.message : "The panel did not update.");
    } finally {
      setBusy(false);
    }
  }

  async function onNext() {
    const host = now?.host || (await loadHost());
    if (!host) {
      setError(true);
      setStatus("Set the panel address in Settings.");
      return;
    }
    await run("/api/next", { host }, "Sending the next piece. The glass takes about half a minute.", "On the glass.");
  }

  async function onClear() {
    const host = now?.host || (await loadHost());
    if (!host) {
      setError(true);
      setStatus("Set the panel address in Settings.");
      return;
    }
    await run("/api/clear", { host }, "Clearing the glass to white.", "Panel is white.");
  }

  const panelLine = !now?.host
    ? "No panel address yet."
    : now.reachable
      ? `${now.host} · ${now.busy ? "refreshing" : "idle"}`
      : `${now.host} · not reachable`;

  return (
    <section>
      <h1 className="text-3xl font-medium tracking-tight">Currently showing</h1>
      <p className="mt-2 text-muted">{panelLine}</p>

      {now?.id ? (
        <figure className="mt-6 max-w-[420px]">
          <img
            src={`/api/library/${now.id}/preview`}
            alt={now.title || "Current piece"}
            className="aspect-[3/4] w-full border border-line bg-white object-cover"
          />
          <figcaption className="mt-3 text-lg">
            {cleared ? `${now.title} was last sent. The glass is white.` : now.title}
          </figcaption>
        </figure>
      ) : (
        <p className="mt-8 max-w-md text-muted">
          Nothing has been sent yet.{" "}
          <Link href="/upload" className="text-ink underline">
            Upload art
          </Link>{" "}
          to put the first piece on the glass.
        </p>
      )}

      <div className="mt-6 flex flex-wrap gap-2">
        <button
          type="button"
          onClick={onNext}
          disabled={busy || pieces.length === 0}
          className="bg-ink px-4 py-2 text-paper"
        >
          Next
        </button>
        <button
          type="button"
          onClick={onClear}
          disabled={busy}
          className="border border-line bg-card px-4 py-2"
        >
          Clear
        </button>
      </div>
      <p className={`mt-4 min-h-6 ${error ? "text-accent" : "text-muted"}`}>{status}</p>
    </section>
  );
}
