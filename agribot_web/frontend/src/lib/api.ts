/**
 * api.ts – REST client for the AgriBot FastAPI backend.
 *
 * In development the Vite proxy forwards /api/* to localhost:8080.
 * In production the frontend is served from the same origin.
 */

const BASE = import.meta.env.VITE_API_BASE_URL || '';

// ---------------------------------------------------------------------------
//  Types
// ---------------------------------------------------------------------------

export interface LogEntry {
  id: number;
  timestamp: string;
  type: 'periodic' | 'manual';
  temperature: number | null;
  humidity: number | null;
  soil_raw: number | null;
  soil_percent: number | null;
  ph_level?: number | null;
  condition?: string | null;
  chance_of_rain?: number | null;
  will_it_rain?: number | null;
  uv_index?: number | null;
  wind_kph?: number | null;
  aqi_us_epa?: number | null;
  aqi_co?: number | null;
  aqi_no2?: number | null;
  aqi_o3?: number | null;
  aqi_so2?: number | null;
  aqi_pm25?: number | null;
  aqi_pm10?: number | null;
}

export interface MoistureReading {
  raw: number;
  percent: number;
  ph: number;
}

// ---------------------------------------------------------------------------
//  Endpoints
// ---------------------------------------------------------------------------

export async function fetchLogs(
  limit = 100,
  offset = 0,
  type?: 'periodic' | 'manual',
): Promise<LogEntry[]> {
  const params = new URLSearchParams({
    limit: String(limit),
    offset: String(offset),
  });
  if (type) params.set('type', type);

  const res = await fetch(`${BASE}/api/logs?${params}`);
  if (!res.ok) throw new Error(`GET /api/logs failed: ${res.status}`);
  return res.json();
}

export async function fetchLatestLog(
  type?: 'periodic' | 'manual',
): Promise<LogEntry | null> {
  const params = type ? `?type=${type}` : '';
  const res = await fetch(`${BASE}/api/logs/latest${params}`);
  if (res.status === 404) return null;
  if (!res.ok) throw new Error(`GET /api/logs/latest failed: ${res.status}`);
  return res.json();
}

export async function postMoistureReading(
  reading: MoistureReading,
): Promise<LogEntry> {
  const res = await fetch(`${BASE}/api/logs/moisture`, {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify(reading),
  });
  if (!res.ok) throw new Error(`POST /api/logs/moisture failed: ${res.status}`);
  return res.json();
}
