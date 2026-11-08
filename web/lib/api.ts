export type Piece = {
  id: string;
  title: string;
  kind: string;
  width: number;
  height: number;
  added: string;
  url: string;
};

export type Crop = {
  x: number;
  y: number;
  width: number;
  height: number;
};

export type Now = {
  id: string | null;
  title: string | null;
  host: string;
  busy: boolean | null;
  reachable: boolean;
};

async function errorMessage(response: Response) {
  const type = response.headers.get("content-type") || "";
  if (type.includes("json")) {
    const data = await response.json().catch(() => null);
    if (data && typeof data.error === "string") return data.error;
  }
  const text = await response.text().catch(() => "");
  return text || response.statusText;
}

export async function api<T>(path: string, init?: RequestInit): Promise<T> {
  const response = await fetch(path, init);
  if (!response.ok) throw new Error(await errorMessage(response));
  const type = response.headers.get("content-type") || "";
  if (type.includes("json")) return response.json() as Promise<T>;
  return response as unknown as T;
}

export function loadHost() {
  return api<{ host: string }>("/api/settings").then((data) => data.host || "");
}