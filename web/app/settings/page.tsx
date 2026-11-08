"use client";

import { useEffect, useState } from "react";
import { api } from "@/lib/api";

type Health = { ip?: string; busy?: boolean };
type Wifi = { ssid?: string; ip?: string; switching?: boolean; state?: string };

export default function SettingsPage() {
  const [host, setHost] = useState("");
  const [health, setHealth] = useState<string>("");
  const [wifi, setWifi] = useState<Wifi | null>(null);
  const [ssid, setSsid] = useState("");
  const [password, setPassword] = useState("");
  const [status, setStatus] = useState("");
  const [error, setError] = useState(false);
  const [busy, setBusy] = useState(false);

  async function refresh(address: string) {
    if (!address) {
      setHealth("No panel address yet.");
      setWifi(null);
      return;
    }
    try {
      const device = await api<Health>(`/api/device?host=${encodeURIComponent(address)}`);
      setHealth(`${device.ip || address} · ${device.busy ? "refreshing" : "idle"}`);
    } catch {
      setHealth("Panel not reachable.");
    }
    try {
      setWifi(await api<Wifi>(`/api/wifi?host=${encodeURIComponent(address)}`));
    } catch {
      setWifi(null);
    }
  }

  useEffect(() => {
    api<{ host: string }>("/api/settings")
      .then((data) => {
        setHost(data.host || "");
        return refresh(data.host || "");
      })
      .catch((err: Error) => {
        setError(true);
        setStatus(err.message);
      });
  }, []);

  async function onSaveAddress() {
    const address = host.trim();
    setBusy(true);
    setError(false);
    setStatus("");
    try {
      await api(`/api/device?host=${encodeURIComponent(address)}`);
      await refresh(address);
      setStatus("Address saved.");
    } catch (err) {
      const message = err instanceof Error ? err.message : "Could not save that address.";
      if (message.includes("unreachable")) {
        setHealth("Panel not reachable.");
        setStatus("Address saved. The panel is not reachable from here.");
      } else {
        setError(true);
        setStatus(message);
      }
    } finally {
      setBusy(false);
    }
  }

  async function onSwitch() {
    if (!host.trim() || !ssid.trim()) {
      setError(true);
      setStatus(host.trim() ? "Enter a network name." : "Enter the panel address.");
      return;
    }
    setBusy(true);
    setError(false);
    setStatus("Asking the panel to switch.");
    try {
      await api("/api/wifi", {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify({ host: host.trim(), ssid: ssid.trim(), password }),
      });
      setPassword("");
      setStatus(`Switch started. Join ${ssid.trim()} on this computer to reach the panel again.`);
    } catch (err) {
      setError(true);
      setStatus(err instanceof Error ? err.message : "Could not switch networks.");
    } finally {
      setBusy(false);
    }
  }

  const network = wifi
    ? `On ${wifi.ssid || "no network"} · ${wifi.ip || ""}${wifi.switching ? " · switching" : ""}`
    : "";

  return (
    <section className="max-w-xl">
      <h1 className="text-3xl font-medium tracking-tight">Settings</h1>
      <p className="mt-2 text-muted">The panel address, and the network it should join.</p>

      <label className="mt-8 block text-xs tracking-wide text-muted uppercase">
        Panel address
        <input
          value={host}
          onChange={(event) => setHost(event.target.value)}
          placeholder="10.0.0.194"
          className="mt-2 w-full border border-line bg-card px-3 py-2 text-base text-ink normal-case"
        />
      </label>
      <p className="mt-2 text-sm text-muted">{health}</p>
      <button type="button" onClick={onSaveAddress} disabled={busy} className="mt-3 bg-ink px-4 py-2 text-paper">
        Save address
      </button>

      <div className="mt-10 border-t border-line pt-6">
        <h2 className="text-lg">Wi-Fi</h2>
        {network && <p className="mt-2 text-sm text-muted">{network}</p>}
        <label className="mt-4 block text-xs tracking-wide text-muted uppercase">
          Network name
          <input
            value={ssid}
            onChange={(event) => setSsid(event.target.value)}
            className="mt-2 w-full border border-line bg-card px-3 py-2 text-base text-ink normal-case"
          />
        </label>
        <label className="mt-4 block text-xs tracking-wide text-muted uppercase">
          Password
          <input
            type="password"
            value={password}
            onChange={(event) => setPassword(event.target.value)}
            autoComplete="off"
            className="mt-2 w-full border border-line bg-card px-3 py-2 text-base text-ink normal-case"
          />
        </label>
        <button type="button" onClick={onSwitch} disabled={busy} className="mt-4 bg-ink px-4 py-2 text-paper">
          Switch
        </button>
        <p className="mt-3 text-sm text-muted">
          This computer loses the panel until it joins that network too. The password is sent to the board and is not saved here.
        </p>
      </div>

      <p className={`mt-4 min-h-6 ${error ? "text-accent" : "text-muted"}`}>{status}</p>
    </section>
  );
}
