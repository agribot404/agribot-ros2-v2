"""
agribot_web.launch.py – Launches all AgriBot web dashboard processes.

Starts:
    1. rosbridge_server       (WebSocket bridge on port 9090)
    2. sensor_logger          (ROS 2 node → SQLite)
    3. web_server             (FastAPI on port 8080)
    4. micro_ros_agent        (Docker container on UDP port 8888)
"""

from launch import LaunchDescription
from launch.actions import ExecuteProcess
from launch_ros.actions import Node


def generate_launch_description():
    return LaunchDescription([
        # 1. rosbridge_server – bridges ROS 2 DDS ↔ WebSocket for the browser
        Node(
            package='rosbridge_server',
            executable='rosbridge_websocket',
            name='rosbridge_websocket',
            output='screen',
            parameters=[{
                'port': 9090,
                'address': '',
                'unregister_timeout': 10.0,
            }],
        ),

        # 2. sensor_logger – subscribes to DHT11 topics, writes to SQLite
        Node(
            package='agribot_web',
            executable='sensor_logger',
            name='sensor_logger',
            output='screen',
        ),

        # 3. web_server – FastAPI (serves frontend + REST API on port 8080)
        ExecuteProcess(
            cmd=['python3', '-m', 'agribot_web.web_server'],
            name='web_server',
            output='screen',
        ),

        # 4. micro_ros_agent – Docker container for ESP32 micro-ROS nodes
        ExecuteProcess(
            cmd=['docker', 'run', '--rm', '--net=host', 'microros/micro-ros-agent:humble', 'udp4', '--port', '8888'],
            name='micro_ros_agent',
            output='screen',
        ),
    ])
