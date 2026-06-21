#include <unity.h>

// ── Replicate pure logic from sensors/ldr.cpp ldrRead() ───────────────────────

struct LdrFilterResult {
    int  minVal;
    int  validCount;
};

static LdrFilterResult filterSamples(const int samples[5]) {
    LdrFilterResult r{ 4095, 0 };
    for (int i = 0; i < 5; i++) {
        if (samples[i] < 4090) {
            if (samples[i] < r.minVal) r.minVal = samples[i];
            r.validCount++;
        }
    }
    return r;
}

static bool lightOn(int minVal, int thresh) {
    return minVal > thresh;  // high value = lit, low value = dark
}

// ── Sample filtering ──────────────────────────────────────────────────────────

void test_filter_all_valid_picks_min() {
    int samples[5] = { 1000, 800, 1200, 900, 2000 };
    auto r = filterSamples(samples);
    TEST_ASSERT_EQUAL_INT(800, r.minVal);
    TEST_ASSERT_EQUAL_INT(5, r.validCount);
}

void test_filter_rejects_strapping_spike() {
    // 4090+ is a strapping spike, excluded from min/valid count
    int samples[5] = { 4095, 4091, 4090, 1500, 1600 };
    auto r = filterSamples(samples);
    TEST_ASSERT_EQUAL_INT(1500, r.minVal);
    TEST_ASSERT_EQUAL_INT(2, r.validCount);
}

void test_filter_all_spikes_yields_no_valid() {
    int samples[5] = { 4095, 4095, 4090, 4091, 4092 };
    auto r = filterSamples(samples);
    TEST_ASSERT_EQUAL_INT(0, r.validCount);
}

void test_filter_boundary_4089_is_valid() {
    int samples[5] = { 4089, 4090, 4090, 4090, 4090 };
    auto r = filterSamples(samples);
    TEST_ASSERT_EQUAL_INT(1, r.validCount);
    TEST_ASSERT_EQUAL_INT(4089, r.minVal);
}

// ── Threshold comparison ──────────────────────────────────────────────────────

void test_light_on_above_threshold() {
    TEST_ASSERT_TRUE(lightOn(60, 50));
}

void test_light_off_below_threshold() {
    TEST_ASSERT_FALSE(lightOn(40, 50));
}

void test_light_off_at_exact_threshold() {
    // strictly greater than — equal does not trigger
    TEST_ASSERT_FALSE(lightOn(50, 50));
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_filter_all_valid_picks_min);
    RUN_TEST(test_filter_rejects_strapping_spike);
    RUN_TEST(test_filter_all_spikes_yields_no_valid);
    RUN_TEST(test_filter_boundary_4089_is_valid);
    RUN_TEST(test_light_on_above_threshold);
    RUN_TEST(test_light_off_below_threshold);
    RUN_TEST(test_light_off_at_exact_threshold);
    return UNITY_END();
}
