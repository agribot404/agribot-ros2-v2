#!/bin/bash

echo "Cleaning up old processes..."
# Kill any existing micro_ros_agent (which might be running as root from systemd/Docker etc)
sudo killall -9 micro_ros_agent 2>/dev/null || true
# Kill any lingering ros2 launch or frontend servers started by this user
killall -9 ros2 2>/dev/null || true

echo "Starting ROS2 launch..."
# Start ros2 launch in the background
ros2 launch agribot_web agribot_web.launch.py &
ROS_PID=$!

echo "Starting frontend..."
cd agribot_web/frontend
npm install
npm run dev -- --host &
NPM_PID=$!

# Cleanup function to kill both processes when script is stopped
cleanup() {
    echo "Stopping agribot..."
    kill $ROS_PID $NPM_PID 2>/dev/null
    exit 0
}

# Trap Ctrl+C (SIGINT) and call cleanup
trap cleanup SIGINT SIGTERM

echo "Agribot is running. Press Ctrl+C to stop."
# Wait indefinitely so the script doesn't exit
wait $ROS_PID $NPM_PID
