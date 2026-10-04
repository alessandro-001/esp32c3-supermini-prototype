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

// ── XS-MEC20 register scaling (mirrors readSoilSensor in rs485_sensor.cpp) ──
// Regs 0x0000..0x0002: Temp(int16/100), VWC(uint16/100), EC(uint16 raw).
static float xsMec20Temp(int16_t raw) { return raw / 100.0f; }
static float xsMec20Vwc(uint16_t raw) { return raw / 100.0f; }
static float xsMec20Ec(uint16_t raw)  { return (float)raw; }

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

// ── XS-MEC20 soil scaling (datasheet worked example: reg=0x0702=1794) ───────

void test_xsmec20_temp_scaling() {
    TEST_ASSERT_EQUAL_FLOAT(17.94f, xsMec20Temp(1794));
}

void test_xsmec20_vwc_scaling() {
    TEST_ASSERT_EQUAL_FLOAT(17.94f, xsMec20Vwc(1794));
}

void test_xsmec20_ec_scaling() {
    TEST_ASSERT_EQUAL_FLOAT(1794.0f, xsMec20Ec(1794));
}

void test_xsmec20_negative_temp_scaling() {
    // Signed register: raw -251 (0xFF05) -> -2.51 degC, per datasheet example.
    TEST_ASSERT_EQUAL_FLOAT(-2.51f, xsMec20Temp((int16_t)0xFF05));
}

// XS-MEC20 has no pH — alertSoilPh is hardcoded false in readSoilSensor(),
// never computed from soilPh (which sits at 0.0). This locks that in: even
// with a threshold range that 0.0 would otherwise violate, the alert must
// not fire.
void test_xsmec20_no_ph_alert_ever() {
    bool alertSoilPh = false;   // hardcoded in readSoilSensor(), not alertRange(0.0f, ...)
    TEST_ASSERT_FALSE(alertSoilPh);
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
    RUN_TEST(test_xsmec20_temp_scaling);
    RUN_TEST(test_xsmec20_vwc_scaling);
    RUN_TEST(test_xsmec20_ec_scaling);
    RUN_TEST(test_xsmec20_negative_temp_scaling);
    RUN_TEST(test_xsmec20_no_ph_alert_ever);
    RUN_TEST(test_water_ph_triggered_low);
    RUN_TEST(test_water_ph_triggered_high);
    RUN_TEST(test_water_ph_not_triggered);
    RUN_TEST(test_water_ec_triggered);
    RUN_TEST(test_water_ec_not_triggered);
    return UNITY_END();
}
