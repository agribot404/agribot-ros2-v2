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
    soil_percent  REAL
);
"""

INSERT_SQL = """
INSERT INTO sensor_logs (timestamp, type, temperature, humidity)
VALUES (?, 'periodic', ?, ?);
"""


class SensorLogger(Node):
    """Subscribes to DHT11 topics and logs paired readings to SQLite."""

    def __init__(self):
        super().__init__('sensor_logger')

        # --- Ensure DB directory and table exist ---
        os.makedirs(DB_DIR, exist_ok=True)
        self._conn = sqlite3.connect(DB_PATH, check_same_thread=False)
        self._conn.execute(CREATE_TABLE_SQL)
        self._conn.commit()
        self.get_logger().info(f'SQLite database ready at {DB_PATH}')

        # --- Staging area for paired readings ---
        self._pending_temp: float | None = None
        self._pending_hum: float | None = None

        # --- Subscriptions ---
        self.create_subscription(
            Temperature,
            'sensor/dht11_temperature',
            self._on_temperature,
            10,
        )
        self.create_subscription(
            RelativeHumidity,
            'sensor/dht11_humidity',
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

    def _try_log(self):
        """Write a row only when both values have arrived."""
        if self._pending_temp is None or self._pending_hum is None:
            return

        now = datetime.now(timezone.utc).isoformat()
        self._conn.execute(INSERT_SQL, (now, self._pending_temp, self._pending_hum))
        self._conn.commit()

        self.get_logger().info(
            f'Logged: T={self._pending_temp:.1f}°C  H={self._pending_hum:.1f}%'
        )

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
