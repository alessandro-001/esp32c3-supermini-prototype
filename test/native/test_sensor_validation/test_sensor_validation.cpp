#include <unity.h>
#include <cstdint>

// ── Replicate validation logic from shtc3.cpp and ens160.cpp ─────────────────

static bool shtc3ReadingValid(float t, float h) {
    return (t > -40 && t < 120 && h >= 0 && h <= 100);
}

static bool ens160ReadingValid(uint8_t aqi, uint16_t eco2) {
    return (aqi >= 1 && aqi <= 5 && eco2 >= 400 && eco2 <= 65000);
}

// ── SHTC3 tests ───────────────────────────────────────────────────────────────

void test_shtc3_valid_normal_reading() {
    TEST_ASSERT_TRUE(shtc3ReadingValid(25.0f, 55.0f));
}

void test_shtc3_valid_boundary_low() {
    TEST_ASSERT_TRUE(shtc3ReadingValid(-39.9f, 0.0f));
}

void test_shtc3_valid_boundary_high() {
    TEST_ASSERT_TRUE(shtc3ReadingValid(119.9f, 100.0f));
}

void test_shtc3_invalid_temp_too_low() {
    TEST_ASSERT_FALSE(shtc3ReadingValid(-41.0f, 55.0f));
}

void test_shtc3_invalid_temp_too_high() {
    TEST_ASSERT_FALSE(shtc3ReadingValid(121.0f, 55.0f));
}

void test_shtc3_invalid_humidity_negative() {
    TEST_ASSERT_FALSE(shtc3ReadingValid(25.0f, -1.0f));
}

void test_shtc3_invalid_humidity_over_100() {
    TEST_ASSERT_FALSE(shtc3ReadingValid(25.0f, 101.0f));
}

// ── ENS160 tests ──────────────────────────────────────────────────────────────

void test_ens160_valid_normal_reading() {
    TEST_ASSERT_TRUE(ens160ReadingValid(2, 800));
}

void test_ens160_valid_aqi_boundary_low() {
    TEST_ASSERT_TRUE(ens160ReadingValid(1, 400));
}

void test_ens160_valid_aqi_boundary_high() {
    TEST_ASSERT_TRUE(ens160ReadingValid(5, 65000));
}

void test_ens160_invalid_aqi_zero() {
    TEST_ASSERT_FALSE(ens160ReadingValid(0, 800));
}

void test_ens160_invalid_aqi_too_high() {
    TEST_ASSERT_FALSE(ens160ReadingValid(6, 800));
}

void test_ens160_invalid_eco2_too_low() {
    TEST_ASSERT_FALSE(ens160ReadingValid(2, 399));
}

void test_ens160_invalid_eco2_too_high() {
    TEST_ASSERT_FALSE(ens160ReadingValid(2, 65001));
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_shtc3_valid_normal_reading);
    RUN_TEST(test_shtc3_valid_boundary_low);
    RUN_TEST(test_shtc3_valid_boundary_high);
    RUN_TEST(test_shtc3_invalid_temp_too_low);
    RUN_TEST(test_shtc3_invalid_temp_too_high);
    RUN_TEST(test_shtc3_invalid_humidity_negative);
    RUN_TEST(test_shtc3_invalid_humidity_over_100);
    RUN_TEST(test_ens160_valid_normal_reading);
    RUN_TEST(test_ens160_valid_aqi_boundary_low);
    RUN_TEST(test_ens160_valid_aqi_boundary_high);
    RUN_TEST(test_ens160_invalid_aqi_zero);
    RUN_TEST(test_ens160_invalid_aqi_too_high);
    RUN_TEST(test_ens160_invalid_eco2_too_low);
    RUN_TEST(test_ens160_invalid_eco2_too_high);
    return UNITY_END();
}