import rclpy
from rclpy.node import Node
from geometry_msgs.msg import Twist
from sensor_msgs.msg import Range

class ObstacleAvoidanceNode(Node):
    def __init__(self):
        super().__init__('obstacle_avoidance')
        
        self.declare_parameter('stop_distance', 0.25)  # 25 cm
        self.stop_distance = self.get_parameter('stop_distance').value
        
        self.front_dist = 999.0
        self.rear_dist = 999.0
        
        # Subscriptions
        self.sub_teleop = self.create_subscription(
            Twist,
            '/agribot/cmd_vel_teleop',
            self.teleop_cb,
            10
        )
        self.sub_front = self.create_subscription(
            Range,
            '/agribot/sensor/sonar_front',
            self.front_cb,
            10
        )
        self.sub_rear = self.create_subscription(
            Range,
            '/agribot/sensor/sonar_rear',
            self.rear_cb,
            10
        )
        
        # Publisher
        self.pub_cmd_vel = self.create_publisher(
            Twist,
            '/agribot/cmd_vel',
            10
        )
        
        self.get_logger().info(f"Obstacle avoidance active. Stop distance: {self.stop_distance}m")

    def front_cb(self, msg: Range):
        # -1.0 is sent when out of range, mapping it to max_range
        self.front_dist = msg.range if msg.range > 0 else msg.max_range

    def rear_cb(self, msg: Range):
        self.rear_dist = msg.range if msg.range > 0 else msg.max_range

    def teleop_cb(self, msg: Twist):
        safe_msg = Twist()
        safe_msg.linear.x = msg.linear.x
        safe_msg.angular.z = msg.angular.z
        
        # Forward check
        if msg.linear.x > 0.0 and self.front_dist < self.stop_distance:
            self.get_logger().warn(f"Obstacle front ({self.front_dist:.2f}m)! Blocking forward.")
            safe_msg.linear.x = 0.0
            
        # Backward check
        elif msg.linear.x < 0.0 and self.rear_dist < self.stop_distance:
            self.get_logger().warn(f"Obstacle rear ({self.rear_dist:.2f}m)! Blocking backward.")
            safe_msg.linear.x = 0.0
            
        self.pub_cmd_vel.publish(safe_msg)

def main(args=None):
    rclpy.init(args=args)
    node = ObstacleAvoidanceNode()
    try:
        rclpy.spin(node)
    except KeyboardInterrupt:
        pass
    finally:
        node.destroy_node()
        rclpy.shutdown()

if __name__ == '__main__':
    main()
