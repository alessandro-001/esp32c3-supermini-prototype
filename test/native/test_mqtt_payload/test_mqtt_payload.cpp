#include <unity.h>
#include <cstdio>
#include <cstring>
#include <cstdint>

// ── Replicate payload builders from mqtt.cpp ──────────────────────────────────

static int buildTelemetryPayload(char* buf, size_t len,
    float temp, float hum,
    bool alertTemp, bool alertHum,
    uint8_t aqi, const char* aqiLabel,
    uint16_t tvoc, uint16_t eco2,
    const char* airStatus)
{
    return snprintf(buf, len,
        "{"
        "\"temperature\":%.2f,"
        "\"humidity\":%.2f,"
        "\"alert_temp\":%s,"
        "\"alert_hum\":%s,"
        "\"aqi\":%d,"
        "\"aqi_label\":\"%s\","
        "\"tvoc\":%d,"
        "\"eco2\":%d,"
        "\"air_quality_status\":\"%s\""
        "}",
        temp, hum,
        alertTemp ? "true" : "false",
        alertHum  ? "true" : "false",
        aqi, aqiLabel, tvoc, eco2, airStatus
    );
}

static int buildAttributePayload(char* buf, size_t len,
    const char* mac, const char* ip, int rssi, const char* firmware,
    float threshTemp, float threshHum, float threshTvoc, float threshEco2)
{
    return snprintf(buf, len,
        "{"
        "\"mac\":\"%s\","
        "\"ip\":\"%s\","
        "\"rssi\":%d,"
        "\"firmware\":\"%s\","
        "\"highTempThreshold\":%.1f,"
        "\"highHumThreshold\":%.1f,"
        "\"highTvocThreshold\":%.0f,"
        "\"highEco2Threshold\":%.0f"
        "}",
        mac, ip, rssi, firmware,
        threshTemp, threshHum, threshTvoc, threshEco2
    );
}

// ── Helpers ───────────────────────────────────────────────────────────────────

static bool containsStr(const char* haystack, const char* needle) {
    return strstr(haystack, needle) != nullptr;
}

// ── Telemetry payload tests ───────────────────────────────────────────────────

void test_telemetry_payload_fits_in_buffer() {
    char buf[256];
    int len = buildTelemetryPayload(buf, sizeof(buf),
        22.50f, 55.00f, false, false,
        2, "Good", 120, 650, "Normal");
    TEST_ASSERT_GREATER_THAN(0, len);
    TEST_ASSERT_LESS_THAN((int)sizeof(buf), len);
}

void test_telemetry_contains_temperature() {
    char buf[256];
    buildTelemetryPayload(buf, sizeof(buf),
        22.50f, 55.00f, false, false,
        2, "Good", 120, 650, "Normal");
    TEST_ASSERT_TRUE(containsStr(buf, "\"temperature\":22.50"));
}

void test_telemetry_contains_humidity() {
    char buf[256];
    buildTelemetryPayload(buf, sizeof(buf),
        22.50f, 55.00f, false, false,
        2, "Good", 120, 650, "Normal");
    TEST_ASSERT_TRUE(containsStr(buf, "\"humidity\":55.00"));
}

void test_telemetry_alert_temp_true() {
    char buf[256];
    buildTelemetryPayload(buf, sizeof(buf),
        35.0f, 55.0f, true, false,
        2, "Good", 120, 650, "Normal");
    TEST_ASSERT_TRUE(containsStr(buf, "\"alert_temp\":true"));
}

void test_telemetry_alert_temp_false() {
    char buf[256];
    buildTelemetryPayload(buf, sizeof(buf),
        22.0f, 55.0f, false, false,
        2, "Good", 120, 650, "Normal");
    TEST_ASSERT_TRUE(containsStr(buf, "\"alert_temp\":false"));
}

void test_telemetry_contains_aqi_label() {
    char buf[256];
    buildTelemetryPayload(buf, sizeof(buf),
        22.0f, 55.0f, false, false,
        1, "Excellent", 80, 420, "Normal");
    TEST_ASSERT_TRUE(containsStr(buf, "\"aqi_label\":\"Excellent\""));
}

void test_telemetry_contains_tvoc_and_eco2() {
    char buf[256];
    buildTelemetryPayload(buf, sizeof(buf),
        22.0f, 55.0f, false, false,
        2, "Good", 300, 900, "Normal");
    TEST_ASSERT_TRUE(containsStr(buf, "\"tvoc\":300"));
    TEST_ASSERT_TRUE(containsStr(buf, "\"eco2\":900"));
}

// ── Attribute payload tests ───────────────────────────────────────────────────

void test_attribute_payload_fits_in_buffer() {
    char buf[384];
    int len = buildAttributePayload(buf, sizeof(buf),
        "AABBCCDDEEFF", "192.168.1.100", -65, "1.0.0",
        30.0f, 80.0f, 500.0f, 1000.0f);
    TEST_ASSERT_GREATER_THAN(0, len);
    TEST_ASSERT_LESS_THAN((int)sizeof(buf), len);
}

void test_attribute_contains_firmware() {
    char buf[384];
    buildAttributePayload(buf, sizeof(buf),
        "AABBCCDDEEFF", "192.168.1.100", -65, "1.0.0",
        30.0f, 80.0f, 500.0f, 1000.0f);
    TEST_ASSERT_TRUE(containsStr(buf, "\"firmware\":\"1.0.0\""));
}

void test_attribute_contains_thresholds() {
    char buf[384];
    buildAttributePayload(buf, sizeof(buf),
        "AABBCCDDEEFF", "192.168.1.100", -65, "1.0.0",
        30.0f, 80.0f, 500.0f, 1000.0f);
    TEST_ASSERT_TRUE(containsStr(buf, "\"highTempThreshold\":30.0"));
    TEST_ASSERT_TRUE(containsStr(buf, "\"highHumThreshold\":80.0"));
    TEST_ASSERT_TRUE(containsStr(buf, "\"highTvocThreshold\":500"));
    TEST_ASSERT_TRUE(containsStr(buf, "\"highEco2Threshold\":1000"));
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_telemetry_payload_fits_in_buffer);
    RUN_TEST(test_telemetry_contains_temperature);
    RUN_TEST(test_telemetry_contains_humidity);
    RUN_TEST(test_telemetry_alert_temp_true);
    RUN_TEST(test_telemetry_alert_temp_false);
    RUN_TEST(test_telemetry_contains_aqi_label);
    RUN_TEST(test_telemetry_contains_tvoc_and_eco2);
    RUN_TEST(test_attribute_payload_fits_in_buffer);
    RUN_TEST(test_attribute_contains_firmware);
    RUN_TEST(test_attribute_contains_thresholds);
    return UNITY_END();
}