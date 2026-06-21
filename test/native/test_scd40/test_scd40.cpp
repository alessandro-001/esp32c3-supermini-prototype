#include <unity.h>
#include <cstdint>

// ── Replicate pure logic from sensors/scd40.cpp ───────────────────────────────

static uint8_t scd40Crc8(uint8_t msb, uint8_t lsb) {
    uint8_t crc = 0xFF;
    uint8_t data[2] = { msb, lsb };
    for (uint8_t i = 0; i < 2; i++) {
        crc ^= data[i];
        for (uint8_t bit = 0; bit < 8; bit++) {
            crc = (crc & 0x80) ? (uint8_t)((crc << 1) ^ 0x31) : (uint8_t)(crc << 1);
        }
    }
    return crc;
}

static const char* co2Label(uint16_t co2) {
    if      (co2 == 0)    return "Unavailable";
    else if (co2 < 400)   return "Low/Warming";
    else if (co2 < 600)   return "Excellent";
    else if (co2 < 800)   return "Good";
    else if (co2 < 1000)  return "Moderate";
    else if (co2 < 1500)  return "Poor";
    else                  return "Unhealthy";
}

static float rawToTemp(uint16_t rawTemp) {
    return -45.0f + 175.0f * ((float)rawTemp / 65535.0f);
}

static float rawToHum(uint16_t rawHum) {
    return 100.0f * ((float)rawHum / 65535.0f);
}

static uint16_t applyCo2Offset(uint16_t co2, float co2Offset) {
    float v = (float)co2 + co2Offset;
    return (uint16_t)(v < 0.0f ? 0 : v);
}

// ── CRC-8 ───────────────────────────────────────────────────────────────────

void test_crc8_deterministic() {
    TEST_ASSERT_EQUAL_UINT8(scd40Crc8(0x02, 0x84), scd40Crc8(0x02, 0x84));
}

void test_crc8_detects_corruption() {
    TEST_ASSERT_NOT_EQUAL(scd40Crc8(0x02, 0x84), scd40Crc8(0x02, 0x85));
}

// ── co2Label ────────────────────────────────────────────────────────────────

void test_co2_label_zero_unavailable() {
    TEST_ASSERT_EQUAL_STRING("Unavailable", co2Label(0));
}

void test_co2_label_low_warming() {
    TEST_ASSERT_EQUAL_STRING("Low/Warming", co2Label(399));
}

void test_co2_label_excellent() {
    TEST_ASSERT_EQUAL_STRING("Excellent", co2Label(400));
    TEST_ASSERT_EQUAL_STRING("Excellent", co2Label(599));
}

void test_co2_label_good() {
    TEST_ASSERT_EQUAL_STRING("Good", co2Label(600));
    TEST_ASSERT_EQUAL_STRING("Good", co2Label(799));
}

void test_co2_label_moderate() {
    TEST_ASSERT_EQUAL_STRING("Moderate", co2Label(800));
    TEST_ASSERT_EQUAL_STRING("Moderate", co2Label(999));
}

void test_co2_label_poor() {
    TEST_ASSERT_EQUAL_STRING("Poor", co2Label(1000));
    TEST_ASSERT_EQUAL_STRING("Poor", co2Label(1499));
}

void test_co2_label_unhealthy() {
    TEST_ASSERT_EQUAL_STRING("Unhealthy", co2Label(1500));
    TEST_ASSERT_EQUAL_STRING("Unhealthy", co2Label(40000));
}

// ── Raw -> physical conversion ───────────────────────────────────────────────

void test_raw_temp_min() {
    TEST_ASSERT_EQUAL_FLOAT(-45.0f, rawToTemp(0));
}

void test_raw_temp_max() {
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 130.0f, rawToTemp(65535));
}

void test_raw_hum_min() {
    TEST_ASSERT_EQUAL_FLOAT(0.0f, rawToHum(0));
}

void test_raw_hum_max() {
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 100.0f, rawToHum(65535));
}

// ── CO2 offset clamp ──────────────────────────────────────────────────────────

void test_co2_offset_positive() {
    TEST_ASSERT_EQUAL_UINT16(650, applyCo2Offset(600, 50.0f));
}

void test_co2_offset_negative_clamps_to_zero() {
    TEST_ASSERT_EQUAL_UINT16(0, applyCo2Offset(30, -100.0f));
}

void test_co2_offset_zero() {
    TEST_ASSERT_EQUAL_UINT16(600, applyCo2Offset(600, 0.0f));
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_crc8_deterministic);
    RUN_TEST(test_crc8_detects_corruption);
    RUN_TEST(test_co2_label_zero_unavailable);
    RUN_TEST(test_co2_label_low_warming);
    RUN_TEST(test_co2_label_excellent);
    RUN_TEST(test_co2_label_good);
    RUN_TEST(test_co2_label_moderate);
    RUN_TEST(test_co2_label_poor);
    RUN_TEST(test_co2_label_unhealthy);
    RUN_TEST(test_raw_temp_min);
    RUN_TEST(test_raw_temp_max);
    RUN_TEST(test_raw_hum_min);
    RUN_TEST(test_raw_hum_max);
    RUN_TEST(test_co2_offset_positive);
    RUN_TEST(test_co2_offset_negative_clamps_to_zero);
    RUN_TEST(test_co2_offset_zero);
    return UNITY_END();
}
