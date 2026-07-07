#include <Arduino.h>
#include <unity.h>

void setUp(void) {}
void tearDown(void) {}

void test_sonar_details(void) {
    gpio_num_t trig = GPIO_NUM_16;
    gpio_num_t echo = GPIO_NUM_17;
    pinMode(trig, OUTPUT);
    pinMode(echo, INPUT);
    digitalWrite(trig, LOW);
    
    // warm up
    delay(50);
    
    digitalWrite(trig, LOW);
    delayMicroseconds(2);
    digitalWrite(trig, HIGH);
    delayMicroseconds(10);
    digitalWrite(trig, LOW);
    
    long duration = pulseIn(echo, HIGH, 30000);
    
    char buf[128];
    snprintf(buf, sizeof(buf), "Duration measured: %ld us", duration);
    TEST_MESSAGE(buf);
    
    TEST_ASSERT_GREATER_THAN_INT32(0, duration);
}

void setup() {
    delay(2000);
    UNITY_BEGIN();
    RUN_TEST(test_sonar_details);
    UNITY_END();
}

void loop() {
}
