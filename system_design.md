# 🤖 Agribot (v2) — System Design Architecture

Agribot is a smart, autonomous, differential-drive agricultural robot designed to connect robust field hardware with modern robotics middleware. The system relies on a multi-node microcontroller setup using Micro-ROS to interact directly with a host machine running ROS 2 (Humble), allowing for real-time monitoring and web-based teleoperation.

---

## 🏗️ 1. Fundamental Components of System Design

The system follows a highly modular, decoupled architecture distributed across five primary layers:

1. **Hardware & Sensing Layer:** The physical realm where motors, servos, ultrasonic sensors, and environmental sensors are wired to multiple distributed ESP32 microcontrollers.
2. **Firmware & RTOS Layer:** Custom C++ firmware executing on ESP32 microcontrollers. It utilizes FreeRTOS for dual-core task scheduling (Core 0 manages Wi-Fi/Micro-ROS network protocols; Core 1 handles time-critical hardware interrupts, bit-banging, and motor PWM).
3. **Middleware & Routing Layer (ROS 2):** A host machine running ROS 2 (Humble). A containerized `micro_ros_agent` bridges the DDS network to the microcontrollers. It translates raw serial/Wi-Fi packets into standard, routable ROS 2 Topics and Services.
4. **Backend & Application Logic Layer:** Contains independent Python ROS 2 nodes handling specific tasks like automated obstacle avoidance (`obstacle_avoidance.py`), database logging (`sensor_logger.py`), and a FastAPI web server serving REST endpoints.
5. **Frontend UI Layer:** A modern web-based dashboard that connects to the ROS 2 network directly via WebSockets (`rosbridge_websocket` + `roslibjs`) and HTTP for seamless teleoperation, live video, and real-time telemetry rendering.

---

## ⚙️ 2. Hardware Components Brief

The Agribot separates hardware concerns across three distinct "Nodes" to distribute processing loads and avoid latency:

*   **Microcontrollers:**
    *   **ESP32-WROOM (Dual-Core Xtensa LX6):** Used for both the Drive Node and the Payload Node.
    *   **ESP32-CAM (AI Thinker):** Used specifically for the camera node, featuring a built-in high-power LED flash.
*   **Drive & Locomotion (`esp-drive-node`):**
    *   **L298N Dual H-Bridge Motor Driver:** Controls speed and direction.
    *   **4x DC Motors:** Wired in a Differential Drive configuration (2 left-side motors on Channel A, 2 right-side motors on Channel B).
*   **Safety & Environment (`esp-drive-node` & `esp-payload-node`):**
    *   **2x Ultrasonic Sensors (HC-SR04):** Mounted Front and Rear for collision detection and obstacle avoidance.
    *   **DHT11 Sensor:** Measures precise atmospheric Temperature and Humidity.
    *   **Analog Soil Moisture Sensor:** Ground sensor for evaluating crop hydration.
*   **Actuators & Payload (`esp-payload-node`):**
    *   **Standard Servo Motor:** Provides PWM-based angular control for a payload/tool arm.
    *   **3x Auxiliary Relays/Switches:** GPIO-controlled outputs for custom tools (e.g., pumps, lights).
*   **Vision & Camera (`esp-cam-node`):**
    *   **OV2640 Camera Module:** Captures field data and broadcasts a live MJPEG video stream.

---

## 💻 3. Software Stacks Involved

The project relies on a diverse set of modern frameworks across the embedded, backend, and frontend environments.

### Firmware (ESP32)
*   **C++ & Arduino Framework:** Core hardware abstractions.
*   **PlatformIO:** Dependency management and build system toolchain.
*   **FreeRTOS:** Real-time OS for thread/core scheduling.
*   **Micro-ROS:** DDS bridging framework tailored for microcontrollers.

### Middleware & Host System
*   **ROS 2 Humble Hawksbill:** Core robotics DDS middleware.
*   **Docker:** Used to containerize the `micro_ros_agent`.
*   **Python 3:** The primary language for host-side ROS 2 nodes and backend services.
*   **rosbridge_server:** Converts DDS topics into WebSocket JSON streams for the frontend.

### Web Server & Backend
*   **FastAPI:** High-performance async Python web framework used for handling non-ROS REST logic and serving the frontend statically in production.

### Frontend (Web Dashboard)
*   **React 19:** Component-based UI library.
*   **TypeScript:** Static typing for safer frontend data handling.
*   **Vite:** Extremely fast frontend build tool and development server.
*   **Tailwind CSS:** Utility-first framework for responsive UI styling.
*   **roslibjs:** JS library handling WebSocket handshakes and message serialization with ROS 2.

---

## 🗄️ 4. Database Stacks

*   **SQLite:** Used locally by the backend. The `sensor_logger.py` ROS 2 node subscribes to environmental metrics (like DHT11 Temperature and Humidity) from the DDS network and writes them to a lightweight, serverless SQLite database. This provides historical telemetry logs without the overhead of heavy RDBMS setups.

---

## 🌟 5. Overall System Features

*   **Real-Time Teleoperation:** Users can drive the robot smoothly from a web browser via an on-screen joystick, which publishes `cmd_vel` instructions over WebSockets directly to the ESP32 Drive node.
*   **Live Video Streaming:** The ESP32-CAM broadcasts an independent, low-latency MJPEG stream (over HTTP/mDNS) that embeds directly into the React dashboard.
*   **Live Telemetry Dashboard:** Visualizes real-time environmental data (Temperature, Humidity, Soil Moisture) originating from field sensors.
*   **Hardware Actuation Control:** Toggles payload servos and standard on/off auxiliary relays via the web dashboard.
*   **Autonomous Obstacle Avoidance:** A dedicated ROS 2 node (`obstacle_avoidance.py`) intercepts ultrasonic sensor data and can override manual commands to halt the robot and prevent collisions.
*   **Data Logging:** Automatic background archival of historical sensor data into an SQLite database for post-field analysis.
*   **Single-Command Deployment:** Entire host stack (ROS bridge, web servers, Micro-ROS Docker agent, backend nodes) spins up together using a unified ROS 2 launch file (`agribot_web.launch.py`).