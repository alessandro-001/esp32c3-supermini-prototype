#include <unity.h>
#include <cstdint>
#include <cstring>

// ── Replicate AQI label logic from ens160.cpp ─────────────────────────────────

static const char* ens160AQILabel(uint8_t aqi) {
    switch (aqi) {
        case 1: return "Excellent";
        case 2: return "Good";
        case 3: return "Moderate";
        case 4: return "Poor";
        case 5: return "Unhealthy";
        default: return "Unknown";
    }
}

void test_aqi_label_1_excellent() {
    TEST_ASSERT_EQUAL_STRING("Excellent", ens160AQILabel(1));
}

void test_aqi_label_2_good() {
    TEST_ASSERT_EQUAL_STRING("Good", ens160AQILabel(2));
}

void test_aqi_label_3_moderate() {
    TEST_ASSERT_EQUAL_STRING("Moderate", ens160AQILabel(3));
}

void test_aqi_label_4_poor() {
    TEST_ASSERT_EQUAL_STRING("Poor", ens160AQILabel(4));
}

void test_aqi_label_5_unhealthy() {
    TEST_ASSERT_EQUAL_STRING("Unhealthy", ens160AQILabel(5));
}

void test_aqi_label_0_unknown() {
    TEST_ASSERT_EQUAL_STRING("Unknown", ens160AQILabel(0));
}

void test_aqi_label_6_unknown() {
    TEST_ASSERT_EQUAL_STRING("Unknown", ens160AQILabel(6));
}

void test_aqi_label_255_unknown() {
    TEST_ASSERT_EQUAL_STRING("Unknown", ens160AQILabel(255));
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_aqi_label_1_excellent);
    RUN_TEST(test_aqi_label_2_good);
    RUN_TEST(test_aqi_label_3_moderate);
    RUN_TEST(test_aqi_label_4_poor);
    RUN_TEST(test_aqi_label_5_unhealthy);
    RUN_TEST(test_aqi_label_0_unknown);
    RUN_TEST(test_aqi_label_6_unknown);
    RUN_TEST(test_aqi_label_255_unknown);
    return UNITY_END();
}