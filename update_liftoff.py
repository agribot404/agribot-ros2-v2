import re

with open("liftoff.md", "r") as f:
    content = f.read()

# Update 3.B
old_3b = "3. **`web_server`** (Port `8080`): The FastAPI server serving the frontend and REST API."
new_3b = "3. **`web_server`** (Port `8080`): The FastAPI server serving the frontend and REST API.\n4. **`micro_ros_agent`** (Port `8888`): A Docker container running the micro-ROS agent, bridging the ESP32 nodes to the ROS 2 network."
content = content.replace(old_3b, new_3b)

# Update 3.C
old_3c = """### C. Where is the Camera and micro-ROS Agent?
- **Camera Stream**: The camera stream operates independently of ROS 2. It serves an MJPEG stream directly over HTTP on port 81 and broadcasts via mDNS as `agricam.local`. The dashboard embeds this directly.
- **micro-ROS Agent**: If the micro-ROS agent is *not* dynamically included in your local `agribot_web.launch.py`, you must either add it to the launch file or run it in a separate terminal:

```bash
# Run standalone micro-ROS agent (UDP port 8888)
ros2 run micro_ros_agent micro_ros_agent udp4 --port 8888
```"""
new_3c = """### C. Where is the Camera?
- **Camera Stream**: The camera stream operates independently of ROS 2. It serves an MJPEG stream directly over HTTP on port 81 and broadcasts via mDNS as `agricam.local`. The dashboard embeds this directly.
*(Note: The micro-ROS Agent is now automatically started via Docker in the launch file.)*"""
content = content.replace(old_3c, new_3c)

with open("liftoff.md", "w") as f:
    f.write(content)
