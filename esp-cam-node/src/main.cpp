// ═══════════════════════════════════════════════════════════════════════════
//  AgriBot Camera Node – ESP32-CAM (AI Thinker) Firmware
//  ─────────────────────────────────────────────────────────────────────────
//  Serves:
//      GET /stream         → MJPEG live video stream (multipart)
//      GET /capture        → Single JPEG snapshot
//      GET /health         → JSON health status (uptime, fps, WiFi RSSI)
//      GET /control?flash= → LED flash control (0=off, 1=on, 2=auto)
//      GET /               → Status HTML page with embedded stream preview
//
//  Architecture:
//      - HTTP server on port 81 (avoids conflict with port 80 services)
//      - mDNS hostname: agricam  →  http://agricam.local:81/stream
//      - Uses shared wifi_config.h from repo root (PlatformIO -I../)
//      - FreeRTOS: camera capture & stream on dedicated task
//      - Built-in LED flash on GPIO 4 with configurable control
//      - CORS enabled for cross-origin dashboard embedding
//      - Watchdog: auto-restarts on persistent camera failures
//
//  Dashboard integration:
//      The React dashboard at agribot_web/frontend embeds the stream
//      via an <img src="http://agricam.local:81/stream"> tag. MJPEG
//      multipart streams are natively supported by all modern browsers.
//
//  Build:  PlatformIO  |  Board: esp32cam  |  Framework: Arduino
// ═══════════════════════════════════════════════════════════════════════════

#include <Arduino.h>
#include <WiFi.h>
#include <ESPmDNS.h>
#include <esp_camera.h>
#include <esp_http_server.h>
#include <esp_timer.h>

// ─── WiFi credentials (shared across all AgriBot nodes) ─────────────────
#include "wifi_config.h"

// ═════════════════════════════════════════════════════════════════════════
//  AI Thinker ESP32-CAM Pin Definitions
// ═════════════════════════════════════════════════════════════════════════
#define PWDN_GPIO_NUM     32
#define RESET_GPIO_NUM    -1
#define XCLK_GPIO_NUM      0
#define SIOD_GPIO_NUM     26
#define SIOC_GPIO_NUM     27
#define Y9_GPIO_NUM       35
#define Y8_GPIO_NUM       34
#define Y7_GPIO_NUM       39
#define Y6_GPIO_NUM       36
#define Y5_GPIO_NUM       21
#define Y4_GPIO_NUM       19
#define Y3_GPIO_NUM       18
#define Y2_GPIO_NUM        5
#define VSYNC_GPIO_NUM    25
#define HREF_GPIO_NUM     23
#define PCLK_GPIO_NUM     22

// ─── LED Flash (built-in on AI Thinker board) ───────────────────────────
static const gpio_num_t PIN_FLASH = GPIO_NUM_4;

// ─── Server Configuration ───────────────────────────────────────────────
static const uint16_t SERVER_PORT     = 81;
static const char*    MDNS_HOSTNAME   = "agricam";

// ─── Stream Configuration ───────────────────────────────────────────────
static const uint32_t STREAM_DELAY_MS = 33;     // ~30 FPS target
static const int      JPEG_QUALITY    = 12;      // 0-63, lower = better quality
static const int      FB_COUNT        = 2;       // Frame buffer count (PSRAM)

// ─── Watchdog ───────────────────────────────────────────────────────────
static const uint32_t CAM_FAIL_REBOOT_THRESHOLD = 50;  // consecutive failures

// ═════════════════════════════════════════════════════════════════════════
//  MJPEG Stream Constants
// ═════════════════════════════════════════════════════════════════════════
#define PART_BOUNDARY "agribot_mjpeg_boundary"

static const char* STREAM_CONTENT_TYPE =
    "multipart/x-mixed-replace;boundary=" PART_BOUNDARY;
static const char* STREAM_BOUNDARY =
    "\r\n--" PART_BOUNDARY "\r\n";
static const char* STREAM_PART =
    "Content-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n";

// ═════════════════════════════════════════════════════════════════════════
//  Runtime State
// ═════════════════════════════════════════════════════════════════════════
static httpd_handle_t stream_httpd = NULL;
static httpd_handle_t ctrl_httpd   = NULL;

// Metrics (thread-safe via atomic-like single-word writes on ESP32)
static volatile uint32_t g_frames_served    = 0;
static volatile uint32_t g_active_clients   = 0;
static volatile float    g_current_fps      = 0.0f;
static volatile uint8_t  g_flash_mode       = 0;   // 0=off, 1=on, 2=auto
static volatile uint32_t g_cam_fail_count   = 0;
static uint32_t          g_boot_time_ms     = 0;

// ═════════════════════════════════════════════════════════════════════════
//  Camera Initialisation
// ═════════════════════════════════════════════════════════════════════════
static bool initCamera() {
    camera_config_t config;
    config.ledc_channel = LEDC_CHANNEL_0;
    config.ledc_timer   = LEDC_TIMER_0;

    config.pin_d0       = Y2_GPIO_NUM;
    config.pin_d1       = Y3_GPIO_NUM;
    config.pin_d2       = Y4_GPIO_NUM;
    config.pin_d3       = Y5_GPIO_NUM;
    config.pin_d4       = Y6_GPIO_NUM;
    config.pin_d5       = Y7_GPIO_NUM;
    config.pin_d6       = Y8_GPIO_NUM;
    config.pin_d7       = Y9_GPIO_NUM;
    config.pin_xclk     = XCLK_GPIO_NUM;
    config.pin_pclk     = PCLK_GPIO_NUM;
    config.pin_vsync    = VSYNC_GPIO_NUM;
    config.pin_href     = HREF_GPIO_NUM;
    config.pin_sscb_sda = SIOD_GPIO_NUM;
    config.pin_sscb_scl = SIOC_GPIO_NUM;
    config.pin_pwdn     = PWDN_GPIO_NUM;
    config.pin_reset    = RESET_GPIO_NUM;

    config.xclk_freq_hz = 20000000;         // 20 MHz XCLK
    config.pixel_format = PIXFORMAT_JPEG;    // Direct JPEG from sensor

    // Use PSRAM for higher resolution + double buffering
    if (psramFound()) {
        config.frame_size  = FRAMESIZE_VGA;  // 640x480 – good balance
        config.jpeg_quality = JPEG_QUALITY;
        config.fb_count    = FB_COUNT;
        config.fb_location = CAMERA_FB_IN_PSRAM;
        config.grab_mode   = CAMERA_GRAB_LATEST; // Always grab newest frame
        Serial.println("[cam] PSRAM detected – VGA, double-buffered");
    } else {
        config.frame_size  = FRAMESIZE_CIF;  // 400x296 fallback
        config.jpeg_quality = 15;
        config.fb_count    = 1;
        config.fb_location = CAMERA_FB_IN_DRAM;
        config.grab_mode   = CAMERA_GRAB_WHEN_EMPTY;
        Serial.println("[cam] No PSRAM – CIF, single buffer");
    }

    esp_err_t err = esp_camera_init(&config);
    if (err != ESP_OK) {
        Serial.printf("[cam] Camera init failed: 0x%x\n", err);
        return false;
    }

    // Tune sensor for outdoor / agriculture use
    sensor_t* s = esp_camera_sensor_get();
    if (s) {
        s->set_brightness(s, 1);     // Slightly brighter
        s->set_saturation(s, 0);     // Natural colours
        s->set_whitebal(s, 1);       // Auto white balance ON
        s->set_awb_gain(s, 1);       // AWB gain ON
        s->set_exposure_ctrl(s, 1);  // Auto exposure ON
        s->set_aec2(s, 1);           // DSP auto exposure ON
        s->set_gain_ctrl(s, 1);      // Auto gain ON
        // s->set_vflip(s, 1);       // Uncomment if mounted upside-down
        // s->set_hmirror(s, 1);     // Uncomment if mounted backwards
    }

    Serial.println("[cam] Camera initialised OK");
    return true;
}

// ═════════════════════════════════════════════════════════════════════════
//  Flash LED Control
// ═════════════════════════════════════════════════════════════════════════
static void setFlash(bool on) {
    digitalWrite(PIN_FLASH, on ? HIGH : LOW);
}

static void updateFlashState(bool has_active_stream) {
    switch (g_flash_mode) {
        case 0: setFlash(false); break;          // Off
        case 1: setFlash(true);  break;          // Always on
        case 2: setFlash(has_active_stream); break; // On during stream
        default: setFlash(false); break;
    }
}

// ═════════════════════════════════════════════════════════════════════════
//  HTTP Handlers
// ═════════════════════════════════════════════════════════════════════════

// ─── CORS helper ────────────────────────────────────────────────────────
static void setCorsHeaders(httpd_req_t* req) {
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Methods", "GET, OPTIONS");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Headers", "Content-Type");
}

// ─── GET /stream → MJPEG multipart stream ───────────────────────────────
static esp_err_t streamHandler(httpd_req_t* req) {
    esp_err_t res = ESP_OK;
    camera_fb_t* fb = NULL;
    char part_hdr[64];

    g_active_clients++;
    updateFlashState(true);
    Serial.printf("[cam] Stream client connected (active: %d)\n",
                  g_active_clients);

    res = httpd_resp_set_type(req, STREAM_CONTENT_TYPE);
    if (res != ESP_OK) {
        g_active_clients--;
        updateFlashState(g_active_clients > 0);
        return res;
    }

    setCorsHeaders(req);
    httpd_resp_set_hdr(req, "X-Framerate", "30");
    httpd_resp_set_hdr(req, "Cache-Control", "no-cache, no-store, must-revalidate");

    // FPS tracking
    uint32_t frame_count = 0;
    uint32_t fps_start   = millis();

    while (true) {
        fb = esp_camera_fb_get();
        if (!fb) {
            g_cam_fail_count++;
            Serial.printf("[cam] Capture failed (count: %d)\n",
                          g_cam_fail_count);

            // Watchdog: reboot after too many consecutive failures
            if (g_cam_fail_count >= CAM_FAIL_REBOOT_THRESHOLD) {
                Serial.println("[cam] FATAL: too many capture failures, rebooting");
                delay(1000);
                ESP.restart();
            }
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }

        g_cam_fail_count = 0;  // Reset on success

        // Build MJPEG part header
        size_t hlen = snprintf(part_hdr, sizeof(part_hdr),
                               STREAM_PART, fb->len);

        // Send boundary
        res = httpd_resp_send_chunk(req, STREAM_BOUNDARY,
                                    strlen(STREAM_BOUNDARY));
        if (res == ESP_OK) {
            // Send part header
            res = httpd_resp_send_chunk(req, part_hdr, hlen);
        }
        if (res == ESP_OK) {
            // Send JPEG data
            res = httpd_resp_send_chunk(req, (const char*)fb->buf,
                                        fb->len);
        }

        esp_camera_fb_return(fb);
        fb = NULL;

        if (res != ESP_OK) {
            break;  // Client disconnected
        }

        // Update metrics
        g_frames_served++;
        frame_count++;

        uint32_t elapsed = millis() - fps_start;
        if (elapsed >= 1000) {
            g_current_fps = (float)frame_count * 1000.0f / (float)elapsed;
            frame_count = 0;
            fps_start = millis();
        }

        // Frame rate limiter
        vTaskDelay(pdMS_TO_TICKS(STREAM_DELAY_MS));
    }

    g_active_clients--;
    updateFlashState(g_active_clients > 0);
    Serial.printf("[cam] Stream client disconnected (active: %d)\n",
                  g_active_clients);
    return res;
}

// ─── GET /capture → Single JPEG snapshot ────────────────────────────────
static esp_err_t captureHandler(httpd_req_t* req) {
    setCorsHeaders(req);

    camera_fb_t* fb = esp_camera_fb_get();
    if (!fb) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    httpd_resp_set_type(req, "image/jpeg");
    httpd_resp_set_hdr(req, "Content-Disposition",
                       "inline; filename=agribot_snapshot.jpg");
    httpd_resp_set_hdr(req, "Cache-Control", "no-cache");

    esp_err_t res = httpd_resp_send(req, (const char*)fb->buf, fb->len);
    esp_camera_fb_return(fb);

    return res;
}

// ─── GET /health → JSON health/status ───────────────────────────────────
static esp_err_t healthHandler(httpd_req_t* req) {
    setCorsHeaders(req);

    uint32_t uptime_s = (millis() - g_boot_time_ms) / 1000;
    uint32_t heap_free = esp_get_free_heap_size();
    int8_t   rssi      = WiFi.RSSI();

    char json[384];
    snprintf(json, sizeof(json),
        "{"
            "\"node\":\"agricam\","
            "\"status\":\"online\","
            "\"uptime_s\":%u,"
            "\"fps\":%.1f,"
            "\"frames_served\":%u,"
            "\"active_clients\":%u,"
            "\"flash_mode\":%u,"
            "\"wifi_rssi\":%d,"
            "\"wifi_ssid\":\"%s\","
            "\"ip\":\"%s\","
            "\"heap_free\":%u,"
            "\"psram\":%s"
        "}",
        uptime_s,
        g_current_fps,
        g_frames_served,
        g_active_clients,
        g_flash_mode,
        rssi,
        WiFi.SSID().c_str(),
        WiFi.localIP().toString().c_str(),
        heap_free,
        psramFound() ? "true" : "false"
    );

    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, json, strlen(json));
}

// ─── GET /control?flash=0|1|2 → LED flash control ──────────────────────
static esp_err_t controlHandler(httpd_req_t* req) {
    setCorsHeaders(req);

    // Parse query string
    size_t query_len = httpd_req_get_url_query_len(req) + 1;
    if (query_len > 1) {
        char query[64];
        if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK) {
            char val[4];
            if (httpd_query_key_value(query, "flash", val, sizeof(val)) == ESP_OK) {
                int mode = atoi(val);
                if (mode >= 0 && mode <= 2) {
                    g_flash_mode = (uint8_t)mode;
                    updateFlashState(g_active_clients > 0);
                    Serial.printf("[cam] Flash mode set to %d\n", mode);
                }
            }
        }
    }

    // Respond with current state
    char json[128];
    snprintf(json, sizeof(json),
        "{\"flash_mode\":%u,\"flash_labels\":[\"off\",\"on\",\"auto\"]}",
        g_flash_mode);

    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, json, strlen(json));
}

// ─── GET / → Status landing page ────────────────────────────────────────
static esp_err_t indexHandler(httpd_req_t* req) {
    setCorsHeaders(req);

    const char html[] =
        "<!DOCTYPE html>"
        "<html><head>"
        "<meta charset='UTF-8'>"
        "<meta name='viewport' content='width=device-width,initial-scale=1'>"
        "<title>AgriBot Camera</title>"
        "<style>"
        "body{font-family:system-ui,sans-serif;background:#0f1a0f;color:#d4e8d4;"
        "margin:0;padding:20px;display:flex;flex-direction:column;align-items:center}"
        "h1{color:#6ee76e;margin-bottom:4px}"
        ".sub{color:#8ab88a;font-size:14px;margin-bottom:20px}"
        "img{width:100%;max-width:640px;border-radius:12px;"
        "border:2px solid #2a4a2a;background:#000}"
        ".info{margin-top:16px;font-size:13px;color:#8ab88a;"
        "display:flex;gap:20px;flex-wrap:wrap;justify-content:center}"
        ".info span{background:#1a2e1a;padding:4px 12px;border-radius:6px}"
        "a{color:#6ee76e}"
        "</style>"
        "</head><body>"
        "<h1>AgriBot Camera Node</h1>"
        "<p class='sub'>AI Thinker ESP32-CAM &mdash; MJPEG Stream</p>"
        "<img src='/stream' alt='Live Feed'>"
        "<div class='info'>"
        "<span>Stream: <a href='/stream'>/stream</a></span>"
        "<span>Snapshot: <a href='/capture'>/capture</a></span>"
        "<span>Health: <a href='/health'>/health</a></span>"
        "<span>Control: <a href='/control?flash=0'>/control</a></span>"
        "</div>"
        "</body></html>";

    httpd_resp_set_type(req, "text/html");
    return httpd_resp_send(req, html, strlen(html));
}

// ═════════════════════════════════════════════════════════════════════════
//  HTTP Server Start
// ═════════════════════════════════════════════════════════════════════════
static bool startServers() {
    // ── Stream server (needs larger stack for MJPEG) ────────────────
    httpd_config_t stream_config = HTTPD_DEFAULT_CONFIG();
    stream_config.server_port    = SERVER_PORT;
    stream_config.ctrl_port      = 32769;
    stream_config.max_uri_handlers = 8;
    stream_config.stack_size     = 8192;

    Serial.printf("[cam] Starting HTTP server on port %d\n", SERVER_PORT);

    if (httpd_start(&stream_httpd, &stream_config) != ESP_OK) {
        Serial.println("[cam] Failed to start HTTP server");
        return false;
    }

    // Register URI handlers
    httpd_uri_t uri_stream = {
        .uri      = "/stream",
        .method   = HTTP_GET,
        .handler  = streamHandler,
        .user_ctx = NULL
    };

    httpd_uri_t uri_capture = {
        .uri      = "/capture",
        .method   = HTTP_GET,
        .handler  = captureHandler,
        .user_ctx = NULL
    };

    httpd_uri_t uri_health = {
        .uri      = "/health",
        .method   = HTTP_GET,
        .handler  = healthHandler,
        .user_ctx = NULL
    };

    httpd_uri_t uri_control = {
        .uri      = "/control",
        .method   = HTTP_GET,
        .handler  = controlHandler,
        .user_ctx = NULL
    };

    httpd_uri_t uri_index = {
        .uri      = "/",
        .method   = HTTP_GET,
        .handler  = indexHandler,
        .user_ctx = NULL
    };

    httpd_register_uri_handler(stream_httpd, &uri_stream);
    httpd_register_uri_handler(stream_httpd, &uri_capture);
    httpd_register_uri_handler(stream_httpd, &uri_health);
    httpd_register_uri_handler(stream_httpd, &uri_control);
    httpd_register_uri_handler(stream_httpd, &uri_index);

    Serial.println("[cam] All HTTP handlers registered");
    return true;
}

// ═════════════════════════════════════════════════════════════════════════
//  WiFi Connection
// ═════════════════════════════════════════════════════════════════════════
static bool connectWiFi() {
    WiFi.mode(WIFI_STA);
    WiFi.setSleep(false);       // Disable modem sleep for stable streaming
    WiFi.begin(WIFI_SSID, WIFI_PASS);

    Serial.print("[cam] WiFi connecting");
    uint32_t t0 = millis();
    while (WiFi.status() != WL_CONNECTED) {
        if (millis() - t0 > 20000) {
            Serial.println("\n[cam] WiFi connection timeout");
            return false;
        }
        delay(500);
        Serial.print('.');
    }

    Serial.printf("\n[cam] WiFi connected  IP: %s  RSSI: %d dBm\n",
                  WiFi.localIP().toString().c_str(),
                  WiFi.RSSI());
    return true;
}

// ═════════════════════════════════════════════════════════════════════════
//  mDNS Setup
// ═════════════════════════════════════════════════════════════════════════
static bool setupMDNS() {
    if (!MDNS.begin(MDNS_HOSTNAME)) {
        Serial.println("[cam] mDNS init failed");
        return false;
    }

    // Advertise HTTP service so dashboard can auto-discover
    MDNS.addService("http", "tcp", SERVER_PORT);
    MDNS.addServiceTxt("http", "tcp", "type", "agribot-cam");
    MDNS.addServiceTxt("http", "tcp", "stream", "/stream");

    Serial.printf("[cam] mDNS: http://%s.local:%d\n",
                  MDNS_HOSTNAME, SERVER_PORT);
    return true;
}

// ═════════════════════════════════════════════════════════════════════════
//  WiFi Reconnection Task (runs on Core 0)
// ═════════════════════════════════════════════════════════════════════════
static void wifiWatchdogTask(void* param) {
    (void)param;
    for (;;) {
        if (WiFi.status() != WL_CONNECTED) {
            Serial.println("[cam] WiFi lost – attempting reconnect...");

            // Blink flash LED to indicate connection issue
            for (int i = 0; i < 3; i++) {
                setFlash(true);  delay(100);
                setFlash(false); delay(100);
            }

            WiFi.reconnect();

            // Wait up to 15 seconds for reconnection
            uint32_t t0 = millis();
            while (WiFi.status() != WL_CONNECTED && millis() - t0 < 15000) {
                delay(500);
            }

            if (WiFi.status() == WL_CONNECTED) {
                Serial.printf("[cam] WiFi reconnected  IP: %s\n",
                              WiFi.localIP().toString().c_str());
                // Re-register mDNS after reconnect
                MDNS.end();
                setupMDNS();
            } else {
                Serial.println("[cam] WiFi reconnect failed – will retry in 10s");
            }
        }
        vTaskDelay(pdMS_TO_TICKS(10000));   // Check every 10 seconds
    }
}

// ═════════════════════════════════════════════════════════════════════════
//  Arduino Setup
// ═════════════════════════════════════════════════════════════════════════
void setup() {
    g_boot_time_ms = millis();
    Serial.begin(115200);
    Serial.setDebugOutput(true);
    delay(500);

    Serial.println();
    Serial.println("╔══════════════════════════════════════════════╗");
    Serial.println("║      AgriBot Camera Node – ESP32-CAM        ║");
    Serial.println("╚══════════════════════════════════════════════╝");

    // Initialise flash LED (off by default)
    pinMode(PIN_FLASH, OUTPUT);
    setFlash(false);

    // 1. Initialise camera
    if (!initCamera()) {
        Serial.println("[cam] FATAL: Camera init failed, rebooting in 5s");
        delay(5000);
        ESP.restart();
    }

    // 2. Connect to WiFi
    if (!connectWiFi()) {
        Serial.println("[cam] FATAL: WiFi failed, rebooting in 5s");
        delay(5000);
        ESP.restart();
    }

    // 3. Start mDNS
    setupMDNS();

    // 4. Start HTTP server
    if (!startServers()) {
        Serial.println("[cam] FATAL: HTTP server failed, rebooting in 5s");
        delay(5000);
        ESP.restart();
    }

    // 5. Launch WiFi watchdog on Core 0
    xTaskCreatePinnedToCore(
        wifiWatchdogTask, "wifi_wd", 4096,
        NULL, 1, NULL, 0   // Core 0 (shared with WiFi stack)
    );

    // ── Ready! ──────────────────────────────────────────────────────
    Serial.println();
    Serial.println("[cam] ════════════════════════════════════════════");
    Serial.printf( "[cam]  Stream:   http://%s.local:%d/stream\n",
                   MDNS_HOSTNAME, SERVER_PORT);
    Serial.printf( "[cam]  Stream:   http://%s:%d/stream\n",
                   WiFi.localIP().toString().c_str(), SERVER_PORT);
    Serial.printf( "[cam]  Snapshot: http://%s.local:%d/capture\n",
                   MDNS_HOSTNAME, SERVER_PORT);
    Serial.printf( "[cam]  Health:   http://%s.local:%d/health\n",
                   MDNS_HOSTNAME, SERVER_PORT);
    Serial.println("[cam] ════════════════════════════════════════════");
    Serial.println("[cam] Ready – open Dashboard or stream URL above");
    Serial.println();
}

// ═════════════════════════════════════════════════════════════════════════
//  Arduino Loop
// ═════════════════════════════════════════════════════════════════════════
void loop() {
    // Everything runs via HTTP server callbacks and FreeRTOS tasks.
    // Keep the Arduino loop idle with a long delay to save power.
    vTaskDelay(pdMS_TO_TICKS(10000));
}
