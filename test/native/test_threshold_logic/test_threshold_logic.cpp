#include <unity.h>

// ── Replicate alert logic from web_server.cpp handleSetThresh() ───────────────
// alertTemp = (sensorTemp > threshTemp) || (sensorTemp < threshTempLow);
// alertHum  = (sensorHum  > threshHum)  || (sensorHum  < threshHumLow);
// alertCO2  = (sensorCO2  > threshCO2);

static bool calcAlertTemp(float sensorTemp, float threshTemp, float threshTempLow) {
    return (sensorTemp > threshTemp) || (sensorTemp < threshTempLow);
}

static bool calcAlertHum(float sensorHum, float threshHum, float threshHumLow) {
    return (sensorHum > threshHum) || (sensorHum < threshHumLow);
}

static bool calcAlertCO2(uint16_t co2, float threshCO2) {
    return co2 > threshCO2;
}

// ── Temperature ─────────────────────────────────────────────────────────────

void test_temp_alert_triggered_high() {
    TEST_ASSERT_TRUE(calcAlertTemp(31.0f, 30.0f, 5.0f));
}

void test_temp_alert_triggered_low() {
    TEST_ASSERT_TRUE(calcAlertTemp(4.0f, 30.0f, 5.0f));
}

void test_temp_alert_not_triggered() {
    TEST_ASSERT_FALSE(calcAlertTemp(20.0f, 30.0f, 5.0f));
}

void test_temp_alert_at_exact_high_threshold() {
    // strictly greater than — equal should NOT trigger
    TEST_ASSERT_FALSE(calcAlertTemp(30.0f, 30.0f, 5.0f));
}

void test_temp_alert_at_exact_low_threshold() {
    // strictly less than — equal should NOT trigger
    TEST_ASSERT_FALSE(calcAlertTemp(5.0f, 30.0f, 5.0f));
}

// ── Humidity ────────────────────────────────────────────────────────────────

void test_hum_alert_triggered_high() {
    TEST_ASSERT_TRUE(calcAlertHum(81.0f, 80.0f, 20.0f));
}

void test_hum_alert_triggered_low() {
    TEST_ASSERT_TRUE(calcAlertHum(19.0f, 80.0f, 20.0f));
}

void test_hum_alert_not_triggered() {
    TEST_ASSERT_FALSE(calcAlertHum(50.0f, 80.0f, 20.0f));
}

void test_hum_alert_at_exact_high_threshold() {
    TEST_ASSERT_FALSE(calcAlertHum(80.0f, 80.0f, 20.0f));
}

void test_hum_alert_at_exact_low_threshold() {
    TEST_ASSERT_FALSE(calcAlertHum(20.0f, 80.0f, 20.0f));
}

// ── CO2 ─────────────────────────────────────────────────────────────────────

void test_co2_alert_triggered() {
    TEST_ASSERT_TRUE(calcAlertCO2(1001, 1000.0f));
}

void test_co2_alert_not_triggered() {
    TEST_ASSERT_FALSE(calcAlertCO2(999, 1000.0f));
}

void test_co2_alert_at_exact_threshold() {
    TEST_ASSERT_FALSE(calcAlertCO2(1000, 1000.0f));
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_temp_alert_triggered_high);
    RUN_TEST(test_temp_alert_triggered_low);
    RUN_TEST(test_temp_alert_not_triggered);
    RUN_TEST(test_temp_alert_at_exact_high_threshold);
    RUN_TEST(test_temp_alert_at_exact_low_threshold);
    RUN_TEST(test_hum_alert_triggered_high);
    RUN_TEST(test_hum_alert_triggered_low);
    RUN_TEST(test_hum_alert_not_triggered);
    RUN_TEST(test_hum_alert_at_exact_high_threshold);
    RUN_TEST(test_hum_alert_at_exact_low_threshold);
    RUN_TEST(test_co2_alert_triggered);
    RUN_TEST(test_co2_alert_not_triggered);
    RUN_TEST(test_co2_alert_at_exact_threshold);
    return UNITY_END();
}
