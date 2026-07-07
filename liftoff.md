# 🚀 AgriBot Liftoff Guide

Welcome to the central operations manual for bringing AgriBot online. This guide covers hardware pinouts, firmware flashing procedures, and the single-command launch process for the entire ROS 2 stack.

---

## 🔌 1. Pin Configurations

AgriBot runs on three ESP32 nodes. Here are the active pin mappings for the configurations:

### Drive Node (`esp-drive-node`)
Handles differential drive kinematics and ultrasonic safety sensors.

| Component / Function | GPIO Pin | Notes |
| :--- | :--- | :--- |
| **Motor A (Left) EN** | `GPIO 14` | PWM Speed Control (Channel A) |
| **Motor A IN1** | `GPIO 27` | Direction (Forward) |
| **Motor A IN2** | `GPIO 26` | Direction (Reverse) |
| **Motor B (Right) EN** | `GPIO 32` | PWM Speed Control (Channel B) |
| **Motor B IN3** | `GPIO 25` | Direction (Forward) |
| **Motor B IN4** | `GPIO 33` | Direction (Reverse) |
| **Sonar Front TRIG** | `GPIO 16` | Front Ultrasonic Trigger |
| **Sonar Front ECHO** | `GPIO 17` | Front Ultrasonic Echo |
| **Sonar Rear TRIG** | `GPIO 18` | Rear Ultrasonic Trigger |
| **Sonar Rear ECHO** | `GPIO 19` | Rear Ultrasonic Echo |

### Payload Node (`esp-payload-node`)
Handles environmental sensing and tool actuation.

| Component / Function | GPIO Pin | Notes |
| :--- | :--- | :--- |
| **DHT11 Data** | `GPIO 4` | Temperature & Humidity |
| **Soil Moisture** | `GPIO 34` | Analog Input (ADC1_CH6) |
| **Servo PWM** | `GPIO 13` | Tool/Payload Actuation |
| **Switch/Relay 1** | `GPIO 21` | Auxiliary Output 1 |
| **Switch/Relay 2** | `GPIO 22` | Auxiliary Output 2 |
| **Switch/Relay 3** | `GPIO 23` | Auxiliary Output 3 |

### Camera Node (`esp-cam-node`)
Handles the live MJPEG video stream (Standalone, no micro-ROS).

| Component / Function | GPIO Pin | Notes |
| :--- | :--- | :--- |
| **Camera (OV2640)** | Various | Standard AI Thinker ESP32-CAM Pinout |
| **LED Flash** | `GPIO 4` | Built-in high-power LED |
| **Download Mode** | `GPIO 0` | Must connect to GND during flashing |

*(Note: There is also an older combined firmware in `esp-firmware/` that maps sensors and motors across a single ESP32, but the modern node-based architecture splits them.)*

---

## ⚡ 2. Flashing Firmware

All firmwares are built using **PlatformIO** and the Arduino framework.

### Prerequisites
1. Ensure your ESP32 is connected via USB. (For ESP32-CAM, you must use an FTDI adapter).
2. Ensure you have the `wifi_config.h` properly set up with your local credentials at the workspace root (`/home/agribot/agribot-ros2-v2/wifi_config.h`). All nodes read from this shared file.

### Flashing the Drive Node
```bash
cd esp-drive-node
pio run -t upload
```

### Flashing the Payload Node
```bash
cd esp-payload-node
pio run -t upload
```

### Flashing the Camera Node (ESP32-CAM)
The ESP32-CAM requires an FTDI programmer. Connect `GPIO 0` to `GND` before powering it on to enter Download Mode.
```bash
cd esp-cam-node
pio run -t upload
```
*Note: After flashing, remove the jumper from GPIO 0 and press the RESET button to start the stream.*

---

## 🚀 3. Starting the ROS 2 Stack (Production)

To fulfill the goal of a **single ROS launch command**, we use a unified launch file that spins up both the micro-ROS agent (which bridges the ESP32s to the ROS 2 network) and the web interface backends.

### A. The Single Launch Command
Run the following from the root of your workspace to start the entire system:

```bash
# Source the ROS 2 environment
source /opt/ros/humble/setup.bash
source install/setup.bash

# Run the unified web package launch
ros2 launch agribot_web agribot_web.launch.py
```

### B. What this launches (`agribot_web.launch.py`):
1. **`rosbridge_websocket`** (Port `9090`): Bridges ROS 2 DDS to WebSockets for the frontend React dashboard.
2. **`sensor_logger`**: A ROS 2 Python node that subscribes to DHT11 topics and logs to SQLite.
3. **`web_server`** (Port `8080`): The FastAPI server serving the frontend and REST API.

### C. Where is the Camera and micro-ROS Agent?
- **Camera Stream**: The camera stream operates independently of ROS 2. It serves an MJPEG stream directly over HTTP on port 81 and broadcasts via mDNS as `agricam.local`. The dashboard embeds this directly.
- **micro-ROS Agent**: If the micro-ROS agent is *not* dynamically included in your local `agribot_web.launch.py`, you must either add it to the launch file or run it in a separate terminal:

```bash
# Run standalone micro-ROS agent (UDP port 8888)
ros2 run micro_ros_agent micro_ros_agent udp4 --port 8888
```

---

## 💻 4. Dashboard Development Mode

The launch command above runs the FastAPI server which serves the pre-built React frontend in "production" mode. If you want to make changes to the dashboard UI and see them update in real-time, you need to run the Vite development server.

1. First, make sure your ROS 2 backend is running (so the WebSocket and APIs are available):
   ```bash
   ros2 launch agribot_web agribot_web.launch.py
   ```

2. Open a new terminal and start the Vite dev server:
   ```bash
   cd agribot_web/frontend
   
   # Install dependencies (only needed the first time)
   npm install
   
   # Start the development server and expose it to your local network
   npm run dev -- --host
   ```

3. Open your browser on any device (like your laptop) to `http://<ROBOT_IP>:5173`. 
   *(Vite will automatically proxy `/api` requests to your FastAPI backend, and the ROS bridge will dynamically connect using your network IP).*

---

## 🛠️ 5. Auto-Sourcing ROS 2 (Pro-Tip)

To save time and avoid running `source /opt/ros/humble/setup.bash` every time you open a new terminal, you can add it to your `~/.bashrc` file. Run these commands once:

```bash
# Auto-source ROS 2 Humble
echo "source /opt/ros/humble/setup.bash" >> ~/.bashrc

# Optional: Auto-source the Agribot workspace (if you always work from this project)
echo "source /home/agribot/agribot-ros2-v2/install/setup.bash" >> ~/.bashrc

# Apply the changes to your current terminal
source ~/.bashrc
```
