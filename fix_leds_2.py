import os

def insert_after(filepath, search_str, insert_str):
    if not os.path.exists(filepath): return
    with open(filepath, "r") as f:
        lines = f.readlines()
    
    out = []
    for line in lines:
        out.append(line)
        if search_str in line and insert_str not in "".join(lines):
            out.append(insert_str)
            
    with open(filepath, "w") as f:
        f.writelines(out)

insert_after("esp-cam-node/src/main.cpp", "WiFi.RSSI());", "    digitalWrite(BLUE_LED, HIGH);\n")
insert_after("esp-payload-node/src/main.cpp", "WiFi.localIP().toString().c_str());", "        digitalWrite(BLUE_LED, HIGH);\n")
insert_after("esp-drive-node/src/main.cpp", "WiFi.localIP().toString().c_str());", "        digitalWrite(BLUE_LED, HIGH);\n")
insert_after("esp-firmware/src/main.cpp", "WiFi.localIP().toString().c_str());", "    digitalWrite(BLUE_LED, HIGH);\n")
insert_after("agribot/esp32_cam/esp32_cam.ino", "Serial.println(\"WiFi connected\");", "  digitalWrite(BLUE_LED, HIGH);\n")
insert_after("agribot/esp32_main/esp32_main.ino", "Serial.println(WiFi.localIP());", "  digitalWrite(BLUE_LED, HIGH);\n")

