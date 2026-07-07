import re

with open("documentation.md", "r") as f:
    content = f.read()

old_nodes = "* `/esp-drive-node/` & `/esp-payload-node/` - Python-based ROS 2 nodes for high-level robot logic and payload control."
new_nodes = "* `/esp-drive-node/` & `/esp-payload-node/` - Micro-ROS C++ firmware PlatformIO projects for the ESP32s handling drive logic and payload sensors/actuation."
content = content.replace(old_nodes, new_nodes)

old_firmware = "* `/esp-firmware/` - Micro-ROS C++ firmware for the main ESP32 (sensors, motors)."
new_firmware = "* `/esp-firmware/` - Legacy combined Micro-ROS C++ firmware for a single ESP32 setup."
content = content.replace(old_firmware, new_firmware)

with open("documentation.md", "w") as f:
    f.write(content)
