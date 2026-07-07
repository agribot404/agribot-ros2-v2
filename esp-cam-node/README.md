# AgriBot Camera Node (esp-cam-node)

This folder contains the modern PlatformIO-based firmware for the ESP32-CAM module to stream MJPEG video to the Agribot Dashboard.

## Hardware Setup
1. Use an AI Thinker ESP32-CAM board.
2. Connect an FTDI Programmer (5V or 3.3V power, GND, TX to U0R, RX to U0T).
3. Connect GPIO 0 to GND before applying power to enter Download Mode.

## Configuration
WiFi credentials are automatically pulled from `wifi_config.h` located in the root of the project repository. You do not need to configure them separately here.

## Flashing (PlatformIO)
Using PlatformIO, simply build and upload:
```bash
pio run -t upload
```

After uploading, remove the GPIO 0 jumper and press the RESET button.

## Endpoints
- Live Stream: `http://agricam.local:81/stream`
- Snapshot: `http://agricam.local:81/capture`
- Status/Health: `http://agricam.local:81/health`
- Flash Control: `http://agricam.local:81/control?flash=[0|1|2]` (0: Off, 1: On, 2: Auto)
