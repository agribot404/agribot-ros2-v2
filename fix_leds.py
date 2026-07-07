import os

files = [
    ("esp-cam-node/src/main.cpp", "WiFi connected", "[cam]"),
    ("esp-payload-node/src/main.cpp", "WiFi OK", "[payload]"),
    ("esp-drive-node/src/main.cpp", "WiFi OK", "[drive]"),
    ("esp-firmware/src/main.cpp", "[WIFI] Connected", "[WIFI]"),
    ("agribot/esp32_cam/esp32_cam.ino", "WiFi connected", "WiFi"),
    ("agribot/esp32_main/esp32_main.ino", "Connected with IP:", "IP")
]

for filepath, search_str, _ in files:
    if not os.path.exists(filepath): continue
    with open(filepath, "r") as f:
        content = f.read()
    
    # add #define BLUE_LED 2 at the top
    if "#define BLUE_LED 2" not in content:
        content = "#define BLUE_LED 2\n" + content
    
    # add pinMode(BLUE_LED, OUTPUT); in setup
    if "pinMode(BLUE_LED, OUTPUT);" not in content:
        content = content.replace("Serial.begin(115200);", "Serial.begin(115200);\n    pinMode(BLUE_LED, OUTPUT);")
    
    with open(filepath, "w") as f:
        f.write(content)

