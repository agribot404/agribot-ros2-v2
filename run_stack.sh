#!/bin/bash
source /opt/ros/humble/setup.bash
source install/setup.bash
ros2 launch agribot_web agribot_web.launch.py > launch.log 2>&1 &
