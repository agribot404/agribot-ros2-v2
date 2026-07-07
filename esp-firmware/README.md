# AgriBot ESP32 Firmware

Micro-ROS firmware for the ESP32 WROOM that bridges ROS 2 Humble with field hardware: environmental sensors, a differential-drive motor controller, and a servo actuator. Built with PlatformIO on the Arduino framework and designed for dual-core FreeRTOS execution to keep the Wi-Fi stack stable under real-time hardware control.

## Table of Contents

- [Architecture](#architecture)
- [Hardware Pinout](#hardware-pinout)
- [ROS 2 Interface](#ros-2-interface)
- [Prerequisites](#prerequisites)
- [Configuration](#configuration)
- [Build and Flash](#build-and-flash)
- [Running the micro-ROS Agent](#running-the-micro-ros-agent)
- [Testing](#testing)
- [Boot Sequence](#boot-sequence)
- [Troubleshooting](#troubleshooting)

## Architecture

The ESP32 has two Xtensa LX6 cores. This firmware pins network and hardware workloads to separate cores to prevent Wi-Fi stack crashes caused by timing-critical hardware interrupts.

```
Core 0 (Protocol CPU)               Core 1 (Application CPU)
========================             ========================
Wi-Fi / LwIP stack                   DHT11 bit-bang read
micro-ROS executor                   L298N motor PWM control
  - cmd_vel subscriber callback      Servo PWM control
  - cmd_servo subscriber callback    cmd_vel watchdog (500 ms)
  - DHT publish timer callback
  - Soil moisture service callback
         |                                    |
         +-------- SharedState (spinlock) ----+
```

Data flows between cores through a `SharedState` struct protected by a FreeRTOS `portMUX` spinlock. No heap allocation occurs at runtime.

### Class Overview

| Class | File | Responsibility |
|---|---|---|
| `DHTSensor` | `src/main.cpp:138` | Non-blocking 2-second periodic temperature/humidity read |
| `SoilMoistureSensor` | `src/main.cpp:179` | On-demand ADC1 read (GPIO 34) |
| `L298NMotorDriver` | `src/main.cpp:206` | Single H-bridge channel: direction + LEDC PWM |
| `ServoController` | `src/main.cpp:264` | 50 Hz hardware PWM, 0-180 degree range |
| `ReservedOutputs` | `src/main.cpp:295` | Initialises reserved GPIOs to OUTPUT LOW |
| `MicroROSNode` | `src/main.cpp:314` | All RCL/RCLC handles, publishers, subscribers, service, timer, executor |

## Hardware Pinout

### Sensors

| Peripheral | GPIO | Mode | Notes |
|---|---|---|---|
| DHT11 (data) | 4 | Digital input | 2-second read interval, runs on Core 1 |
| Soil moisture | 34 | ADC1 (analog) | Read only on service request. ADC1 is Wi-Fi-safe |

### L298N Motor Driver

| Signal | GPIO | Motor |
|---|---|---|
| ENA (PWM) | 14 | Motor A (left) |
| IN1 | 27 | Motor A direction |
| IN2 | 26 | Motor A direction |
| ENB (PWM) | 32 | Motor B (right) |
| IN3 | 25 | Motor B direction |
| IN4 | 33 | Motor B direction |

Motor PWM: 1 kHz, 8-bit resolution (duty 0-255).

### Servo

| Signal | GPIO | Notes |
|---|---|---|
| PWM | 13 | 50 Hz, 16-bit resolution. 1.0-2.0 ms pulse range |

### Reserved Switches

| GPIO | Boot State | ROS 2 Exposure |
|---|---|---|
| 18 | OUTPUT LOW | None |
| 19 | OUTPUT LOW | None |
| 23 | OUTPUT LOW | None |

These pins are configured as digital outputs and driven LOW on boot. They are not exposed to ROS 2.

## ROS 2 Interface

**Node name:** `/agribot_esp32`

### Publishers

| Topic | Type | Rate | Description |
|---|---|---|---|
| `/sensor/dht11_temperature` | `sensor_msgs/msg/Temperature` | 0.5 Hz | Temperature in Celsius. Frame: `dht11_frame` |
| `/sensor/dht11_humidity` | `sensor_msgs/msg/RelativeHumidity` | 0.5 Hz | Humidity as 0.0-1.0 ratio (REP-145) |

### Subscribers

| Topic | Type | Description |
|---|---|---|
| `/cmd_vel` | `geometry_msgs/msg/Twist` | Differential drive input. `linear.x` and `angular.z` mixed to left/right motor speeds |
| `/cmd_servo` | `std_msgs/msg/Int32` | Servo angle in degrees (0-180) |

### Services

| Service | Type | Description |
|---|---|---|
| `/srv/read_moisture` | `std_srvs/srv/Trigger` | Returns `success: true` and a JSON string: `{"raw":<adc>,"percent":<float>}` |

### Motor Watchdog

If no message is received on `/cmd_vel` within **500 ms**, both motors are automatically braked (IN1/IN2 LOW, PWM 0). This prevents runaway behaviour if the controlling node crashes or the network drops.

## Prerequisites

### Development Machine

- [PlatformIO CLI](https://docs.platformio.org/en/latest/core/installation.html) or the VS Code extension
- USB-to-UART driver for your ESP32 board (CP2102 or CH340)

### Raspberry Pi (micro-ROS Agent Host)

- ROS 2 Humble installed
- micro-ROS agent:
  ```bash
  sudo apt install ros-humble-micro-ros-agent
  # or build from source:
  # https://micro.ros.org/docs/tutorials/core/first_application_linux/
  ```
- mDNS responder broadcasting `agripi.local` (Avahi is enabled by default on Raspberry Pi OS)

### Network

- Both the ESP32 and the Raspberry Pi must be on the same Wi-Fi network / subnet
- mDNS (multicast DNS) must not be blocked by the router

## Configuration

Edit the two credential constants at the top of `src/main.cpp` before building:

```cpp
static const char* WIFI_SSID = "YOUR_SSID";       // your Wi-Fi network name
static const char* WIFI_PASS = "YOUR_PASSWORD";    // your Wi-Fi password
```

Other tuneable constants in `src/main.cpp`:

| Constant | Default | Description |
|---|---|---|
| `MDNS_HOSTNAME` | `"esp"` | mDNS name broadcast by the ESP32 (`esp.local`) |
| `AGENT_MDNS_NAME` | `"agripi"` | mDNS name of the micro-ROS agent host |
| `AGENT_PORT` | `8888` | UDP port the agent listens on |
| `DHT_READ_INTERVAL_MS` | `2000` | Sensor poll period (ms) |
| `CMD_VEL_TIMEOUT_MS` | `500` | Motor watchdog timeout (ms) |
| `HARDWARE_LOOP_PERIOD_MS` | `20` | Core 1 control loop period (50 Hz) |

## Build and Flash

```bash
cd esp-firmware

# Build
pio run

# Flash (auto-detects USB port)
pio run -t upload

# Monitor serial output
pio device monitor -b 115200
```

To do both in one step:

```bash
pio run -t upload && pio device monitor -b 115200
```

## Running the micro-ROS Agent

On the Raspberry Pi (or any ROS 2 Humble host on the same network):

```bash
# Source the ROS 2 workspace
source /opt/ros/humble/setup.bash

# Start the agent on UDP port 8888
ros2 run micro_ros_agent micro_ros_agent udp4 --port 8888
```

The ESP32 will resolve `agripi.local` via mDNS, connect to this agent, and register its node, topics, and services.

### Verifying the Connection

```bash
# List discovered nodes
ros2 node list
# Expected output: /agribot_esp32

# List all topics
ros2 topic list
# Expected output includes:
#   /sensor/dht11_temperature
#   /sensor/dht11_humidity
#   /cmd_vel
#   /cmd_servo

# List all services
ros2 service list
# Expected output includes:
#   /srv/read_moisture
```

## Testing

### Monitor DHT11 Sensor Data

```bash
# Temperature (degrees Celsius)
ros2 topic echo /sensor/dht11_temperature

# Relative humidity (0.0 - 1.0)
ros2 topic echo /sensor/dht11_humidity

# Verify publish rate (~0.5 Hz)
ros2 topic hz /sensor/dht11_temperature
```

### Drive Motors with Teleop Twist Keyboard

```bash
# Install if not already present
sudo apt install ros-humble-teleop-twist-keyboard

# Launch (publishes to /cmd_vel by default)
ros2 run teleop_twist_keyboard teleop_twist_keyboard
```

Key bindings (default):

| Key | Action |
|---|---|
| `i` | Forward |
| `,` | Backward |
| `j` | Turn left |
| `l` | Turn right |
| `k` | Stop |

Stop pressing keys and the 500 ms watchdog will automatically brake both motors.

### Publish a Manual Twist Command

```bash
# Drive forward at half speed
ros2 topic pub --once /cmd_vel geometry_msgs/msg/Twist \
  "{linear: {x: 0.5, y: 0.0, z: 0.0}, angular: {x: 0.0, y: 0.0, z: 0.0}}"

# Spin in place (turn left)
ros2 topic pub --once /cmd_vel geometry_msgs/msg/Twist \
  "{linear: {x: 0.0, y: 0.0, z: 0.0}, angular: {x: 0.0, y: 0.0, z: 0.5}}"
```

### Control the Servo

```bash
# Full left (0 degrees)
ros2 topic pub --once /cmd_servo std_msgs/msg/Int32 "{data: 0}"

# Centre (90 degrees)
ros2 topic pub --once /cmd_servo std_msgs/msg/Int32 "{data: 90}"

# Full right (180 degrees)
ros2 topic pub --once /cmd_servo std_msgs/msg/Int32 "{data: 180}"
```

### Call the Soil Moisture Service

```bash
ros2 service call /srv/read_moisture std_srvs/srv/Trigger "{}"
```

Expected response:

```
success: True
message: '{"raw":2048,"percent":50.0}'
```

- `raw`: 12-bit ADC value (0-4095)
- `percent`: Moisture estimate (high ADC = dry = low %, low ADC = wet = high %)

## Boot Sequence

The serial monitor shows the full initialisation sequence:

```
================================================
   AgriBot ESP32 Firmware  v1.0  (micro-ROS)
================================================
[SW  ] Reserved GPIOs 18, 19, 23 -> OUTPUT LOW
[DHT ] Initialised on GPIO 4
[SOIL] ADC initialised on GPIO 34
[MTR ] ch0  EN=14  IN1=27  IN2=26
[MTR ] ch1  EN=32  IN3=25  IN4=33
[SRV ] Initialised on GPIO 13 (centred at 90)
[WIFI] Connecting to "YOUR_SSID" ....
[WIFI] Connected - IP 192.168.1.42
[mDNS] Broadcasting hostname: esp.local
[mDNS] Resolving agripi.local ....
[mDNS] Resolved agripi.local -> 192.168.1.10
[uROS] Setting transport -> 192.168.1.10:8888
[uROS] Node fully initialised
[BOOT] All tasks launched - entering idle.
[TASK] Network  -> Core 0
[TASK] Hardware -> Core 1
```

## Troubleshooting

| Symptom | Likely Cause | Fix |
|---|---|---|
| ESP32 hangs at `Resolving agripi.local` | Raspberry Pi mDNS not running or different subnet | Run `avahi-resolve -n agripi.local` on a laptop to verify. Ensure both devices are on the same network |
| `[ERR] rclc_support_init` | micro-ROS agent not running or wrong port | Start the agent: `ros2 run micro_ros_agent micro_ros_agent udp4 --port 8888` |
| DHT reads return NaN | Wiring issue or missing 10k pull-up resistor | Verify the DHT11 data pin is connected to GPIO 4 with a 10k pull-up to 3.3V |
| Motors do not spin | L298N 12V supply not connected, or ENA/ENB jumper removed without PWM | Check motor driver power and that ENA/ENB are wired to GPIO 14/32 |
| Wi-Fi crashes during motor operation | Hardware tasks running on Core 0 | Verify the firmware is unmodified -- motor PWM must only run in `taskHardware` on Core 1 |
| Soil moisture always reads 0% or 100% | Sensor not in soil, or wrong GPIO | Confirm the sensor is on GPIO 34 (ADC1). ADC2 pins conflict with Wi-Fi |
| Servo jitters | Power supply brownout | Power the servo from an external 5V supply, not from the ESP32 3.3V rail |

## License

This firmware is part of the AgriBot project. See the repository root for license details.
