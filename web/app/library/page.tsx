"use client";

import Link from "next/link";
import { useCallback, useEffect, useState } from "react";
import { api, loadHost, type Piece } from "@/lib/api";

export default function LibraryPage() {
  const [pieces, setPieces] = useState<Piece[] | null>(null);
  const [status, setStatus] = useState("");
  const [error, setError] = useState(false);
  const [busyId, setBusyId] = useState("");

  const refresh = useCallback(async () => {
    const data = await api<{ pieces: Piece[] }>("/api/library");
    setPieces(data.pieces);
  }, []);

  useEffect(() => {
    refresh().catch((err: Error) => {
      setError(true);
      setStatus(err.message);
      setPieces([]);
    });
  }, [refresh]);

  async function onShow(piece: Piece) {
    const host = await loadHost();
    if (!host) {
      setError(true);
      setStatus("Set the panel address in Settings.");
      return;
    }
    setBusyId(piece.id);
    setError(false);
    setStatus(`Sending ${piece.title}. The glass takes about half a minute.`);
    try {
      await api("/api/show", {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify({ id: piece.id, host }),
      });
      setStatus(`${piece.title} is on the glass.`);
    } catch (err) {
      setError(true);
      setStatus(err instanceof Error ? err.message : "The panel did not update.");
    } finally {
      setBusyId("");
    }
  }

  async function onRemove(piece: Piece) {
    setBusyId(piece.id);
    setError(false);
    setStatus("");
    try {
      await api(`/api/library/${piece.id}`, { method: "DELETE" });
      await refresh();
      setStatus(`Removed ${piece.title}.`);
    } catch (err) {
      setError(true);
      setStatus(err instanceof Error ? err.message : "Could not remove that piece.");
    } finally {
      setBusyId("");
    }
  }

  return (
    <section>
      <h1 className="text-3xl font-medium tracking-tight">Select art</h1>
      <p className="mt-2 text-muted">Show a saved piece, or remove it from the library.</p>

      {pieces && pieces.length === 0 && (
        <p className="mt-8 text-muted">
          The library is empty.{" "}
          <Link href="/upload" className="text-ink underline">
            Upload art
          </Link>
          .
        </p>
      )}

      <ul className="mt-6 grid grid-cols-2 gap-4 lg:grid-cols-3">
        {pieces?.map((piece) => (
          <li key={piece.id} className="border border-line bg-card">
            <img
              src={`/api/library/${piece.id}/preview`}
              alt=""
              className="aspect-[3/4] w-full object-cover"
            />
            <div className="p-3">
              <p className="truncate">{piece.title}</p>
              <p className="text-sm text-muted">{piece.kind}</p>
              <div className="mt-3 flex gap-2">
                <button
                  type="button"
                  onClick={() => onShow(piece)}
                  disabled={busyId !== ""}
                  className="bg-ink px-3 py-1.5 text-sm text-paper"
                >
                  Show
                </button>
                <button
                  type="button"
                  onClick={() => onRemove(piece)}
                  disabled={busyId !== ""}
                  className="border border-line px-3 py-1.5 text-sm"
                >
                  Remove
                </button>
              </div>
            </div>
          </li>
        ))}
      </ul>
      <p className={`mt-4 min-h-6 ${error ? "text-accent" : "text-muted"}`}>{status}</p>
    </section>
  );
}
