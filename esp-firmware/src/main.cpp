// =============================================================================
// AgriBot ESP32 WROOM Firmware – micro-ROS + FreeRTOS Dual-Core
//
// Core 0 : Wi-Fi stack + micro-ROS executor (spin, publish, service)
// Core 1 : Hardware control (DHT11 read, motor PWM, servo PWM, watchdog)
//
// Build  : PlatformIO  |  Framework: Arduino  |  ROS 2 Humble (micro-ROS)
// =============================================================================

#include <Arduino.h>
#include <WiFi.h>
#include <ESPmDNS.h>

// ---- micro-ROS ----
#include <micro_ros_arduino.h>
#include <rcl/rcl.h>
#include <rcl/error_handling.h>
#include <rclc/rclc.h>
#include <rclc/executor.h>

// ---- ROS 2 message / service types ----
#include <sensor_msgs/msg/temperature.h>
#include <sensor_msgs/msg/relative_humidity.h>
#include <geometry_msgs/msg/twist.h>
#include <std_msgs/msg/int32.h>
#include <std_srvs/srv/trigger.h>

// ---- DHT sensor library (Adafruit) ----
#include <DHT.h>

// =============================================================================
//  PIN DEFINITIONS
// =============================================================================

// DHT11 temperature & humidity
static constexpr uint8_t PIN_DHT   = 4;
static constexpr uint8_t DHT_TYPE  = DHT11;

// Soil moisture – ADC1 channel (ADC1 is Wi-Fi-safe on ESP32)
static constexpr uint8_t PIN_SOIL  = 34;

// L298N Motor A
static constexpr uint8_t PIN_ENA   = 14;   // PWM enable
static constexpr uint8_t PIN_IN1   = 27;
static constexpr uint8_t PIN_IN2   = 26;

// L298N Motor B
static constexpr uint8_t PIN_ENB   = 32;   // PWM enable
static constexpr uint8_t PIN_IN3   = 25;
static constexpr uint8_t PIN_IN4   = 33;

// Mini servo
static constexpr uint8_t PIN_SERVO = 13;

// Reserved digital-output switches (LOW on boot, not exposed to ROS 2)
static constexpr uint8_t PIN_SW1   = 18;
static constexpr uint8_t PIN_SW2   = 19;
static constexpr uint8_t PIN_SW3   = 23;

// =============================================================================
//  LEDC (hardware PWM) CONFIGURATION
// =============================================================================

// Motors – 1 kHz, 8-bit resolution (duty 0–255)
static constexpr uint8_t  LEDC_CH_MOTOR_A  = 0;
static constexpr uint8_t  LEDC_CH_MOTOR_B  = 1;
static constexpr uint32_t MOTOR_PWM_FREQ   = 1000;
static constexpr uint8_t  MOTOR_PWM_RES    = 8;

// Servo – 50 Hz, 16-bit resolution (duty 0–65 535)
//   Period = 20 ms.  Standard pulse range 1.0 ms – 2.0 ms.
//   0°  → 1.0 ms → duty = (1.0 / 20.0) × 65 536 ≈ 3 277
//   180°→ 2.0 ms → duty = (2.0 / 20.0) × 65 536 ≈ 6 554
static constexpr uint8_t  LEDC_CH_SERVO    = 2;
static constexpr uint32_t SERVO_PWM_FREQ   = 50;
static constexpr uint8_t  SERVO_PWM_RES    = 16;
static constexpr uint32_t SERVO_DUTY_MIN   = 3277;
static constexpr uint32_t SERVO_DUTY_MAX   = 6554;

// =============================================================================
//  NETWORK CONFIGURATION
// =============================================================================

static const char* WIFI_SSID       = "YOUR_SSID";       // ← set before flashing
static const char* WIFI_PASS       = "YOUR_PASSWORD";    // ← set before flashing
static const char* MDNS_HOSTNAME   = "esp";              // → esp.local
static const char* AGENT_MDNS_NAME = "agripi";           // → agripi.local
static constexpr uint16_t AGENT_PORT = 8888;             // micro-ROS agent UDP port

// =============================================================================
//  TIMING CONSTANTS
// =============================================================================

static constexpr uint32_t DHT_READ_INTERVAL_MS   = 60000;  // 1 minute
static constexpr uint32_t CMD_VEL_TIMEOUT_MS     = 500;   // watchdog
static constexpr uint32_t HARDWARE_LOOP_PERIOD_MS = 20;   // 50 Hz control loop

// =============================================================================
//  RETURN-CODE HELPERS
// =============================================================================

#define RCCHECK(fn) do { \
    rcl_ret_t _rc = (fn); \
    if (_rc != RCL_RET_OK) { \
        Serial.printf("[ERR] %s  line %d  rc=%d\n", #fn, __LINE__, (int)_rc); \
    } \
} while (0)

#define RCSOFTCHECK(fn) do { rcl_ret_t _rc = (fn); (void)_rc; } while (0)

// =============================================================================
//  INTER-CORE SHARED STATE  (protected by portMUX spinlock)
// =============================================================================

static portMUX_TYPE g_mux = portMUX_INITIALIZER_UNLOCKED;

struct SharedState {
    // Motor targets  – written on Core 0 (cmd_vel cb), read on Core 1
    float   motor_left_speed   = 0.0f;   // –1.0 … +1.0
    float   motor_right_speed  = 0.0f;   // –1.0 … +1.0
    int64_t cmd_vel_stamp_ms   = 0;      // millis() of last cmd_vel

    // Servo target   – written on Core 0 (cmd_servo cb), read on Core 1
    int32_t servo_angle        = 90;     // 0 … 180

    // DHT readings   – written on Core 1 (sensor read), read on Core 0 (publish)
    float   temperature        = 0.0f;
    float   humidity           = 0.0f;
    bool    dht_data_ready     = false;
};

static SharedState g_shared;

// =============================================================================
//  CLASS : DHTSensor
// =============================================================================

class DHTSensor {
public:
    DHTSensor(uint8_t pin, uint8_t type)
        : dht_(pin, type), last_read_ms_(0) {}

    void begin() {
        dht_.begin();
        Serial.printf("[DHT ] Initialised on GPIO %d\n", PIN_DHT);
    }

    /// Non-blocking periodic read.  Returns true when new data is available.
    bool tryRead() {
        uint32_t now = millis();
        if (now - last_read_ms_ < DHT_READ_INTERVAL_MS) return false;
        last_read_ms_ = now;

        float t = dht_.readTemperature();
        float h = dht_.readHumidity();
        if (isnan(t) || isnan(h)) {
            Serial.println("[DHT ] Read failed (NaN)");
            return false;
        }
        temperature_ = t;
        humidity_    = h;
        return true;
    }

    float temperature() const { return temperature_; }
    float humidity()    const { return humidity_; }

private:
    DHT      dht_;
    uint32_t last_read_ms_;
    float    temperature_ = 0.0f;
    float    humidity_    = 0.0f;
};

// =============================================================================
//  CLASS : SoilMoistureSensor
// =============================================================================

class SoilMoistureSensor {
public:
    explicit SoilMoistureSensor(uint8_t pin) : pin_(pin) {}

    void begin() {
        pinMode(pin_, INPUT);
        Serial.printf("[SOIL] ADC initialised on GPIO %d\n", pin_);
    }

    /// Raw 12-bit ADC value (0–4095).
    int readRaw() const { return analogRead(pin_); }

    /// Moisture percentage 0–100 %.
    /// Convention: high ADC → dry → low %;  low ADC → wet → high %.
    float readPercent() const {
        int raw = analogRead(pin_);
        return 100.0f * (1.0f - static_cast<float>(raw) / 4095.0f);
    }

private:
    uint8_t pin_;
};

// =============================================================================
//  CLASS : L298NMotorDriver  (one H-bridge channel)
// =============================================================================

class L298NMotorDriver {
public:
    L298NMotorDriver(uint8_t en_pin, uint8_t in1_pin, uint8_t in2_pin,
                     uint8_t ledc_ch)
        : en_pin_(en_pin), in1_pin_(in1_pin), in2_pin_(in2_pin),
          ledc_ch_(ledc_ch) {}

    void begin() {
        pinMode(in1_pin_, OUTPUT);
        pinMode(in2_pin_, OUTPUT);
        digitalWrite(in1_pin_, LOW);
        digitalWrite(in2_pin_, LOW);

        ledcSetup(ledc_ch_, MOTOR_PWM_FREQ, MOTOR_PWM_RES);
        ledcAttachPin(en_pin_, ledc_ch_);
        ledcWrite(ledc_ch_, 0);

        Serial.printf("[MTR ] ch%d  EN=%d  IN1=%d  IN2=%d\n",
                      ledc_ch_, en_pin_, in1_pin_, in2_pin_);
    }

    /// Set normalised speed:  –1.0 (full reverse) … +1.0 (full forward).
    void setSpeed(float speed) {
        speed = constrain(speed, -1.0f, 1.0f);
        uint8_t pwm = static_cast<uint8_t>(fabsf(speed) * 255.0f);

        if (speed > 0.01f) {            // forward
            digitalWrite(in1_pin_, HIGH);
            digitalWrite(in2_pin_, LOW);
        } else if (speed < -0.01f) {    // reverse
            digitalWrite(in1_pin_, LOW);
            digitalWrite(in2_pin_, HIGH);
        } else {                        // stop / coast
            digitalWrite(in1_pin_, LOW);
            digitalWrite(in2_pin_, LOW);
            pwm = 0;
        }
        ledcWrite(ledc_ch_, pwm);
    }

    /// Active brake: both inputs LOW, PWM = 0.
    void brake() {
        digitalWrite(in1_pin_, LOW);
        digitalWrite(in2_pin_, LOW);
        ledcWrite(ledc_ch_, 0);
    }

private:
    uint8_t en_pin_;
    uint8_t in1_pin_;
    uint8_t in2_pin_;
    uint8_t ledc_ch_;
};

// =============================================================================
//  CLASS : ServoController  (hardware 50 Hz PWM)
// =============================================================================

class ServoController {
public:
    ServoController(uint8_t pin, uint8_t ledc_ch)
        : pin_(pin), ledc_ch_(ledc_ch) {}

    void begin() {
        ledcSetup(ledc_ch_, SERVO_PWM_FREQ, SERVO_PWM_RES);
        ledcAttachPin(pin_, ledc_ch_);
        setAngle(90);   // centre on boot
        Serial.printf("[SRV ] Initialised on GPIO %d (centred at 90°)\n", pin_);
    }

    /// Set absolute angle 0–180°.
    void setAngle(int angle) {
        angle = constrain(angle, 0, 180);
        uint32_t duty = SERVO_DUTY_MIN +
            static_cast<uint32_t>(
                static_cast<float>(SERVO_DUTY_MAX - SERVO_DUTY_MIN) *
                (static_cast<float>(angle) / 180.0f));
        ledcWrite(ledc_ch_, duty);
    }

private:
    uint8_t pin_;
    uint8_t ledc_ch_;
};

// =============================================================================
//  CLASS : ReservedOutputs
// =============================================================================

class ReservedOutputs {
public:
    void begin() {
        const uint8_t pins[] = {PIN_SW1, PIN_SW2, PIN_SW3};
        for (uint8_t p : pins) {
            pinMode(p, OUTPUT);
            digitalWrite(p, LOW);
        }
        Serial.println("[SW  ] Reserved GPIOs 18, 19, 23 → OUTPUT LOW");
    }
};

// =============================================================================
//  CLASS : MicroROSNode
//
//  Owns every RCL / RCLC object.  Static callbacks write to the SharedState
//  struct so Core 1 can act on the data without touching RCL objects.
// =============================================================================

class MicroROSNode {
public:
    MicroROSNode() = default;

    // Singleton pointer used by static callbacks to invoke member functions.
    static MicroROSNode* instance;

    // ------------------------------------------------------------------
    //  Initialise transport, node, pubs, subs, service, timer, executor
    // ------------------------------------------------------------------
    bool init(const IPAddress& agent_ip) {

        // --- micro-ROS Wi-Fi transport ---
        // set_microros_wifi_transports calls WiFi.begin() internally;
        // because we already connected, ESP32 will stay connected or
        // re-associate very quickly with the same credentials.
        static char ip_buf[20];
        agent_ip.toString().toCharArray(ip_buf, sizeof(ip_buf));

        Serial.printf("[uROS] Setting transport → %s:%d\n", ip_buf, AGENT_PORT);
        set_microros_wifi_transports(
            const_cast<char*>(WIFI_SSID),
            const_cast<char*>(WIFI_PASS),
            ip_buf,
            AGENT_PORT);

        // Brief settle after transport setup
        delay(1000);

        // Re-register mDNS after the internal WiFi.begin() in transport setup
        MDNS.begin(MDNS_HOSTNAME);

        // --- Allocator ---
        allocator_ = rcl_get_default_allocator();

        // --- Support ---
        RCCHECK(rclc_support_init(&support_, 0, NULL, &allocator_));

        // --- Node ---
        RCCHECK(rclc_node_init_default(&node_, "agribot_esp32", "", &support_));

        // ====== Publishers ======

        RCCHECK(rclc_publisher_init_default(
            &pub_temperature_, &node_,
            ROSIDL_GET_MSG_TYPE_SUPPORT(sensor_msgs, msg, Temperature),
            "sensor/dht11_temperature"));

        RCCHECK(rclc_publisher_init_default(
            &pub_humidity_, &node_,
            ROSIDL_GET_MSG_TYPE_SUPPORT(sensor_msgs, msg, RelativeHumidity),
            "sensor/dht11_humidity"));

        // ====== Subscribers ======

        RCCHECK(rclc_subscription_init_default(
            &sub_cmd_vel_, &node_,
            ROSIDL_GET_MSG_TYPE_SUPPORT(geometry_msgs, msg, Twist),
            "cmd_vel"));

        RCCHECK(rclc_subscription_init_default(
            &sub_cmd_servo_, &node_,
            ROSIDL_GET_MSG_TYPE_SUPPORT(std_msgs, msg, Int32),
            "cmd_servo"));

        // ====== Service: soil moisture ======

        RCCHECK(rclc_service_init_default(
            &srv_moisture_, &node_,
            ROSIDL_GET_SRV_TYPE_SUPPORT(std_srvs, srv, Trigger),
            "srv/read_moisture"));

        // Pre-allocate the Trigger response string buffer
        srv_response_.message.data     = static_cast<char*>(malloc(64));
        srv_response_.message.capacity = 64;
        srv_response_.message.size     = 0;

        // ====== Timer: DHT publish ======

        RCCHECK(rclc_timer_init_default(
            &timer_dht_, &support_,
            RCL_MS_TO_NS(DHT_READ_INTERVAL_MS),
            timerDHTCallback));

        // ====== Executor (2 subs + 1 service + 1 timer = 4 handles) ======

        RCCHECK(rclc_executor_init(&executor_, &support_.context,
                                   4, &allocator_));

        RCCHECK(rclc_executor_add_subscription(
            &executor_, &sub_cmd_vel_, &msg_twist_,
            cmdVelCallback, ON_NEW_DATA));

        RCCHECK(rclc_executor_add_subscription(
            &executor_, &sub_cmd_servo_, &msg_servo_,
            cmdServoCallback, ON_NEW_DATA));

        RCCHECK(rclc_executor_add_service(
            &executor_, &srv_moisture_,
            &srv_request_, &srv_response_,
            moistureServiceCallback));

        RCCHECK(rclc_executor_add_timer(&executor_, &timer_dht_));

        Serial.println("[uROS] Node fully initialised  ✔");
        return true;
    }

    // ------------------------------------------------------------------
    //  Spin the executor (call from Core 0 loop)
    // ------------------------------------------------------------------
    void spinOnce(uint32_t timeout_ms = 10) {
        RCSOFTCHECK(rclc_executor_spin_some(
            &executor_, RCL_MS_TO_NS(timeout_ms)));
    }

    // ------------------------------------------------------------------
    //  Publish latest DHT data (called from the timer callback)
    // ------------------------------------------------------------------
    void publishDHT(float temperature, float humidity) {

        // --- Temperature ---
        sensor_msgs__msg__Temperature msg_t;
        memset(&msg_t, 0, sizeof(msg_t));
        msg_t.temperature = static_cast<double>(temperature);
        msg_t.variance    = 0.0;
        msg_t.header.frame_id.data     = const_cast<char*>("dht11_frame");
        msg_t.header.frame_id.size     = strlen("dht11_frame");
        msg_t.header.frame_id.capacity = msg_t.header.frame_id.size + 1;

        RCSOFTCHECK(rcl_publish(&pub_temperature_, &msg_t, NULL));

        // --- Relative Humidity (0.0 – 1.0 per REP-145) ---
        sensor_msgs__msg__RelativeHumidity msg_h;
        memset(&msg_h, 0, sizeof(msg_h));
        msg_h.relative_humidity = static_cast<double>(humidity / 100.0);
        msg_h.variance          = 0.0;
        msg_h.header.frame_id.data     = const_cast<char*>("dht11_frame");
        msg_h.header.frame_id.size     = strlen("dht11_frame");
        msg_h.header.frame_id.capacity = msg_h.header.frame_id.size + 1;

        RCSOFTCHECK(rcl_publish(&pub_humidity_, &msg_h, NULL));
    }

private:
    // ---- RCL / RCLC handles ----
    rcl_allocator_t    allocator_;
    rclc_support_t     support_;
    rcl_node_t         node_;
    rclc_executor_t    executor_;

    rcl_publisher_t    pub_temperature_;
    rcl_publisher_t    pub_humidity_;
    rcl_subscription_t sub_cmd_vel_;
    rcl_subscription_t sub_cmd_servo_;
    rcl_service_t      srv_moisture_;
    rcl_timer_t        timer_dht_;

    // ---- Message / service buffers ----
    geometry_msgs__msg__Twist           msg_twist_;
    std_msgs__msg__Int32                msg_servo_;
    std_srvs__srv__Trigger_Request      srv_request_;
    std_srvs__srv__Trigger_Response     srv_response_;

    // ==================================================================
    //  STATIC CALLBACKS  (fire on Core 0 inside rclc_executor_spin_some)
    // ==================================================================

    // ---- cmd_vel → differential-drive motor targets ----
    static void cmdVelCallback(const void* msg_in) {
        const auto* twist =
            static_cast<const geometry_msgs__msg__Twist*>(msg_in);

        float linear  = static_cast<float>(twist->linear.x);   // m/s
        float angular = static_cast<float>(twist->angular.z);   // rad/s

        // Differential drive mix (normalised to ±1.0)
        float left  = constrain(linear - angular, -1.0f, 1.0f);
        float right = constrain(linear + angular, -1.0f, 1.0f);

        portENTER_CRITICAL(&g_mux);
        g_shared.motor_left_speed  = left;
        g_shared.motor_right_speed = right;
        g_shared.cmd_vel_stamp_ms  = static_cast<int64_t>(millis());
        portEXIT_CRITICAL(&g_mux);
    }

    // ---- cmd_servo → servo angle target ----
    static void cmdServoCallback(const void* msg_in) {
        const auto* msg =
            static_cast<const std_msgs__msg__Int32*>(msg_in);

        portENTER_CRITICAL(&g_mux);
        g_shared.servo_angle = msg->data;
        portEXIT_CRITICAL(&g_mux);
    }

    // ---- srv/read_moisture (Trigger: empty req → success + JSON string) ----
    static void moistureServiceCallback(const void* req, void* res) {
        (void)req;   // Trigger request is empty
        auto* response =
            static_cast<std_srvs__srv__Trigger_Response*>(res);

        // ADC1 on GPIO 34 is safe to read alongside Wi-Fi (only ADC2 conflicts).
        int   raw = analogRead(PIN_SOIL);
        float pct = 100.0f * (1.0f - static_cast<float>(raw) / 4095.0f);

        response->success = true;
        int n = snprintf(response->message.data,
                         response->message.capacity,
                         "{\"raw\":%d,\"percent\":%.1f}", raw, pct);
        response->message.size = (n > 0) ? static_cast<size_t>(n) : 0;

        Serial.printf("[SOIL] Service called → raw=%d  pct=%.1f%%\n", raw, pct);
    }

    // ---- Timer: read shared DHT data and publish ----
    static void timerDHTCallback(rcl_timer_t* timer, int64_t last_call_time) {
        (void)last_call_time;
        if (timer == NULL || instance == NULL) return;

        float t = 0.0f, h = 0.0f;
        bool  ready = false;

        portENTER_CRITICAL(&g_mux);
        ready = g_shared.dht_data_ready;
        if (ready) {
            t = g_shared.temperature;
            h = g_shared.humidity;
            g_shared.dht_data_ready = false;
        }
        portEXIT_CRITICAL(&g_mux);

        if (ready) {
            instance->publishDHT(t, h);
            Serial.printf("[DHT ] Published  T=%.1f°C  H=%.1f%%\n", t, h);
        }
    }
};

// Static member definition
MicroROSNode* MicroROSNode::instance = nullptr;

// =============================================================================
//  GLOBAL OBJECT INSTANCES
// =============================================================================

static DHTSensor          g_dht(PIN_DHT, DHT_TYPE);
static SoilMoistureSensor g_soil(PIN_SOIL);
static L298NMotorDriver   g_motorA(PIN_ENA, PIN_IN1, PIN_IN2, LEDC_CH_MOTOR_A);
static L298NMotorDriver   g_motorB(PIN_ENB, PIN_IN3, PIN_IN4, LEDC_CH_MOTOR_B);
static ServoController    g_servo(PIN_SERVO, LEDC_CH_SERVO);
static ReservedOutputs    g_switches;
static MicroROSNode       g_uros;

// =============================================================================
//  FreeRTOS TASK : Network  (pinned to Core 0)
//
//  Runs the micro-ROS executor alongside the ESP32 Wi-Fi/LwIP stack which
//  is also bound to Core 0 (protocol CPU) by default.
// =============================================================================

void taskNetwork(void* /*param*/) {
    Serial.printf("[TASK] Network  → Core %d\n", xPortGetCoreID());

    for (;;) {
        g_uros.spinOnce(10);
        vTaskDelay(pdMS_TO_TICKS(1));   // yield to Wi-Fi / LwIP
    }
}

// =============================================================================
//  FreeRTOS TASK : Hardware  (pinned to Core 1)
//
//  Reads the DHT11 (timing-critical bit-bang protocol kept away from Wi-Fi
//  ISRs), applies motor PWM, applies servo PWM, and enforces the cmd_vel
//  watchdog.
// =============================================================================

void taskHardware(void* /*param*/) {
    Serial.printf("[TASK] Hardware → Core %d\n", xPortGetCoreID());

    for (;;) {
        uint32_t loop_start = millis();

        // ---- 1. DHT11 non-blocking read (every 2 s) ----
        if (g_dht.tryRead()) {
            portENTER_CRITICAL(&g_mux);
            g_shared.temperature    = g_dht.temperature();
            g_shared.humidity       = g_dht.humidity();
            g_shared.dht_data_ready = true;
            portEXIT_CRITICAL(&g_mux);
        }

        // ---- 2. Fetch motor targets & watchdog stamp ----
        float   left_sp  = 0.0f;
        float   right_sp = 0.0f;
        int64_t stamp    = 0;

        portENTER_CRITICAL(&g_mux);
        left_sp  = g_shared.motor_left_speed;
        right_sp = g_shared.motor_right_speed;
        stamp    = g_shared.cmd_vel_stamp_ms;
        portEXIT_CRITICAL(&g_mux);

        // ---- 3. Watchdog: brake if no cmd_vel for 500 ms ----
        bool timed_out = (stamp > 0) &&
            (static_cast<int64_t>(millis()) - stamp > CMD_VEL_TIMEOUT_MS);

        if (timed_out) {
            g_motorA.brake();
            g_motorB.brake();

            // Clear cached speeds so we don't re-trigger every loop
            portENTER_CRITICAL(&g_mux);
            g_shared.motor_left_speed  = 0.0f;
            g_shared.motor_right_speed = 0.0f;
            portEXIT_CRITICAL(&g_mux);
        } else {
            g_motorA.setSpeed(left_sp);
            g_motorB.setSpeed(right_sp);
        }

        // ---- 4. Servo update ----
        int32_t angle;
        portENTER_CRITICAL(&g_mux);
        angle = g_shared.servo_angle;
        portEXIT_CRITICAL(&g_mux);
        g_servo.setAngle(angle);

        // ---- Fixed-rate 50 Hz loop ----
        uint32_t elapsed = millis() - loop_start;
        if (elapsed < HARDWARE_LOOP_PERIOD_MS) {
            vTaskDelay(pdMS_TO_TICKS(HARDWARE_LOOP_PERIOD_MS - elapsed));
        }
    }
}

// =============================================================================
//  Wi-Fi CONNECT  +  mDNS HOSTNAME BROADCAST  +  AGENT RESOLUTION
// =============================================================================

IPAddress connectAndResolveAgent() {

    // 1. Connect to Wi-Fi
    Serial.printf("[WIFI] Connecting to \"%s\" ", WIFI_SSID);
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASS);

    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }
    Serial.printf("\n[WIFI] Connected – IP %s\n",
                  WiFi.localIP().toString().c_str());

    // 2. Broadcast our own mDNS hostname  →  esp.local
    if (!MDNS.begin(MDNS_HOSTNAME)) {
        Serial.println("[mDNS] ERROR: responder failed to start");
    } else {
        Serial.printf("[mDNS] Broadcasting hostname: %s.local\n", MDNS_HOSTNAME);
    }

    // 3. Resolve the micro-ROS agent  →  agripi.local
    Serial.printf("[mDNS] Resolving %s.local ", AGENT_MDNS_NAME);
    IPAddress agent_ip;
    while (agent_ip.toString() == "0.0.0.0") {
        agent_ip = MDNS.queryHost(AGENT_MDNS_NAME);
        if (agent_ip.toString() == "0.0.0.0") {
            Serial.print(".");
            delay(1000);
        }
    }
    Serial.printf("\n[mDNS] Resolved %s.local → %s\n",
                  AGENT_MDNS_NAME, agent_ip.toString().c_str());

    return agent_ip;
}

// =============================================================================
//  setup()   (runs once on Core 1 – Arduino default)
// =============================================================================

void setup() {
    Serial.begin(115200);
    delay(500);

    Serial.println();
    Serial.println("================================================");
    Serial.println("   AgriBot ESP32 Firmware  v1.0  (micro-ROS)");
    Serial.println("================================================");

    // ---- 1. Reserved outputs (safe first) ----
    g_switches.begin();

    // ---- 2. Hardware peripherals ----
    g_dht.begin();
    g_soil.begin();
    g_motorA.begin();
    g_motorB.begin();
    g_servo.begin();

    // ---- 3. Network: Wi-Fi + mDNS agent resolution ----
    IPAddress agent_ip = connectAndResolveAgent();

    // ---- 4. micro-ROS node ----
    MicroROSNode::instance = &g_uros;
    g_uros.init(agent_ip);

    // ---- 5. Launch dual-core FreeRTOS tasks ----

    // Core 0 – network (micro-ROS executor + Wi-Fi stack)
    xTaskCreatePinnedToCore(
        taskNetwork,        // function
        "uros_net",         // name
        8192,               // stack (bytes)
        NULL,               // param
        2,                  // priority
        NULL,               // handle
        0);                 // ← Core 0

    // Core 1 – hardware (DHT, motors, servo, watchdog)
    xTaskCreatePinnedToCore(
        taskHardware,
        "hw_ctrl",
        4096,
        NULL,
        3,                  // higher prio than network task
        NULL,
        1);                 // ← Core 1

    Serial.println("[BOOT] All tasks launched – entering idle.");
}

// =============================================================================
//  loop()   (Arduino loop on Core 1 – kept idle, work is in FreeRTOS tasks)
// =============================================================================

void loop() {
    vTaskDelay(pdMS_TO_TICKS(1000));
}
