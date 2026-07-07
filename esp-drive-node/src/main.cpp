#define BLUE_LED 2
// ═══════════════════════════════════════════════════════════════════════════
//  AgriBot Drive Node – ESP32 micro-ROS Firmware
//  ─────────────────────────────────────────────────────────────────────────
//  Subscribes:  /cmd_vel          (geometry_msgs/Twist)
//  Publishes:   /sensor/sonar_front (sensor_msgs/Range)
//               /sensor/sonar_rear  (sensor_msgs/Range)
//
//  FreeRTOS task pinning:
//      Core 0 → micro-ROS agent spin (shares core with WiFi stack)
//      Core 1 → Motor control, sonar reads, watchdog
//
//  Build:  PlatformIO  |  Board: esp32dev  |  ROS 2 Humble
// ═══════════════════════════════════════════════════════════════════════════

#include <Arduino.h>
#include <ESPmDNS.h>
#include <WiFi.h>

#include <micro_ros_platformio.h>
#include <rcl/rcl.h>
#include <rcl/error_handling.h>
#include <rclc/rclc.h>
#include <rclc/executor.h>

#include <geometry_msgs/msg/twist.h>
#include <sensor_msgs/msg/range.h>

// ─── WiFi credentials (shared across all nodes) ─────────────────────────
#include "wifi_config.h"

// ─── micro-ROS agent mDNS hostname ──────────────────────────────────────
static const char* AGENT_MDNS_HOST = "agripi";
static const uint16_t AGENT_PORT   = 8888;

// ─── Motor A (Left) – L298N ─────────────────────────────────────────────
static const gpio_num_t PIN_ENA = GPIO_NUM_14;   // PWM
static const gpio_num_t PIN_IN1 = GPIO_NUM_27;
static const gpio_num_t PIN_IN2 = GPIO_NUM_26;

// ─── Motor B (Right) – L298N ────────────────────────────────────────────
static const gpio_num_t PIN_ENB = GPIO_NUM_32;   // PWM
static const gpio_num_t PIN_IN3 = GPIO_NUM_25;
static const gpio_num_t PIN_IN4 = GPIO_NUM_33;

// ─── Sonar Front – HC-SR04 ──────────────────────────────────────────────
static const gpio_num_t PIN_TRIG_FRONT = GPIO_NUM_16;
static const gpio_num_t PIN_ECHO_FRONT = GPIO_NUM_17;

// ─── Sonar Rear – HC-SR04 ───────────────────────────────────────────────
static const gpio_num_t PIN_TRIG_REAR = GPIO_NUM_18;
static const gpio_num_t PIN_ECHO_REAR = GPIO_NUM_19;

// ─── PWM configuration (LEDC) ───────────────────────────────────────────
static const uint8_t  PWM_CHANNEL_A   = 0;
static const uint8_t  PWM_CHANNEL_B   = 1;
static const uint32_t PWM_FREQ         = 1000;   // 1 kHz
static const uint8_t  PWM_RESOLUTION   = 8;      // 0-255

// ─── Differential drive parameters ──────────────────────────────────────
static const float WHEEL_BASE     = 0.20f;   // metres between wheel centres
static const float MAX_LINEAR_VEL = 1.0f;    // m/s mapped to PWM 255
static const int   PWM_MAX        = 255;
static const int   PWM_MIN_MOVE   = 40;      // dead-band floor

// ─── Watchdog timeout ────────────────────────────────────────────────────
static const uint32_t CMD_VEL_TIMEOUT_MS = 500;

// ─── Sonar parameters ───────────────────────────────────────────────────
static const float SONAR_MIN_RANGE = 0.02f;   // metres
static const float SONAR_MAX_RANGE = 4.00f;
static const float SONAR_FOV       = 0.2618f; // ~15 deg

// ═════════════════════════════════════════════════════════════════════════
//  Forward declarations
// ═════════════════════════════════════════════════════════════════════════
class MotorDriver;
class SonarSensor;
class DriveNode;

// ═════════════════════════════════════════════════════════════════════════
//  MotorDriver – wraps a single L298N half-bridge
// ═════════════════════════════════════════════════════════════════════════
class MotorDriver {
public:
    MotorDriver(gpio_num_t en, gpio_num_t in_fwd, gpio_num_t in_rev,
                uint8_t pwm_ch)
        : pin_en_(en), pin_fwd_(in_fwd), pin_rev_(in_rev), pwm_ch_(pwm_ch) {}

    void begin() {
        pinMode(pin_fwd_, OUTPUT);
        pinMode(pin_rev_, OUTPUT);
        ledcSetup(pwm_ch_, PWM_FREQ, PWM_RESOLUTION);
        ledcAttachPin(pin_en_, pwm_ch_);
        brake();
    }

    /// Set speed in range [-255 .. +255].  Positive = forward.
    void setSpeed(int speed) {
        if (speed > 0) {
            digitalWrite(pin_fwd_, HIGH);
            digitalWrite(pin_rev_, LOW);
        } else if (speed < 0) {
            digitalWrite(pin_fwd_, LOW);
            digitalWrite(pin_rev_, HIGH);
            speed = -speed;
        } else {
            brake();
            return;
        }
        if (speed < PWM_MIN_MOVE) speed = PWM_MIN_MOVE;
        if (speed > PWM_MAX)      speed = PWM_MAX;
        ledcWrite(pwm_ch_, (uint32_t)speed);
    }

    void brake() {
        digitalWrite(pin_fwd_, LOW);
        digitalWrite(pin_rev_, LOW);
        ledcWrite(pwm_ch_, 0);
    }

private:
    gpio_num_t pin_en_, pin_fwd_, pin_rev_;
    uint8_t    pwm_ch_;
};

// ═════════════════════════════════════════════════════════════════════════
//  SonarSensor – non-blocking HC-SR04 read via FreeRTOS task
// ═════════════════════════════════════════════════════════════════════════
class SonarSensor {
public:
    SonarSensor(gpio_num_t trig, gpio_num_t echo)
        : pin_trig_(trig), pin_echo_(echo), distance_m_(-1.0f) {}

    void begin() {
        pinMode(pin_trig_, OUTPUT);
        pinMode(pin_echo_, INPUT);
        digitalWrite(pin_trig_, LOW);
    }

    /// Blocking read – call only from the hardware task on Core 1.
    float readDistanceMetres() {
        // Send 10 µs trigger pulse
        digitalWrite(pin_trig_, LOW);
        delayMicroseconds(2);
        digitalWrite(pin_trig_, HIGH);
        delayMicroseconds(10);
        digitalWrite(pin_trig_, LOW);

        // Measure echo pulse width (timeout 30 ms ≈ ~5 m)
        long duration = pulseIn(pin_echo_, HIGH, 30000);
        if (duration == 0) {
            distance_m_ = -1.0f;   // out of range / no echo
        } else {
            distance_m_ = (duration * 0.000343f) / 2.0f;
        }
        return distance_m_;
    }

    float lastDistance() const { return distance_m_; }

private:
    gpio_num_t pin_trig_, pin_echo_;
    volatile float distance_m_;
};

// ═════════════════════════════════════════════════════════════════════════
//  DriveNode – orchestrates micro-ROS, motors, and sonars
// ═════════════════════════════════════════════════════════════════════════
class DriveNode {
public:
    DriveNode()
        : motor_left_(PIN_ENA, PIN_IN1, PIN_IN2, PWM_CHANNEL_A),
          motor_right_(PIN_ENB, PIN_IN3, PIN_IN4, PWM_CHANNEL_B),
          sonar_front_(PIN_TRIG_FRONT, PIN_ECHO_FRONT),
          sonar_rear_(PIN_TRIG_REAR, PIN_ECHO_REAR),
          agent_ip_{},
          last_cmd_vel_time_(0),
          target_left_speed_(0),
          target_right_speed_(0) {}

    // ── Startup sequence ─────────────────────────────────────────────
    bool begin() {
        Serial.begin(115200);
    pinMode(BLUE_LED, OUTPUT);
        delay(500);
        Serial.println("[drive] Booting...");

        // 1. Connect WiFi
        if (!connectWiFi()) return false;

        // 2. Start mDNS for this board
        if (!MDNS.begin("drive")) {
            Serial.println("[drive] mDNS init failed");
            return false;
        }
        Serial.println("[drive] mDNS: drive.local");

        // 3. Resolve agent IP via mDNS
        if (!resolveAgent()) return false;

        // 4. Initialise hardware
        motor_left_.begin();
        motor_right_.begin();
        sonar_front_.begin();
        sonar_rear_.begin();

        // 5. Initialise micro-ROS transport & node
        if (!initMicroROS()) return false;

        last_cmd_vel_time_ = millis();
        return true;
    }

    // ── FreeRTOS task: micro-ROS spin on Core 0 ─────────────────────
    static void rosSpinTask(void* param) {
        DriveNode* self = static_cast<DriveNode*>(param);
        for (;;) {
            rclc_executor_spin_some(&self->executor_, RCL_MS_TO_NS(10));
            vTaskDelay(pdMS_TO_TICKS(5));
        }
    }

    // ── FreeRTOS task: hardware control on Core 1 ───────────────────
    static void hardwareTask(void* param) {
        DriveNode* self = static_cast<DriveNode*>(param);
        TickType_t last_sonar = xTaskGetTickCount();
        const TickType_t sonar_interval = pdMS_TO_TICKS(100); // 10 Hz

        for (;;) {
            // --- Watchdog: brake if cmd_vel stale ---
            if ((millis() - self->last_cmd_vel_time_) > CMD_VEL_TIMEOUT_MS) {
                self->motor_left_.brake();
                self->motor_right_.brake();
            } else {
                self->motor_left_.setSpeed(self->target_left_speed_);
                self->motor_right_.setSpeed(self->target_right_speed_);
            }

            // --- Sonar reads at ~10 Hz (non-blocking to motor loop) ---
            if ((xTaskGetTickCount() - last_sonar) >= sonar_interval) {
                last_sonar = xTaskGetTickCount();

                float d_front = self->sonar_front_.readDistanceMetres();
                float d_rear  = self->sonar_rear_.readDistanceMetres();

                self->fillRangeMsg(self->msg_sonar_front_, d_front,
                                   "sonar_front_link");
                self->fillRangeMsg(self->msg_sonar_rear_, d_rear,
                                   "sonar_rear_link");

                rcl_publish(&self->pub_sonar_front_,
                            &self->msg_sonar_front_, NULL);
                rcl_publish(&self->pub_sonar_rear_,
                            &self->msg_sonar_rear_, NULL);
            }

            vTaskDelay(pdMS_TO_TICKS(10));
        }
    }

    // ── Launch FreeRTOS tasks ────────────────────────────────────────
    void launch() {
        xTaskCreatePinnedToCore(rosSpinTask,  "ros_spin",  8192,
                                this, 2, NULL, 0);  // Core 0
        xTaskCreatePinnedToCore(hardwareTask,  "hw_ctrl",  4096,
                                this, 3, NULL, 1);  // Core 1
    }

private:
    // ── WiFi ─────────────────────────────────────────────────────────
    bool connectWiFi() {
        WiFi.mode(WIFI_STA);
        WiFi.begin(WIFI_SSID, WIFI_PASS);
        Serial.print("[drive] WiFi connecting");
        uint32_t t0 = millis();
        while (WiFi.status() != WL_CONNECTED) {
            if (millis() - t0 > 15000) {
                Serial.println("\n[drive] WiFi timeout");
                return false;
            }
            delay(400);
            Serial.print('.');
        }
        Serial.printf("\n[drive] WiFi OK  IP: %s\n",
                       WiFi.localIP().toString().c_str());
        digitalWrite(BLUE_LED, HIGH);
        return true;
    }

    // ── Resolve agripi.local via mDNS ────────────────────────────────
    bool resolveAgent() {
        Serial.print("[drive] Resolving agripi.local");
        for (int i = 0; i < 30; ++i) {
            IPAddress ip = MDNS.queryHost(AGENT_MDNS_HOST);
            if (ip != IPAddress(0, 0, 0, 0)) {
                agent_ip_ = ip;
                Serial.printf("\n[drive] Agent IP: %s\n",
                               agent_ip_.toString().c_str());
                return true;
            }
            Serial.print('.');
            delay(1000);
        }
        Serial.println("\n[drive] Agent resolve failed");
        return false;
    }

    // ── micro-ROS initialisation ─────────────────────────────────────
    bool initMicroROS() {
        // WiFi transport
        set_microros_wifi_transports(
            (char*)WIFI_SSID, (char*)WIFI_PASS,
            agent_ip_, AGENT_PORT);

        allocator_ = rcl_get_default_allocator();

        // Support
        rcl_ret_t rc = rclc_support_init(&support_, 0, NULL, &allocator_);
        if (rc != RCL_RET_OK) { Serial.println("[drive] support init fail"); return false; }

        // Node
        rc = rclc_node_init_default(&node_, "drive_node", "agribot",
                                    &support_);
        if (rc != RCL_RET_OK) { Serial.println("[drive] node init fail"); return false; }

        // Subscriber: /cmd_vel
        rc = rclc_subscription_init_default(
            &sub_cmd_vel_, &node_,
            ROSIDL_GET_MSG_TYPE_SUPPORT(geometry_msgs, msg, Twist),
            "cmd_vel");
        if (rc != RCL_RET_OK) { Serial.println("[drive] sub init fail"); return false; }

        // Publisher: /sensor/sonar_front
        rc = rclc_publisher_init_default(
            &pub_sonar_front_, &node_,
            ROSIDL_GET_MSG_TYPE_SUPPORT(sensor_msgs, msg, Range),
            "sensor/sonar_front");
        if (rc != RCL_RET_OK) { Serial.println("[drive] pub front fail"); return false; }

        // Publisher: /sensor/sonar_rear
        rc = rclc_publisher_init_default(
            &pub_sonar_rear_, &node_,
            ROSIDL_GET_MSG_TYPE_SUPPORT(sensor_msgs, msg, Range),
            "sensor/sonar_rear");
        if (rc != RCL_RET_OK) { Serial.println("[drive] pub rear fail"); return false; }

        // Executor – 1 subscription handle
        rc = rclc_executor_init(&executor_, &support_.context,
                                1, &allocator_);
        if (rc != RCL_RET_OK) { Serial.println("[drive] exec init fail"); return false; }

        rc = rclc_executor_add_subscription(
            &executor_, &sub_cmd_vel_, &msg_twist_,
            &DriveNode::cmdVelCallback, ON_NEW_DATA);
        if (rc != RCL_RET_OK) { Serial.println("[drive] exec sub fail"); return false; }

        Serial.println("[drive] micro-ROS ready");
        return true;
    }

    // ── cmd_vel callback (runs on Core 0 via executor) ───────────────
    static void cmdVelCallback(const void* msg_in, void* context) {
        // NOTE: rclc calls this with context == NULL by default.
        //       We recover `this` from the static instance below.
        (void)context;
        const geometry_msgs__msg__Twist* twist =
            static_cast<const geometry_msgs__msg__Twist*>(msg_in);

        // We need access to the singleton; stored in a file-scope ptr.
        extern DriveNode* g_drive_node;

        float lin = twist->linear.x;
        float ang = twist->angular.z;

        // Differential drive: v_left  = lin - ang * (base/2)
        //                     v_right = lin + ang * (base/2)
        float v_left  = lin - ang * (WHEEL_BASE / 2.0f);
        float v_right = lin + ang * (WHEEL_BASE / 2.0f);

        // Map velocity → PWM
        g_drive_node->target_left_speed_  =
            (int)constrain(v_left  / MAX_LINEAR_VEL * PWM_MAX, -PWM_MAX, PWM_MAX);
        g_drive_node->target_right_speed_ =
            (int)constrain(v_right / MAX_LINEAR_VEL * PWM_MAX, -PWM_MAX, PWM_MAX);

        g_drive_node->last_cmd_vel_time_ = millis();
    }

    // ── Fill a Range message ─────────────────────────────────────────
    void fillRangeMsg(sensor_msgs__msg__Range& msg, float distance,
                      const char* frame_id) {
        msg.radiation_type = sensor_msgs__msg__Range__ULTRASOUND;
        msg.field_of_view  = SONAR_FOV;
        msg.min_range      = SONAR_MIN_RANGE;
        msg.max_range      = SONAR_MAX_RANGE;
        msg.range          = (distance < 0) ? SONAR_MAX_RANGE + 1.0f
                                            : distance;

        // Frame ID – use static storage per topic to avoid alloc churn
        msg.header.frame_id.data    = const_cast<char*>(frame_id);
        msg.header.frame_id.size    = strlen(frame_id);
        msg.header.frame_id.capacity = strlen(frame_id) + 1;

        // Timestamp from micro-ROS synced clock (best-effort)
        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        msg.header.stamp.sec     = (int32_t)ts.tv_sec;
        msg.header.stamp.nanosec = (uint32_t)ts.tv_nsec;
    }

    // ── Members ──────────────────────────────────────────────────────
    MotorDriver  motor_left_;
    MotorDriver  motor_right_;
    SonarSensor  sonar_front_;
    SonarSensor  sonar_rear_;

    IPAddress    agent_ip_;
    volatile uint32_t last_cmd_vel_time_;
    volatile int target_left_speed_;
    volatile int target_right_speed_;

    // micro-ROS handles
    rcl_allocator_t       allocator_;
    rclc_support_t        support_;
    rcl_node_t            node_;
    rcl_subscription_t    sub_cmd_vel_;
    rcl_publisher_t       pub_sonar_front_;
    rcl_publisher_t       pub_sonar_rear_;
    rclc_executor_t       executor_;

    // Message storage
    geometry_msgs__msg__Twist  msg_twist_;
    sensor_msgs__msg__Range    msg_sonar_front_;
    sensor_msgs__msg__Range    msg_sonar_rear_;
};

// ═════════════════════════════════════════════════════════════════════════
//  Global singleton (required for static callback access)
// ═════════════════════════════════════════════════════════════════════════
DriveNode* g_drive_node = nullptr;

// ═════════════════════════════════════════════════════════════════════════
//  Arduino entry points
// ═════════════════════════════════════════════════════════════════════════
void setup() {
    static DriveNode node;
    g_drive_node = &node;

    if (!node.begin()) {
        Serial.println("[drive] FATAL – init failed, rebooting in 5 s");
        delay(5000);
        ESP.restart();
    }
    node.launch();
    Serial.println("[drive] FreeRTOS tasks launched – setup() complete");
}

void loop() {
    // All work handled by FreeRTOS tasks; keep Arduino loop idle.
    vTaskDelay(pdMS_TO_TICKS(1000));
}
