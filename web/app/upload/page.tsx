"use client";

import { useRef, useState } from "react";
import { useRouter } from "next/navigation";
import { Cropper, type CropperHandle } from "@/components/Cropper";
import { api, loadHost, type Piece } from "@/lib/api";

type Source = {
  id: string;
  kind: string;
  title: string;
  width: number;
  height: number;
};

export default function UploadPage() {
  const router = useRouter();
  const fileRef = useRef<HTMLInputElement>(null);
  const cropper = useRef<CropperHandle>(null);
  const [url, setUrl] = useState("");
  const [source, setSource] = useState<Source | null>(null);
  const [title, setTitle] = useState("");
  const [preview, setPreview] = useState("");
  const [status, setStatus] = useState("");
  const [error, setError] = useState(false);
  const [busy, setBusy] = useState(false);

  async function readError(response: Response) {
    const data = await response.json().catch(() => null);
    return (data && data.error) || response.statusText;
  }

  async function loadSource(body: BodyInit, contentType?: string) {
    setBusy(true);
    setError(false);
    setStatus("Loading…");
    setPreview("");
    try {
      const headers = contentType ? { "Content-Type": contentType } : undefined;
      const response = await fetch("/api/sources", { method: "POST", headers, body });
      if (!response.ok) throw new Error(await readError(response));
      const data = (await response.json()) as Source;
      setSource(data);
      setTitle(data.title || "");
      setStatus("Loaded. Drag to pan. Scroll to zoom.");
    } catch (err) {
      setError(true);
      setStatus(err instanceof Error ? err.message : "Could not load that.");
    } finally {
      setBusy(false);
    }
  }

  function onLoad() {
    const file = fileRef.current?.files?.[0];
    if (file) {
      const form = new FormData();
      form.append("file", file);
      loadSource(form);
      return;
    }
    if (url.trim()) {
      loadSource(JSON.stringify({ url: url.trim() }), "application/json");
      return;
    }
    setError(true);
    setStatus("Choose a file or a URL.");
  }

  async function onPreview() {
    if (!source || !cropper.current) return;
    setBusy(true);
    setError(false);
    setStatus("Dithering…");
    try {
      const response = await fetch("/api/preview", {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify({ id: source.id, crop: cropper.current.crop() }),
      });
      if (!response.ok) throw new Error(await readError(response));
      const blob = await response.blob();
      setPreview((current) => {
        if (current) URL.revokeObjectURL(current);
        return URL.createObjectURL(blob);
      });
      setStatus("Preview ready.");
    } catch (err) {
      setError(true);
      setStatus(err instanceof Error ? err.message : "Preview failed.");
    } finally {
      setBusy(false);
    }
  }

  async function onShow() {
    if (!source || !cropper.current) return;
    const host = await loadHost();
    if (!host) {
      setError(true);
      setStatus("Set the panel address in Settings.");
      return;
    }
    setBusy(true);
    setError(false);
    setStatus("Saving, then the glass takes about half a minute.");
    try {
      const piece = await api<Piece>("/api/library", {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify({ id: source.id, crop: cropper.current.crop(), title }),
      });
      try {
        await api("/api/show", {
          method: "POST",
          headers: { "Content-Type": "application/json" },
          body: JSON.stringify({ id: piece.id, host }),
        });
      } catch (err) {
        const message = err instanceof Error ? err.message : "the panel did not update";
        setError(true);
        setStatus(`Saved “${piece.title}”, but ${message}.`);
        return;
      }
      router.push("/");
    } catch (err) {
      setError(true);
      setStatus(err instanceof Error ? err.message : "Could not show that piece.");
    } finally {
      setBusy(false);
    }
  }

  return (
    <section>
      <h1 className="text-3xl font-medium tracking-tight">Upload art</h1>
      <p className="mt-2 text-muted">A file, or a URL that is either an image or a page.</p>

      <div className="mt-6 grid gap-4 md:grid-cols-[1fr_1fr_auto] md:items-end">
        <label className="text-xs tracking-wide text-muted uppercase">
          File
          <input ref={fileRef} type="file" accept="image/*" className="mt-2 block w-full text-sm text-ink normal-case" />
        </label>
        <label className="text-xs tracking-wide text-muted uppercase">
          URL
          <input
            value={url}
            onChange={(event) => setUrl(event.target.value)}
            placeholder="https://example.com or an image link"
            className="mt-2 w-full border border-line bg-card px-3 py-2 text-base text-ink normal-case"
          />
        </label>
        <button type="button" onClick={onLoad} disabled={busy} className="bg-ink px-4 py-2 text-paper">
          Load
        </button>
      </div>

      {source && (
        <>
          <div className="mt-8 grid gap-8 md:grid-cols-2">
            <div>
              <Cropper ref={cropper} src={`/api/sources/${source.id}`} />
              <p className="mt-2 text-sm text-muted">
                {source.kind} · {source.width}×{source.height}. Drag to pan. Scroll to zoom.
              </p>
            </div>
            <div>
              {preview ? (
                <img src={preview} alt="Six-color preview" className="aspect-[3/4] w-full max-w-[420px] border border-line bg-white object-contain" />
              ) : (
                <div className="flex aspect-[3/4] w-full max-w-[420px] items-center justify-center border border-dashed border-line text-sm text-muted">
                  Preview appears here
                </div>
              )}
              <p className="mt-2 text-sm text-muted">Six-color preview, from the bytes the panel receives.</p>
            </div>
          </div>

          <label className="mt-6 block max-w-md text-xs tracking-wide text-muted uppercase">
            Title
            <input
              value={title}
              onChange={(event) => setTitle(event.target.value)}
              className="mt-2 w-full border border-line bg-card px-3 py-2 text-base text-ink normal-case"
            />
          </label>

          <div className="mt-4 flex gap-2">
            <button type="button" onClick={onPreview} disabled={busy} className="border border-line bg-card px-4 py-2">
              Preview
            </button>
            <button type="button" onClick={onShow} disabled={busy} className="bg-ink px-4 py-2 text-paper">
              Show
            </button>
          </div>
        </>
      )}

      <p className={`mt-4 min-h-6 ${error ? "text-accent" : "text-muted"}`}>{status}</p>
    </section>
  );
}
