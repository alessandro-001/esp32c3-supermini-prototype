#include <unity.h>

// ── Replicate alert logic from web_server.cpp handleSetThresh() ───────────────

static bool calcAlertTemp(float sensorTemp, float threshTemp) {
    return sensorTemp > threshTemp;
}

static bool calcAlertHum(float sensorHum, float threshHum) {
    return sensorHum > threshHum;
}

static bool calcAlertTvoc(uint16_t tvoc, float threshTvoc) {
    return tvoc > threshTvoc;
}

static bool calcAlertEco2(uint16_t eco2, float threshEco2) {
    return eco2 > threshEco2;
}

void test_temp_alert_triggered() {
    TEST_ASSERT_TRUE(calcAlertTemp(31.0f, 30.0f));
}

void test_temp_alert_not_triggered() {
    TEST_ASSERT_FALSE(calcAlertTemp(29.9f, 30.0f));
}

void test_temp_alert_at_exact_threshold() {
    // strictly greater than — equal should NOT trigger
    TEST_ASSERT_FALSE(calcAlertTemp(30.0f, 30.0f));
}

void test_hum_alert_triggered() {
    TEST_ASSERT_TRUE(calcAlertHum(81.0f, 80.0f));
}

void test_hum_alert_not_triggered() {
    TEST_ASSERT_FALSE(calcAlertHum(79.9f, 80.0f));
}

void test_hum_alert_at_exact_threshold() {
    TEST_ASSERT_FALSE(calcAlertHum(80.0f, 80.0f));
}

void test_tvoc_alert_triggered() {
    TEST_ASSERT_TRUE(calcAlertTvoc(501, 500.0f));
}

void test_tvoc_alert_not_triggered() {
    TEST_ASSERT_FALSE(calcAlertTvoc(499, 500.0f));
}

void test_eco2_alert_triggered() {
    TEST_ASSERT_TRUE(calcAlertEco2(1001, 1000.0f));
}

void test_eco2_alert_not_triggered() {
    TEST_ASSERT_FALSE(calcAlertEco2(999, 1000.0f));
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_temp_alert_triggered);
    RUN_TEST(test_temp_alert_not_triggered);
    RUN_TEST(test_temp_alert_at_exact_threshold);
    RUN_TEST(test_hum_alert_triggered);
    RUN_TEST(test_hum_alert_not_triggered);
    RUN_TEST(test_hum_alert_at_exact_threshold);
    RUN_TEST(test_tvoc_alert_triggered);
    RUN_TEST(test_tvoc_alert_not_triggered);
    RUN_TEST(test_eco2_alert_triggered);
    RUN_TEST(test_eco2_alert_not_triggered);
    return UNITY_END();
}