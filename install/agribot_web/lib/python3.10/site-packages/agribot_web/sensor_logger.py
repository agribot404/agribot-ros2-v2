"""
sensor_logger.py – ROS 2 node that subscribes to DHT11 topics and logs to SQLite.

Subscriptions:
    /sensor/dht11_temperature  (sensor_msgs/msg/Temperature)
    /sensor/dht11_humidity     (sensor_msgs/msg/RelativeHumidity)

Writes timestamped rows to ~/.agribot/history.db every time *both* a new
temperature and humidity reading have arrived (they publish together every
10 minutes from the ESP32).
"""

import os
import sqlite3
import time
import requests
from datetime import datetime, timezone

import rclpy
from rclpy.node import Node
from sensor_msgs.msg import Temperature, RelativeHumidity


DB_DIR = os.path.expanduser('~/.agribot')
DB_PATH = os.path.join(DB_DIR, 'history.db')

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

INSERT_SQL = """
INSERT INTO sensor_logs (
    timestamp, type, temperature, humidity, condition, chance_of_rain,
    will_it_rain, uv_index, wind_kph, aqi_us_epa, aqi_co, aqi_no2, aqi_o3,
    aqi_so2, aqi_pm25, aqi_pm10
) VALUES (?, 'periodic', ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?);
"""


class SensorLogger(Node):
    """Subscribes to DHT11 topics and logs paired readings to SQLite."""

    def __init__(self):
        super().__init__('sensor_logger')

        # --- Ensure DB directory and table exist ---
        os.makedirs(DB_DIR, exist_ok=True)
        self._conn = sqlite3.connect(DB_PATH, check_same_thread=False)
        try:
            self._conn.execute(CREATE_TABLE_SQL)
        except sqlite3.OperationalError:
            pass
        self._conn.commit()
        self.get_logger().info(f'SQLite database ready at {DB_PATH}')

        # --- Staging area for paired readings ---
        self._pending_temp: float | None = None
        self._pending_hum: float | None = None
        self._last_log_time = 0.0

        # --- Subscriptions ---
        self.create_subscription(
            Temperature,
            '/agribot/sensor/dht11/temperature',
            self._on_temperature,
            10,
        )
        self.create_subscription(
            RelativeHumidity,
            '/agribot/sensor/dht11/humidity',
            self._on_humidity,
            10,
        )

        self.get_logger().info('SensorLogger node started – waiting for DHT11 data')

    # ------------------------------------------------------------------
    #  Callbacks
    # ------------------------------------------------------------------

    def _on_temperature(self, msg: Temperature):
        self._pending_temp = msg.temperature
        self._try_log()

    def _on_humidity(self, msg: RelativeHumidity):
        # micro-ROS publishes 0.0–1.0 per REP-145; store as 0–100 %.
        self._pending_hum = msg.relative_humidity * 100.0
        self._try_log()

    def fetch_weather_api(self):
        try:
            api_key = "91dc194f911f4e47991162622261902"
            res = requests.get(f"https://api.weatherapi.com/v1/forecast.json?key={api_key}&q=Rajshahi&days=1&aqi=yes&alerts=no")
            res.raise_for_status()
            data = res.json()
            c = data.get('current', {})
            f = data.get('forecast', {}).get('forecastday', [{}])[0].get('day', {})
            aqi = c.get('air_quality', {})
            return {
                'condition': c.get('condition', {}).get('text'),
                'chance_of_rain': f.get('daily_chance_of_rain'),
                'will_it_rain': f.get('daily_will_it_rain'),
                'uv_index': c.get('uv'),
                'wind_kph': c.get('wind_kph'),
                'aqi_us_epa': aqi.get('us-epa-index'),
                'aqi_co': aqi.get('co'),
                'aqi_no2': aqi.get('no2'),
                'aqi_o3': aqi.get('o3'),
                'aqi_so2': aqi.get('so2'),
                'aqi_pm25': aqi.get('pm2_5'),
                'aqi_pm10': aqi.get('pm10')
            }
        except Exception as e:
            self.get_logger().error(f"Weather API fetch failed: {e}")
            return {}

    def _try_log(self):
        """Write a row only when both values have arrived and 60s have passed."""
        if self._pending_temp is None or self._pending_hum is None:
            return

        current_time = time.time()
        if current_time - self._last_log_time < 60.0:
            # Not enough time has passed; just reset and ignore
            self._pending_temp = None
            self._pending_hum = None
            return

        now = datetime.now(timezone.utc).isoformat()
        w = self.fetch_weather_api()

        self._conn.execute(INSERT_SQL, (
            now, self._pending_temp, self._pending_hum,
            w.get('condition'), w.get('chance_of_rain'), w.get('will_it_rain'),
            w.get('uv_index'), w.get('wind_kph'), w.get('aqi_us_epa'),
            w.get('aqi_co'), w.get('aqi_no2'), w.get('aqi_o3'),
            w.get('aqi_so2'), w.get('aqi_pm25'), w.get('aqi_pm10')
        ))
        self._conn.commit()

        self.get_logger().info(
            f'Logged: T={self._pending_temp:.1f}°C  H={self._pending_hum:.1f}%'
        )

        self._last_log_time = current_time
        # Reset staging so we don't double-log
        self._pending_temp = None
        self._pending_hum = None

    # ------------------------------------------------------------------
    #  Cleanup
    # ------------------------------------------------------------------

    def destroy_node(self):
        self._conn.close()
        super().destroy_node()


def main(args=None):
    rclpy.init(args=args)
    node = SensorLogger()
    try:
        rclpy.spin(node)
    except KeyboardInterrupt:
        pass
    finally:
        node.destroy_node()
        rclpy.try_shutdown()


if __name__ == '__main__':
    main()
