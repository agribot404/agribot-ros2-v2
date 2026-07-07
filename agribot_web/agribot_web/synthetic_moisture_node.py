import rclpy
from rclpy.node import Node
from std_srvs.srv import Trigger
import json
import random

class SyntheticMoistureNode(Node):
    def __init__(self):
        super().__init__('synthetic_moisture_node')
        self.srv = self.create_service(Trigger, '/agribot/srv/read_moisture', self.moisture_callback)
        self.get_logger().info('Synthetic Moisture Node is ready.')

    def moisture_callback(self, request, response):
        # optimal range: percent 40-60, ph 6.0-7.0
        percent = random.uniform(40.0, 60.0)
        raw = int(4095 * (100 - percent) / 100) # mock raw
        ph = random.uniform(6.0, 7.0)
        
        data = {
            "raw": raw,
            "percent": percent,
            "ph": ph
        }
        
        response.success = True
        response.message = json.dumps(data)
        self.get_logger().info(f'Providing synthetic data: {data}')
        return response

def main(args=None):
    rclpy.init(args=args)
    node = SyntheticMoistureNode()
    try:
        rclpy.spin(node)
    except KeyboardInterrupt:
        pass
    finally:
        node.destroy_node()
        rclpy.try_shutdown()

if __name__ == '__main__':
    main()
