#include <unity.h>

// ── Replicate alert logic from sensors/rs485_sensor.cpp ───────────────────────
// alertSoilMoist = (soilMoist < threshSoilMoistLow) || (soilMoist > threshSoilMoistHigh);
// alertSoilEc    = (soilEc > threshSoilEcHigh);
// alertSoilPh    = (soilPh < threshSoilPhLow) || (soilPh > threshSoilPhHigh);
// alertWaterPh   = (waterPh < threshWaterPhLow) || (waterPh > threshWaterPhHigh);
// alertWaterEc   = (waterEc > threshWaterEcHigh);

static bool alertRange(float val, float low, float high) {
    return (val < low) || (val > high);
}

static bool alertHighOnly(float val, float high) {
    return val > high;
}

// ── Soil moisture (range) ──────────────────────────────────────────────────

void test_soil_moist_triggered_high() {
    TEST_ASSERT_TRUE(alertRange(81.0f, 20.0f, 80.0f));
}

void test_soil_moist_triggered_low() {
    TEST_ASSERT_TRUE(alertRange(19.0f, 20.0f, 80.0f));
}

void test_soil_moist_not_triggered() {
    TEST_ASSERT_FALSE(alertRange(50.0f, 20.0f, 80.0f));
}

void test_soil_moist_at_exact_bounds() {
    TEST_ASSERT_FALSE(alertRange(20.0f, 20.0f, 80.0f));
    TEST_ASSERT_FALSE(alertRange(80.0f, 20.0f, 80.0f));
}

// ── Soil EC / pH ────────────────────────────────────────────────────────────

void test_soil_ec_triggered() {
    TEST_ASSERT_TRUE(alertHighOnly(2001.0f, 2000.0f));
}

void test_soil_ec_not_triggered() {
    TEST_ASSERT_FALSE(alertHighOnly(2000.0f, 2000.0f));
}

void test_soil_ph_triggered_low() {
    TEST_ASSERT_TRUE(alertRange(5.0f, 5.5f, 7.5f));
}

void test_soil_ph_triggered_high() {
    TEST_ASSERT_TRUE(alertRange(8.0f, 5.5f, 7.5f));
}

void test_soil_ph_not_triggered() {
    TEST_ASSERT_FALSE(alertRange(6.5f, 5.5f, 7.5f));
}

// ── Water pH / EC ───────────────────────────────────────────────────────────

void test_water_ph_triggered_low() {
    TEST_ASSERT_TRUE(alertRange(5.0f, 5.5f, 7.5f));
}

void test_water_ph_triggered_high() {
    TEST_ASSERT_TRUE(alertRange(8.0f, 5.5f, 7.5f));
}

void test_water_ph_not_triggered() {
    TEST_ASSERT_FALSE(alertRange(6.5f, 5.5f, 7.5f));
}

void test_water_ec_triggered() {
    TEST_ASSERT_TRUE(alertHighOnly(1501.0f, 1500.0f));
}

void test_water_ec_not_triggered() {
    TEST_ASSERT_FALSE(alertHighOnly(1500.0f, 1500.0f));
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_soil_moist_triggered_high);
    RUN_TEST(test_soil_moist_triggered_low);
    RUN_TEST(test_soil_moist_not_triggered);
    RUN_TEST(test_soil_moist_at_exact_bounds);
    RUN_TEST(test_soil_ec_triggered);
    RUN_TEST(test_soil_ec_not_triggered);
    RUN_TEST(test_soil_ph_triggered_low);
    RUN_TEST(test_soil_ph_triggered_high);
    RUN_TEST(test_soil_ph_not_triggered);
    RUN_TEST(test_water_ph_triggered_low);
    RUN_TEST(test_water_ph_triggered_high);
    RUN_TEST(test_water_ph_not_triggered);
    RUN_TEST(test_water_ec_triggered);
    RUN_TEST(test_water_ec_not_triggered);
    return UNITY_END();
}
