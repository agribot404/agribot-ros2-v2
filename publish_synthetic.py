import rclpy
from rclpy.node import Node
from sensor_msgs.msg import Temperature, RelativeHumidity
import time

def main(args=None):
    rclpy.init(args=args)
    node = rclpy.create_node('synthetic_data_publisher')
    temp_pub = node.create_publisher(Temperature, 'sensor/dht11_temperature', 10)
    hum_pub = node.create_publisher(RelativeHumidity, 'sensor/dht11_humidity', 10)

    for i in range(5):
        temp_msg = Temperature()
        temp_msg.temperature = 25.0 + i
        temp_pub.publish(temp_msg)
        
        hum_msg = RelativeHumidity()
        hum_msg.relative_humidity = 0.5 + (i * 0.05)
        hum_pub.publish(hum_msg)
        
        node.get_logger().info(f'Published: T={temp_msg.temperature}°C, H={hum_msg.relative_humidity*100}%')
        time.sleep(1.0)
        
    node.destroy_node()
    rclpy.shutdown()

if __name__ == '__main__':
    main()