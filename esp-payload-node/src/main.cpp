#define BLUE_LED 2
// ═══════════════════════════════════════════════════════════════════════════
//  AgriBot Payload Node – ESP32 micro-ROS Firmware
//  ─────────────────────────────────────────────────────────────────────────
//  Publishes:   /sensor/dht11/temperature   (sensor_msgs/Temperature)
//               /sensor/dht11/humidity      (sensor_msgs/RelativeHumidity)
//  Subscribes:  /cmd_servo                  (std_msgs/Int32)
//  Service:     /srv/read_moisture          (std_srvs/Trigger → float in message)
//
//  FreeRTOS task pinning:
//      Core 0 → micro-ROS agent spin (shares core with WiFi stack)
//      Core 1 → DHT11 reads, servo control, hardware I/O
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

#include <sensor_msgs/msg/temperature.h>
#include <sensor_msgs/msg/relative_humidity.h>
#include <std_msgs/msg/int32.h>
#include <std_srvs/srv/trigger.h>

#include <DHT.h>
#include <ESP32Servo.h>

// ─── WiFi credentials (shared across all nodes) ─────────────────────────
#include "wifi_config.h"

// ─── micro-ROS agent mDNS hostname ──────────────────────────────────────
static const char* AGENT_MDNS_HOST = "agripi";
static const uint16_t AGENT_PORT   = 8888;

// ─── DHT11 ───────────────────────────────────────────────────────────────
static const gpio_num_t PIN_DHT    = GPIO_NUM_4;
static const uint8_t    DHT_TYPE   = DHT11;

// ─── Soil Moisture (ADC1 – safe for WiFi coexistence) ────────────────────
static const gpio_num_t PIN_SOIL   = GPIO_NUM_34;   // ADC1_CH6

// ─── Mini Servo ──────────────────────────────────────────────────────────
static const gpio_num_t PIN_SERVO  = GPIO_NUM_13;
static const int SERVO_FREQ_HZ     = 50;
static const int SERVO_MIN_US      = 500;
static const int SERVO_MAX_US      = 2400;

// ─── Reserved Switches (digital outputs, LOW on boot, no ROS 2) ─────────
static const gpio_num_t PIN_SW1    = GPIO_NUM_21;
static const gpio_num_t PIN_SW2    = GPIO_NUM_22;
static const gpio_num_t PIN_SW3    = GPIO_NUM_23;

// ─── DHT read interval ──────────────────────────────────────────────────
static const uint32_t DHT_INTERVAL_MS = 2000;

// ═════════════════════════════════════════════════════════════════════════
//  Forward declarations
// ═════════════════════════════════════════════════════════════════════════
class PayloadNode;

// ═════════════════════════════════════════════════════════════════════════
//  PayloadNode – orchestrates micro-ROS, DHT11, servo, soil moisture
// ═════════════════════════════════════════════════════════════════════════
class PayloadNode {
public:
    PayloadNode()
        : dht_(PIN_DHT, DHT_TYPE),
          agent_ip_{},
          last_temperature_(NAN),
          last_humidity_(NAN),
          target_servo_angle_(90),
          servo_needs_update_(false) {}

    // ── Startup sequence ─────────────────────────────────────────────
    bool begin() {
        Serial.begin(115200);
    pinMode(BLUE_LED, OUTPUT);
        delay(500);
        Serial.println("[payload] Booting...");
        ros_mutex_ = xSemaphoreCreateMutex();

        // 1. Connect WiFi
        if (!connectWiFi()) return false;

        // 2. Start mDNS for this board
        if (!MDNS.begin("payload")) {
            Serial.println("[payload] mDNS init failed");
            return false;
        }
        Serial.println("[payload] mDNS: payload.local");

        // 3. Resolve agent IP via mDNS
        if (!resolveAgent()) return false;

        // 4. Initialise hardware
        initHardware();

        // 5. Initialise micro-ROS transport & node
        if (!initMicroROS()) return false;

        return true;
    }

    // ── FreeRTOS task: micro-ROS spin on Core 0 ─────────────────────
    static void rosSpinTask(void* param) {
        PayloadNode* self = static_cast<PayloadNode*>(param);
        for (;;) {
            if (xSemaphoreTake(self->ros_mutex_, pdMS_TO_TICKS(50))) {
                rclc_executor_spin_some(&self->executor_, RCL_MS_TO_NS(10));
                xSemaphoreGive(self->ros_mutex_);
            }
            vTaskDelay(pdMS_TO_TICKS(5));
        }
    }

    // ── FreeRTOS task: hardware control on Core 1 ───────────────────
    static void hardwareTask(void* param) {
        PayloadNode* self = static_cast<PayloadNode*>(param);
        TickType_t last_dht_read = 0;

        for (;;) {
            TickType_t now = xTaskGetTickCount();

            // --- DHT11 read every 2 seconds ---
            if ((now - last_dht_read) >= pdMS_TO_TICKS(DHT_INTERVAL_MS)) {
                last_dht_read = now;

                float t = self->dht_.readTemperature();
                float h = self->dht_.readHumidity();

                if (isnan(t) || isnan(h)) {
                    Serial.println("[payload] DHT read failed (NaN)!");
                } else {
                    self->last_temperature_ = t;
                    self->fillTemperatureMsg(self->msg_temp_, t);
                    if (xSemaphoreTake(self->ros_mutex_, pdMS_TO_TICKS(50))) {
                        rcl_publish(&self->pub_temperature_, &self->msg_temp_, NULL);
                        xSemaphoreGive(self->ros_mutex_);
                    }

                    self->last_humidity_ = h;
                    self->fillHumidityMsg(self->msg_hum_, h);
                    if (xSemaphoreTake(self->ros_mutex_, pdMS_TO_TICKS(50))) {
                        rcl_publish(&self->pub_humidity_, &self->msg_hum_, NULL);
                        xSemaphoreGive(self->ros_mutex_);
                    }
                }
            }

            // --- Servo update (when new angle received) ---
            if (self->servo_needs_update_) {
                int angle = self->target_servo_angle_;
                angle = constrain(angle, 0, 180);
                self->servo_.write(angle);
                self->servo_needs_update_ = false;
                Serial.printf("[payload] Servo → %d°\n", angle);
            }

            vTaskDelay(pdMS_TO_TICKS(20));
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
    SemaphoreHandle_t ros_mutex_;
    // ── WiFi ─────────────────────────────────────────────────────────
    bool connectWiFi() {
        WiFi.mode(WIFI_STA);
        WiFi.begin(WIFI_SSID, WIFI_PASS);
        Serial.print("[payload] WiFi connecting");
        uint32_t t0 = millis();
        while (WiFi.status() != WL_CONNECTED) {
            if (millis() - t0 > 15000) {
                Serial.println("\n[payload] WiFi timeout");
                return false;
            }
            delay(400);
            Serial.print('.');
        }
        Serial.printf("\n[payload] WiFi OK  IP: %s\n",
                       WiFi.localIP().toString().c_str());
        digitalWrite(BLUE_LED, HIGH);
        return true;
    }

    // ── Resolve agripi.local via mDNS ────────────────────────────────
    bool resolveAgent() {
        Serial.print("[payload] Resolving agripi.local");
        for (int i = 0; i < 30; ++i) {
            IPAddress ip = MDNS.queryHost(AGENT_MDNS_HOST);
            if (ip != IPAddress(0, 0, 0, 0)) {
                agent_ip_ = ip;
                Serial.printf("\n[payload] Agent IP: %s\n",
                               agent_ip_.toString().c_str());
                return true;
            }
            Serial.print('.');
            delay(1000);
        }
        Serial.println("\n[payload] Agent resolve failed");
        return false;
    }

    // ── Hardware init ────────────────────────────────────────────────
    void initHardware() {
        // DHT11
        dht_.begin();

        // Soil moisture – analog input (ADC1)
        analogReadResolution(12);   // 0-4095
        pinMode(PIN_SOIL, INPUT);

        // Servo – hardware PWM at 50 Hz
        servo_.setPeriodHertz(SERVO_FREQ_HZ);
        servo_.attach(PIN_SERVO, SERVO_MIN_US, SERVO_MAX_US);
        servo_.write(90);   // centre position

        // Reserved switches – digital outputs, LOW on boot
        pinMode(PIN_SW1, OUTPUT);  digitalWrite(PIN_SW1, LOW);
        pinMode(PIN_SW2, OUTPUT);  digitalWrite(PIN_SW2, LOW);
        pinMode(PIN_SW3, OUTPUT);  digitalWrite(PIN_SW3, LOW);

        Serial.println("[payload] Hardware initialised");
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
        if (rc != RCL_RET_OK) { Serial.println("[payload] support init fail"); return false; }

        // Node
        rc = rclc_node_init_default(&node_, "payload_node", "agribot",
                                    &support_);
        if (rc != RCL_RET_OK) { Serial.println("[payload] node init fail"); return false; }

        // Publisher: /sensor/dht11/temperature
        rc = rclc_publisher_init_default(
            &pub_temperature_, &node_,
            ROSIDL_GET_MSG_TYPE_SUPPORT(sensor_msgs, msg, Temperature),
            "sensor/dht11/temperature");
        if (rc != RCL_RET_OK) { Serial.println("[payload] pub temp fail"); return false; }

        // Publisher: /sensor/dht11/humidity
        rc = rclc_publisher_init_default(
            &pub_humidity_, &node_,
            ROSIDL_GET_MSG_TYPE_SUPPORT(sensor_msgs, msg, RelativeHumidity),
            "sensor/dht11/humidity");
        if (rc != RCL_RET_OK) { Serial.println("[payload] pub hum fail"); return false; }

        // Subscriber: /cmd_servo
        rc = rclc_subscription_init_default(
            &sub_servo_, &node_,
            ROSIDL_GET_MSG_TYPE_SUPPORT(std_msgs, msg, Int32),
            "cmd_servo");
        if (rc != RCL_RET_OK) { Serial.println("[payload] sub servo fail"); return false; }

        // Subscriber: /cmd_switch
        rc = rclc_subscription_init_default(
            &sub_switch_, &node_,
            ROSIDL_GET_MSG_TYPE_SUPPORT(std_msgs, msg, Int32),
            "cmd_switch");
        if (rc != RCL_RET_OK) { Serial.println("[payload] sub switch fail"); return false; }

        // Service: /srv/read_moisture  (std_srvs/Trigger)
        rc = rclc_service_init_default(
            &srv_moisture_, &node_,
            ROSIDL_GET_SRV_TYPE_SUPPORT(std_srvs, srv, Trigger),
            "srv/read_moisture");
        if (rc != RCL_RET_OK) { Serial.println("[payload] srv moisture fail"); return false; }

        // Executor – 2 subscriptions + 1 service = 3 handles
        rc = rclc_executor_init(&executor_, &support_.context,
                                3, &allocator_);
        if (rc != RCL_RET_OK) { Serial.println("[payload] exec init fail"); return false; }

        rc = rclc_executor_add_subscription(
            &executor_, &sub_servo_, &msg_servo_,
            &PayloadNode::servoCallback, ON_NEW_DATA);
        if (rc != RCL_RET_OK) { Serial.println("[payload] exec sub fail"); return false; }

        rc = rclc_executor_add_subscription(
            &executor_, &sub_switch_, &msg_switch_,
            &PayloadNode::switchCallback, ON_NEW_DATA);
        if (rc != RCL_RET_OK) { Serial.println("[payload] exec switch sub fail"); return false; }

        rc = rclc_executor_add_service(
            &executor_, &srv_moisture_,
            &srv_req_, &srv_res_,
            &PayloadNode::moistureServiceCallback);
        if (rc != RCL_RET_OK) { Serial.println("[payload] exec srv fail"); return false; }

        Serial.println("[payload] micro-ROS ready");
        return true;
    }

    // ── cmd_servo callback (runs on Core 0 via executor) ─────────────
    static void servoCallback(const void* msg_in) {
        const std_msgs__msg__Int32* msg =
            static_cast<const std_msgs__msg__Int32*>(msg_in);

        extern PayloadNode* g_payload_node;
        g_payload_node->target_servo_angle_ = msg->data;
        g_payload_node->servo_needs_update_ = true;
    }

    // ── cmd_switch callback ──────────────────────────────────────────
    static void switchCallback(const void* msg_in) {
        const std_msgs__msg__Int32* msg =
            static_cast<const std_msgs__msg__Int32*>(msg_in);

        int data = msg->data;
        int switch_id = data / 10;
        int state = data % 10;
        
        int pin = -1;
        if (switch_id == 1) pin = PIN_SW1;
        else if (switch_id == 2) pin = PIN_SW2;
        else if (switch_id == 3) pin = PIN_SW3;
        
        if (pin != -1) {
            digitalWrite(pin, state ? HIGH : LOW);
            Serial.printf("[payload] Switch %d -> %d\n", switch_id, state);
        }
    }

    // ── Soil moisture service callback ───────────────────────────────
    static void moistureServiceCallback(const void* req, void* res) {
        (void)req;
        std_srvs__srv__Trigger_Response* response =
            static_cast<std_srvs__srv__Trigger_Response*>(res);

        extern PayloadNode* g_payload_node;

        // Read ADC (GPIO 34 is on ADC1, safe alongside WiFi)
        int raw = analogRead(PIN_SOIL);
        float percentage = ((4095.0f - (float)raw) / 4095.0f) * 100.0f;

        // Format result into the Trigger response message field
        // Static buffer – safe across serialisation boundary
        static char buf[64];
        snprintf(buf, sizeof(buf), "moisture_raw=%d moisture_pct=%.1f", raw, percentage);

        // Populate response
        response->success = true;

        // Assign message string
        response->message.data     = buf;
        response->message.size     = strlen(buf);
        response->message.capacity = sizeof(buf);

        Serial.printf("[payload] Soil read: raw=%d  pct=%.1f%%\n", raw, percentage);
    }

    // ── Fill Temperature message ─────────────────────────────────────
    void fillTemperatureMsg(sensor_msgs__msg__Temperature& msg, float temp_c) {
        msg.temperature = (double)temp_c;
        msg.variance    = 0.5;   // DHT11 ±2 °C typical

        static char frame[] = "dht11_link";
        msg.header.frame_id.data     = frame;
        msg.header.frame_id.size     = strlen(frame);
        msg.header.frame_id.capacity = sizeof(frame);

        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        msg.header.stamp.sec     = (int32_t)ts.tv_sec;
        msg.header.stamp.nanosec = (uint32_t)ts.tv_nsec;
    }

    // ── Fill Humidity message ─────────────────────────────────────────
    void fillHumidityMsg(sensor_msgs__msg__RelativeHumidity& msg, float rh) {
        msg.relative_humidity = (double)(rh / 100.0);   // ROS 2 expects 0-1 range
        msg.variance          = 0.05;                    // DHT11 ±5 % typical

        static char frame[] = "dht11_link";
        msg.header.frame_id.data     = frame;
        msg.header.frame_id.size     = strlen(frame);
        msg.header.frame_id.capacity = sizeof(frame);

        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        msg.header.stamp.sec     = (int32_t)ts.tv_sec;
        msg.header.stamp.nanosec = (uint32_t)ts.tv_nsec;
    }

    // ── Members ──────────────────────────────────────────────────────
    DHT          dht_;
    Servo        servo_;
    IPAddress    agent_ip_;

    volatile float last_temperature_;
    volatile float last_humidity_;
    volatile int   target_servo_angle_;
    volatile bool  servo_needs_update_;

    // micro-ROS handles
    rcl_allocator_t       allocator_;
    rclc_support_t        support_;
    rcl_node_t            node_;
    rcl_publisher_t       pub_temperature_;
    rcl_publisher_t       pub_humidity_;
    rcl_subscription_t    sub_servo_;
    rcl_subscription_t    sub_switch_;
    rcl_service_t         srv_moisture_;
    rclc_executor_t       executor_;

    // Message / service storage
    sensor_msgs__msg__Temperature       msg_temp_;
    sensor_msgs__msg__RelativeHumidity  msg_hum_;
    std_msgs__msg__Int32                msg_servo_;
    std_msgs__msg__Int32                msg_switch_;
    std_srvs__srv__Trigger_Request      srv_req_;
    std_srvs__srv__Trigger_Response     srv_res_;
};

// ═════════════════════════════════════════════════════════════════════════
//  Global singleton (required for static callback access)
// ═════════════════════════════════════════════════════════════════════════
PayloadNode* g_payload_node = nullptr;

// ═════════════════════════════════════════════════════════════════════════
//  Arduino entry points
// ═════════════════════════════════════════════════════════════════════════
void setup() {
    static PayloadNode node;
    g_payload_node = &node;

    if (!node.begin()) {
        Serial.println("[payload] FATAL – init failed, rebooting in 5 s");
        delay(5000);
        ESP.restart();
    }
    node.launch();
    Serial.println("[payload] FreeRTOS tasks launched – setup() complete");
}

void loop() {
    // All work handled by FreeRTOS tasks; keep Arduino loop idle.
    vTaskDelay(pdMS_TO_TICKS(1000));
}
