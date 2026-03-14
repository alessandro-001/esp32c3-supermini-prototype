#include <unity.h>
#include <cstdint>

// Reading validation (mirrors the if-check in shtc3Read())
static bool shtc3ReadingValid(float t, float h) {
    return (t > -40.0f && t < 120.0f && h >= 0.0f && h <= 100.0f);
}

// Simulated sensor state
static float  sensorTemp = 0.0f;
static float  sensorHum  = 0.0f;
static bool   sensorOK   = false;

// Mirrors shtc3Read() behaviour: only update globals if sensorOK and reading valid
static bool shtc3ApplyReading(float t, float h) {
    if (!sensorOK) return false;
    if (!shtc3ReadingValid(t, h)) return false;
    sensorTemp = t;
    sensorHum  = h;
    return true;
}

// Interval throttle logic (mirrors the millis() guard in shtc3Read())
static bool shtc3ShouldRead(uint32_t now, uint32_t lastRead, uint32_t interval) {
    return (now - lastRead) >= interval;
}

// ── setUp / tearDown ──────────────────────────────────────────────────────────

void setUp() {
    sensorTemp = 0.0f;
    sensorHum  = 0.0f;
    sensorOK   = false;
}

void tearDown() {}

// ── Validation tests ──────────────────────────────────────────────────────────

void test_valid_typical_reading() {
    TEST_ASSERT_TRUE(shtc3ReadingValid(22.5f, 55.0f));
}

void test_valid_boundary_temp_low() {
    TEST_ASSERT_TRUE(shtc3ReadingValid(-39.9f, 50.0f));
}

void test_valid_boundary_temp_high() {
    TEST_ASSERT_TRUE(shtc3ReadingValid(119.9f, 50.0f));
}

void test_valid_boundary_hum_zero() {
    TEST_ASSERT_TRUE(shtc3ReadingValid(22.5f, 0.0f));
}

void test_valid_boundary_hum_100() {
    TEST_ASSERT_TRUE(shtc3ReadingValid(22.5f, 100.0f));
}

void test_invalid_temp_at_lower_bound() {
    // exactly -40 is not valid (strictly greater than)
    TEST_ASSERT_FALSE(shtc3ReadingValid(-40.0f, 50.0f));
}

void test_invalid_temp_at_upper_bound() {
    // exactly 120 is not valid (strictly less than)
    TEST_ASSERT_FALSE(shtc3ReadingValid(120.0f, 50.0f));
}

void test_invalid_temp_too_low() {
    TEST_ASSERT_FALSE(shtc3ReadingValid(-50.0f, 50.0f));
}

void test_invalid_temp_too_high() {
    TEST_ASSERT_FALSE(shtc3ReadingValid(150.0f, 50.0f));
}

void test_invalid_humidity_negative() {
    TEST_ASSERT_FALSE(shtc3ReadingValid(22.5f, -0.1f));
}

void test_invalid_humidity_over_100() {
    TEST_ASSERT_FALSE(shtc3ReadingValid(22.5f, 100.1f));
}

// ── sensorOK guard tests ──────────────────────────────────────────────────────

void test_reading_rejected_when_sensor_not_ok() {
    sensorOK = false;
    TEST_ASSERT_FALSE(shtc3ApplyReading(22.5f, 55.0f));
    TEST_ASSERT_EQUAL_FLOAT(0.0f, sensorTemp);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, sensorHum);
}

void test_reading_accepted_when_sensor_ok() {
    sensorOK = true;
    TEST_ASSERT_TRUE(shtc3ApplyReading(22.5f, 55.0f));
    TEST_ASSERT_EQUAL_FLOAT(22.5f, sensorTemp);
    TEST_ASSERT_EQUAL_FLOAT(55.0f, sensorHum);
}

void test_bad_reading_does_not_update_globals() {
    sensorOK   = true;
    sensorTemp = 20.0f;
    sensorHum  = 50.0f;
    // out-of-range reading — globals should stay unchanged
    TEST_ASSERT_FALSE(shtc3ApplyReading(999.0f, 50.0f));
    TEST_ASSERT_EQUAL_FLOAT(20.0f, sensorTemp);
    TEST_ASSERT_EQUAL_FLOAT(50.0f, sensorHum);
}

void test_globals_updated_on_valid_read() {
    sensorOK = true;
    shtc3ApplyReading(18.3f, 62.1f);
    TEST_ASSERT_EQUAL_FLOAT(18.3f, sensorTemp);
    TEST_ASSERT_EQUAL_FLOAT(62.1f, sensorHum);
}

// ── Interval throttle tests ───────────────────────────────────────────────────

void test_should_read_when_interval_elapsed() {
    TEST_ASSERT_TRUE(shtc3ShouldRead(4000, 2000, 2000)); // 2000ms elapsed
}

void test_should_not_read_before_interval() {
    TEST_ASSERT_FALSE(shtc3ShouldRead(3999, 2000, 2000)); // only 1999ms elapsed
}

void test_should_read_at_exact_interval() {
    TEST_ASSERT_TRUE(shtc3ShouldRead(4000, 2000, 2000)); // exactly 2000ms
}

void test_should_read_on_first_call() {
    // lastRead=0, now=2000 — first read after boot
    TEST_ASSERT_TRUE(shtc3ShouldRead(2000, 0, 2000));
}

void test_should_not_read_immediately_after_read() {
    // just read at t=2000, now=2001
    TEST_ASSERT_FALSE(shtc3ShouldRead(2001, 2000, 2000));
}

// ── main ──────────────────────────────────────────────────────────────────────

int main() {
    UNITY_BEGIN();

    // Validation
    RUN_TEST(test_valid_typical_reading);
    RUN_TEST(test_valid_boundary_temp_low);
    RUN_TEST(test_valid_boundary_temp_high);
    RUN_TEST(test_valid_boundary_hum_zero);
    RUN_TEST(test_valid_boundary_hum_100);
    RUN_TEST(test_invalid_temp_at_lower_bound);
    RUN_TEST(test_invalid_temp_at_upper_bound);
    RUN_TEST(test_invalid_temp_too_low);
    RUN_TEST(test_invalid_temp_too_high);
    RUN_TEST(test_invalid_humidity_negative);
    RUN_TEST(test_invalid_humidity_over_100);

    // sensorOK guard
    RUN_TEST(test_reading_rejected_when_sensor_not_ok);
    RUN_TEST(test_reading_accepted_when_sensor_ok);
    RUN_TEST(test_bad_reading_does_not_update_globals);
    RUN_TEST(test_globals_updated_on_valid_read);

    // Interval throttle
    RUN_TEST(test_should_read_when_interval_elapsed);
    RUN_TEST(test_should_not_read_before_interval);
    RUN_TEST(test_should_read_at_exact_interval);
    RUN_TEST(test_should_read_on_first_call);
    RUN_TEST(test_should_not_read_immediately_after_read);

    return UNITY_END();
}