# 🌱 Agribot — Smart Agriculture Robot (v2)

## 📖 Project Overview
Agribot is a smart, autonomous, differential-drive agricultural robot designed to bridge modern robotics middleware with robust field hardware. The project utilizes an ESP32 micro-controller operating via Micro-ROS to communicate with a ROS 2 (Humble) environment. It allows for real-time monitoring of environmental metrics (temperature, humidity, soil moisture) and offers remote control capabilities through a modern web-based dashboard. 

The architecture guarantees high-reliability operation by isolating network overhead from hardware control using dual-core execution (FreeRTOS) on the ESP32.

---

## 🏗️ Architecture & Flow
1. **ESP32 Firmware (Micro-ROS):** Acts as the bridge to the physical world. Core 0 handles the Wi-Fi/LwIP stack and the Micro-ROS executor. Core 1 handles time-sensitive hardware tasks like reading sensors (bit-banging DHT11), outputting PWM to the L298N motor driver, and managing servo actuation.
2. **ROS 2 Ecosystem:** The host machine runs the Micro-ROS agent, translating ESP32 messages into standard ROS 2 topics. Custom ROS 2 nodes (`esp-drive-node`, `esp-payload-node`) manage high-level logic, kinematics, and payloads.
3. **Web Dashboard:** A React-based web interface communicates directly with the ROS 2 network using `rosbridge_server` (via WebSockets) and `roslibjs`, allowing users to monitor sensor streams, drive the robot (cmd_vel), and control payloads in real-time.

---

## 🛠️ Technology Stack

### Middleware & Backend
* **ROS 2 Humble Hawksbill:** Core robotics middleware.
* **Micro-ROS:** For bringing ROS 2 concepts directly to the microcontroller.
* **rosbridge_server:** Provides a WebSocket interface to ROS 2 topics/services.
* **Python 3:** Used for building custom ROS 2 host nodes.

### Firmware (ESP32)
* **PlatformIO:** Build system and dependency manager.
* **Arduino Framework:** Core C++ hardware abstractions.
* **FreeRTOS:** Real-time operating system utilized for dual-core task scheduling (Core 0: Network/Protocol, Core 1: Application/Hardware).

### Frontend (Web Dashboard)
* **React 19:** UI library.
* **TypeScript:** Static typing for safer code.
* **Vite:** Next-generation fast build tool.
* **Tailwind CSS:** Utility-first CSS framework for styling.
* **roslibjs (roslib):** Standard JS library for interfacing with ROS from the browser.

---

## ⚙️ Hardware Components
* **Microcontroller:** ESP32-WROOM (Dual-Core Xtensa LX6).
* **Motor Controller & Motors:** 1x L298N Dual H-Bridge Motor Driver controlling 4 DC Motors (Differential Drive setup: 2 left-side motors wired to one channel, 2 right-side motors wired to the other).
* **Environmental Sensor:** DHT11 (Temperature and Humidity).
* **Ground Sensor:** Analog Soil Moisture Sensor.
* **Actuation:** Standard Servo Motor (Payload/Tool Control).

### 🚀 Future Hardware Implementation (Camera Feed)
To enhance the robot's remote operability, field monitoring, and future computer-vision capabilities (e.g., crop disease detection, obstacle avoidance), the next hardware revision will include a **Live Camera Feed**.

We will integrate one of the following options:
* **ESP32-CAM Module:** A low-cost, compact module capable of delivering an MJPEG video stream directly over Wi-Fi. This acts as a lightweight standalone IP camera.
* **Raspberry Pi Camera Module:** Integrated directly into an onboard Raspberry Pi companion computer, offering high-definition video, lower latency, and the bandwidth to perform heavy edge-AI processing before passing data to the ROS 2 network.

---

## 📂 Project Structure
* `/esp-firmware/` - Legacy combined Micro-ROS C++ firmware for a single ESP32 setup.
* `/esp-drive-node/` & `/esp-payload-node/` - Micro-ROS C++ firmware PlatformIO projects for the ESP32s handling drive logic and payload sensors/actuation.
* `/agribot_web/` - Contains the `rosbridge` launcher and the Vite/React frontend dashboard.
* `/agribot/` - Legacy/Fallback implementation containing standalone Firebase integrations and ESP32-CAM sketches.
