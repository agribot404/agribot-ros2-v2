"""
web_server.py – FastAPI server that serves the Vite frontend and exposes a
REST API for historical sensor logs stored in SQLite.

Endpoints:
    GET  /api/logs          – paginated log query  (?limit=100&offset=0&type=periodic|manual)
    GET  /api/logs/latest   – single most-recent log entry
    POST /api/logs/moisture – store a manual soil-moisture reading
    GET  /*                 – Vite production build (static files + SPA fallback)

Run standalone for development:
    python -m agribot_web.web_server
"""

import os
import sqlite3
from contextlib import contextmanager
from datetime import datetime, timezone
from pathlib import Path
from typing import Optional

import uvicorn
from fastapi import FastAPI, Query
from fastapi.middleware.cors import CORSMiddleware
from fastapi.responses import FileResponse, JSONResponse
from fastapi.staticfiles import StaticFiles
from pydantic import BaseModel


# ---------------------------------------------------------------------------
#  Paths
# ---------------------------------------------------------------------------

DB_DIR = os.path.expanduser('~/.agribot')
DB_PATH = os.path.join(DB_DIR, 'history.db')

# The Vite production build is placed here by `npm run build`
_PACKAGE_DIR = Path(__file__).resolve().parent
FRONTEND_DIST = _PACKAGE_DIR.parent / 'frontend' / 'dist'

# ---------------------------------------------------------------------------
#  DB helpers
# ---------------------------------------------------------------------------

CREATE_TABLE_SQL = """
CREATE TABLE IF NOT EXISTS sensor_logs (
    id            INTEGER PRIMARY KEY AUTOINCREMENT,
    timestamp     TEXT    NOT NULL,
    type          TEXT    NOT NULL DEFAULT 'periodic',
    temperature   REAL,
    humidity      REAL,
    soil_raw      INTEGER,
    soil_percent  REAL,
    ph_level      REAL,
    condition     TEXT,
    chance_of_rain REAL,
    will_it_rain  INTEGER,
    uv_index      REAL,
    wind_kph      REAL,
    aqi_us_epa    INTEGER,
    aqi_co        REAL,
    aqi_no2       REAL,
    aqi_o3        REAL,
    aqi_so2       REAL,
    aqi_pm25      REAL,
    aqi_pm10      REAL
);
"""

def _ensure_db():
    """Create the database directory and table if they don't exist yet."""
    os.makedirs(DB_DIR, exist_ok=True)
    conn = sqlite3.connect(DB_PATH)
    # Add new columns if missing
    try:
        conn.execute(CREATE_TABLE_SQL)
        # Attempt to alter table if it already exists (SQLite alter table is limited, simple approach)
        columns_to_add = [
            ("ph_level", "REAL"), ("condition", "TEXT"), ("chance_of_rain", "REAL"),
            ("will_it_rain", "INTEGER"), ("uv_index", "REAL"), ("wind_kph", "REAL"),
            ("aqi_us_epa", "INTEGER"), ("aqi_co", "REAL"), ("aqi_no2", "REAL"),
            ("aqi_o3", "REAL"), ("aqi_so2", "REAL"), ("aqi_pm25", "REAL"), ("aqi_pm10", "REAL")
        ]
        for col, col_type in columns_to_add:
            try:
                conn.execute(f"ALTER TABLE sensor_logs ADD COLUMN {col} {col_type}")
            except sqlite3.OperationalError:
                pass
    except sqlite3.Error as e:
        print(e)
    conn.commit()
    conn.close()


@contextmanager
def _get_db():
    """Yield a sqlite3 connection with row_factory set to Row."""
    conn = sqlite3.connect(DB_PATH)
    conn.row_factory = sqlite3.Row
    try:
        yield conn
    finally:
        conn.close()


# ---------------------------------------------------------------------------
#  Pydantic models
# ---------------------------------------------------------------------------

class MoistureReading(BaseModel):
    raw: int
    percent: float
    ph: Optional[float] = None
    condition: Optional[str] = None
    chance_of_rain: Optional[float] = None
    will_it_rain: Optional[int] = None
    uv_index: Optional[float] = None
    wind_kph: Optional[float] = None
    aqi_us_epa: Optional[int] = None
    aqi_co: Optional[float] = None
    aqi_no2: Optional[float] = None
    aqi_o3: Optional[float] = None
    aqi_so2: Optional[float] = None
    aqi_pm25: Optional[float] = None
    aqi_pm10: Optional[float] = None


class LogEntry(BaseModel):
    id: int
    timestamp: str
    type: str
    temperature: Optional[float] = None
    humidity: Optional[float] = None
    soil_raw: Optional[int] = None
    soil_percent: Optional[float] = None
    ph_level: Optional[float] = None
    condition: Optional[str] = None
    chance_of_rain: Optional[float] = None
    will_it_rain: Optional[int] = None
    uv_index: Optional[float] = None
    wind_kph: Optional[float] = None
    aqi_us_epa: Optional[int] = None
    aqi_co: Optional[float] = None
    aqi_no2: Optional[float] = None
    aqi_o3: Optional[float] = None
    aqi_so2: Optional[float] = None
    aqi_pm25: Optional[float] = None
    aqi_pm10: Optional[float] = None


# ---------------------------------------------------------------------------
#  FastAPI app
# ---------------------------------------------------------------------------

app = FastAPI(title='AgriBot Web API', version='1.0.0')

# Allow the Vite dev server (port 5173) during development
app.add_middleware(
    CORSMiddleware,
    allow_origins=['*'],
    allow_methods=['*'],
    allow_headers=['*'],
)


@app.on_event('startup')
def on_startup():
    _ensure_db()


# ---------------------------------------------------------------------------
#  API routes
# ---------------------------------------------------------------------------

@app.get('/api/logs', response_model=list[LogEntry])
def get_logs(
    limit: int = Query(100, ge=1, le=1000),
    offset: int = Query(0, ge=0),
    type: Optional[str] = Query(None, pattern='^(periodic|manual)$'),
):
    """Return paginated sensor log entries, newest first."""
    with _get_db() as conn:
        if type:
            rows = conn.execute(
                'SELECT * FROM sensor_logs WHERE type = ? ORDER BY id DESC LIMIT ? OFFSET ?',
                (type, limit, offset),
            ).fetchall()
        else:
            rows = conn.execute(
                'SELECT * FROM sensor_logs ORDER BY id DESC LIMIT ? OFFSET ?',
                (limit, offset),
            ).fetchall()
    return [dict(r) for r in rows]


@app.get('/api/logs/latest')
def get_latest_log(
    type: Optional[str] = Query(None, pattern='^(periodic|manual)$'),
):
    """Return the single most-recent log entry."""
    with _get_db() as conn:
        if type:
            row = conn.execute(
                'SELECT * FROM sensor_logs WHERE type = ? ORDER BY id DESC LIMIT 1',
                (type,),
            ).fetchone()
        else:
            row = conn.execute(
                'SELECT * FROM sensor_logs ORDER BY id DESC LIMIT 1',
            ).fetchone()
    if row is None:
        return JSONResponse(content={'detail': 'No entries found'}, status_code=404)
    return dict(row)


@app.post('/api/logs/moisture', response_model=LogEntry)
def post_moisture(reading: MoistureReading):
    """Store a manual soil-moisture reading (triggered from the Control page)."""
    now = datetime.now(timezone.utc).isoformat()
    with _get_db() as conn:
        cursor = conn.execute(
            '''INSERT INTO sensor_logs (
                timestamp, type, soil_raw, soil_percent, ph_level,
                condition, chance_of_rain, will_it_rain, uv_index, wind_kph,
                aqi_us_epa, aqi_co, aqi_no2, aqi_o3, aqi_so2, aqi_pm25, aqi_pm10
            ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)''',
            (now, 'manual', reading.raw, reading.percent, reading.ph,
             reading.condition, reading.chance_of_rain, reading.will_it_rain,
             reading.uv_index, reading.wind_kph, reading.aqi_us_epa,
             reading.aqi_co, reading.aqi_no2, reading.aqi_o3, reading.aqi_so2,
             reading.aqi_pm25, reading.aqi_pm10),
        )
        conn.commit()
        row = conn.execute(
            'SELECT * FROM sensor_logs WHERE id = ?', (cursor.lastrowid,)
        ).fetchone()
    return dict(row)


# ---------------------------------------------------------------------------
#  Static file serving  (Vite production build)
# ---------------------------------------------------------------------------

# Mount static assets if the dist directory exists (after `npm run build`)
if FRONTEND_DIST.is_dir():
    # Serve /assets/* directly
    assets_dir = FRONTEND_DIST / 'assets'
    if assets_dir.is_dir():
        app.mount('/assets', StaticFiles(directory=str(assets_dir)), name='assets')

    # SPA fallback: any non-API route returns index.html
    @app.get('/{full_path:path}')
    def serve_spa(full_path: str):
        # Try to serve an exact file first (favicon.ico, robots.txt, etc.)
        file_path = FRONTEND_DIST / full_path
        if full_path and file_path.is_file():
            return FileResponse(str(file_path))
        return FileResponse(str(FRONTEND_DIST / 'index.html'))
else:
    @app.get('/')
    def no_frontend():
        return {
            'status': 'API is running',
            'note': 'Frontend not built. Run: cd frontend && npm run build',
        }


# ---------------------------------------------------------------------------
#  Entry point
# ---------------------------------------------------------------------------

def main():
    """Entry point used by the ROS 2 console_scripts entry or direct invocation."""
    _ensure_db()
    uvicorn.run(
        'agribot_web.web_server:app',
        host='0.0.0.0',
        port=8080,
        log_level='info',
    )


if __name__ == '__main__':
    main()
