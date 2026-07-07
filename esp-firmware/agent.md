# Agent Guide -- AgriBot ESP32 Firmware

This document is for AI coding agents and human contributors working on the `esp-firmware` project. It describes the codebase layout, conventions, build workflow, and common modification patterns so that changes can be made correctly on the first attempt.

## Project Identity

- **What this is:** Micro-ROS firmware for an ESP32 WROOM that bridges ROS 2 Humble with agricultural field hardware (sensors, motors, servo).
- **Build system:** PlatformIO (Arduino framework).
- **Language:** C++17 (ESP-IDF toolchain default).
- **Target board:** `esp32dev` (ESP32 WROOM, dual-core Xtensa LX6, 4 MB flash).
- **ROS 2 middleware:** micro-ROS over Wi-Fi (Micro XRCE-DDS, UDP transport).
- **RTOS:** FreeRTOS (bundled with ESP-IDF / Arduino-ESP32).

## Directory Layout

```
esp-firmware/
├── platformio.ini          # Board, framework, libs, build flags
├── include/                # (empty -- reserved for future headers)
├── src/
│   └── main.cpp            # All firmware source (~754 lines, single-file)
├── README.md               # User-facing documentation
└── agent.md                # This file
```

The project is intentionally single-file. All classes, FreeRTOS tasks, callbacks, and the `setup()`/`loop()` entry points live in `src/main.cpp`. If the file grows past ~1200 lines, consider splitting classes into `include/` headers.

## Source Code Map

Reference these line ranges (approximate) when navigating `src/main.cpp`:

| Section | Lines | Description |
|---|---|---|
| Includes | 1-29 | Arduino, WiFi, ESPmDNS, micro-ROS headers, ROS 2 message types, DHT |
| Pin definitions | 31-58 | `constexpr` GPIO assignments for every peripheral |
| LEDC PWM config | 60-78 | Channel, frequency, resolution constants for motors and servo |
| Network config | 80-88 | Wi-Fi SSID/password, mDNS names, agent port |
| Timing constants | 90-96 | DHT interval, watchdog timeout, control loop period |
| RC check macros | 98-109 | `RCCHECK` / `RCSOFTCHECK` error-handling wrappers |
| SharedState | 111-132 | Inter-core data struct + `portMUX` spinlock |
| DHTSensor class | 134-173 | Non-blocking periodic DHT11 read |
| SoilMoistureSensor class | 175-200 | ADC1 analog read |
| L298NMotorDriver class | 202-258 | Single H-bridge: direction + PWM |
| ServoController class | 260-289 | 50 Hz hardware PWM, 0-180 degrees |
| ReservedOutputs class | 291-305 | GPIO 18, 19, 23 set LOW |
| MicroROSNode class | 307-555 | RCL/RCLC init, publishers, subscribers, service, timer, executor, all static callbacks |
| Global instances | 557-567 | Object construction for all hardware + ROS classes |
| taskNetwork() | 569-583 | FreeRTOS task pinned to Core 0 (executor spin) |
| taskHardware() | 585-650 | FreeRTOS task pinned to Core 1 (DHT, motors, servo, watchdog) |
| connectAndResolveAgent() | 652-691 | Wi-Fi connect, mDNS hostname broadcast, agent IP resolution |
| setup() | 693-746 | Arduino entry point: init hardware, connect, launch tasks |
| loop() | 748-754 | Idle (all work in FreeRTOS tasks) |

## Critical Architecture Rules

These rules exist to prevent Wi-Fi stack crashes and data races. Do not violate them.

### 1. Core Affinity

| Core | Task Name | Stack | Priority | Responsibility |
|---|---|---|---|---|
| Core 0 | `uros_net` | 8192 B | 2 | Wi-Fi/LwIP stack, micro-ROS executor (`spin_some`) |
| Core 1 | `hw_ctrl` | 4096 B | 3 | DHT11 read, motor PWM, servo PWM, cmd_vel watchdog |

- **Never** call timing-critical hardware functions (DHT bit-bang, motor PWM writes) from Core 0.
- **Never** call `rcl_publish`, `rclc_executor_spin_some`, or any RCL function from Core 1.
- The Arduino `loop()` is intentionally idle. All work runs in the two FreeRTOS tasks.

### 2. Inter-Core Communication

All shared data flows through the `SharedState g_shared` struct, protected by the `portMUX_TYPE g_mux` spinlock.

```
Core 0 (writes)                     Core 1 (reads)
─────────────────                   ─────────────────
cmd_vel callback  ──→  motor_left_speed   ──→  L298NMotorDriver::setSpeed()
                       motor_right_speed
                       cmd_vel_stamp_ms

cmd_servo callback ──→ servo_angle        ──→  ServoController::setAngle()

Core 1 (writes)                     Core 0 (reads)
─────────────────                   ─────────────────
DHTSensor::tryRead() ──→ temperature      ──→  timerDHTCallback → publishDHT()
                          humidity
                          dht_data_ready
```

Access pattern:

```cpp
portENTER_CRITICAL(&g_mux);
// read or write g_shared fields
portEXIT_CRITICAL(&g_mux);
```

Keep critical sections as short as possible (copy values in/out, no I/O inside the lock).

### 3. ADC Channel Safety

- **ADC1** (GPIOs 32-39): Safe to use while Wi-Fi is active. The soil moisture sensor is on GPIO 34 (ADC1).
- **ADC2** (GPIOs 0, 2, 4, 12-15, 25-27): **Cannot** be used while Wi-Fi is active on ESP32. Never move the soil sensor to an ADC2 pin.

### 4. LEDC Channel Allocation

| Channel | Peripheral | Frequency | Resolution |
|---|---|---|---|
| 0 | Motor A (ENA) | 1 kHz | 8-bit |
| 1 | Motor B (ENB) | 1 kHz | 8-bit |
| 2 | Servo | 50 Hz | 16-bit |

Channels 3-15 are available for future use. On ESP32, channels 0-7 share timer group 0, and channels 8-15 share timer group 1. Channels 0 and 1 share the same timer, so they must use the same frequency (1 kHz). Channel 2 is on a different timer pair, allowing 50 Hz independently.

## Common Modification Patterns

### Adding a New ROS 2 Publisher

1. Add the message type include at the top of `main.cpp`:
   ```cpp
   #include <std_msgs/msg/float32.h>
   ```

2. Add a `rcl_publisher_t` member to `MicroROSNode` (private section, ~line 465):
   ```cpp
   rcl_publisher_t pub_new_topic_;
   ```

3. Initialise it in `MicroROSNode::init()` after the existing publishers:
   ```cpp
   RCCHECK(rclc_publisher_init_default(
       &pub_new_topic_, &node_,
       ROSIDL_GET_MSG_TYPE_SUPPORT(std_msgs, msg, Float32),
       "topic/new_data"));
   ```

4. Publish from the appropriate callback or timer. If data comes from Core 1, add a field to `SharedState` and publish from a timer callback on Core 0.

5. The executor handle count does **not** need to increase for publishers (only for subscribers, services, and timers).

### Adding a New ROS 2 Subscriber

1. Add the message type include.

2. Add `rcl_subscription_t` and a message buffer to `MicroROSNode`.

3. Initialise with `rclc_subscription_init_default()` in `init()`.

4. Write a `static void` callback that updates `SharedState`.

5. Register with `rclc_executor_add_subscription()`.

6. **Increment the executor handle count** in `rclc_executor_init()` (currently `4`).

### Adding a New ROS 2 Service

1. Include the service type header (e.g., `<std_srvs/srv/set_bool.h>`).

2. Add `rcl_service_t`, request buffer, and response buffer to `MicroROSNode`.

3. Initialise with `rclc_service_init_default()`.

4. Write a static callback with signature `void callback(const void* req, void* res)`.

5. Register with `rclc_executor_add_service()`.

6. **Increment the executor handle count**.

### Adding a New Hardware Peripheral

1. Define the GPIO pin as a `static constexpr uint8_t` in the pin definitions section.

2. Create a class following the existing pattern (constructor takes pin, `begin()` initialises hardware).

3. Instantiate globally alongside the other objects (~line 561).

4. Call `begin()` in `setup()`.

5. If the peripheral needs periodic servicing, add it to `taskHardware()` on Core 1.

6. If it needs ROS 2 exposure, add fields to `SharedState` and create the appropriate publisher/subscriber/service in `MicroROSNode`.

### Activating the Reserved Switches (GPIO 21, 22, 23)

These are pre-configured as OUTPUT LOW. To expose them to ROS 2:

1. Add a subscriber to `MicroROSNode` for a suitable message type (e.g., `std_msgs/msg/Int32` where the data field selects which switch and state).

2. Add a field to `SharedState` (e.g., `uint8_t switch_states`).

3. In `taskHardware()`, read the field and call `digitalWrite()` on the corresponding pin.

4. Increment the executor handle count.

## Build and Test Workflow

### Build

```bash
cd esp-firmware
pio run
```

A clean build takes ~2-3 minutes (micro_ros_arduino is large). Incremental builds are fast.

### Flash and Monitor

```bash
pio run -t upload && pio device monitor -b 115200
```

### Verify on the ROS 2 Side

These commands run on the Raspberry Pi or any ROS 2 Humble machine on the same network:

```bash
# Start the agent
ros2 run micro_ros_agent micro_ros_agent udp4 --port 8888

# Check the node registered
ros2 node list                        # expect: /agribot_esp32

# Check topics
ros2 topic list

# Echo sensor data
ros2 topic echo /sensor/dht11_temperature
ros2 topic echo /sensor/dht11_humidity

# Drive motors
ros2 run teleop_twist_keyboard teleop_twist_keyboard

# Move servo
ros2 topic pub --once /cmd_servo std_msgs/msg/Int32 "{data: 45}"

# Read soil moisture
ros2 service call /srv/read_moisture std_srvs/srv/Trigger "{}"
```

### Serial Log Tags

The firmware prefixes all serial output with a bracketed tag for easy filtering:

| Tag | Source |
|---|---|
| `[BOOT]` | `setup()` lifecycle |
| `[WIFI]` | Wi-Fi connection |
| `[mDNS]` | mDNS broadcast and resolution |
| `[uROS]` | micro-ROS transport and node init |
| `[DHT ]` | DHT11 sensor reads and publishes |
| `[SOIL]` | Soil moisture service calls |
| `[MTR ]` | Motor driver init |
| `[SRV ]` | Servo controller init |
| `[SW  ]` | Reserved switch init |
| `[TASK]` | FreeRTOS task launch |
| `[ERR]` | RCL return code errors |

## Conventions

- **Naming:** Classes use PascalCase. Constants use UPPER_SNAKE_CASE. Member variables use trailing underscore (`pin_`, `ledc_ch_`). Global objects use `g_` prefix.
- **Memory:** No dynamic allocation at runtime. The only `malloc` is the Trigger service response buffer, allocated once during `init()`.
- **Error handling:** `RCCHECK()` logs errors but does not halt. `RCSOFTCHECK()` silently ignores errors (used for non-critical publish calls).
- **Includes:** All ROS 2 message types use the C bindings (e.g., `sensor_msgs/msg/temperature.h`, not `.hpp`).
- **Hardware access:** All `digitalWrite`, `analogRead`, `ledcWrite` calls happen exclusively on Core 1 inside `taskHardware()`, except for `analogRead(PIN_SOIL)` in the moisture service callback (safe because it is ADC1).

## Dependencies

Managed by PlatformIO in `platformio.ini`:

| Library | Version | Purpose |
|---|---|---|
| `micro_ros_arduino` | `humble` branch | micro-ROS client library for Arduino |
| `DHT sensor library` (Adafruit) | `^1.4.6` | DHT11/DHT22 driver |
| `Adafruit Unified Sensor` | `^1.1.14` | Required dependency of the DHT library |

Platform: `espressif32@6.9.0` (ESP-IDF 5.x based Arduino core).

## Known Limitations

- **Single-file architecture:** All code is in `main.cpp`. Acceptable at ~750 lines but should be split if the file exceeds ~1200 lines.
- **No OTA updates:** Firmware must be flashed over USB. OTA can be added via `ArduinoOTA` or the PlatformIO OTA upload target.
- **No persistent configuration:** Wi-Fi credentials are compiled in. A future improvement could use `Preferences.h` (ESP32 NVS) or a BLE provisioning flow.
- **Soil moisture service uses `std_srvs/srv/Trigger`:** This returns a JSON string in the `message` field rather than a dedicated custom service type. A custom `.srv` definition would be cleaner but requires building a micro-ROS custom message package.
- **Motor speed is normalised, not calibrated:** The `cmd_vel` linear/angular values are directly mixed and clamped to -1.0 to +1.0. There is no wheel radius, track width, or encoder feedback. For accurate odometry, a proper kinematic model and PID controller would be needed.
- **DHT11 accuracy:** The DHT11 has +/-2 degrees C and +/-5% RH accuracy. The `variance` field in the published messages is set to `0.0` (unknown). Set it to measured variance if calibration data is available.
