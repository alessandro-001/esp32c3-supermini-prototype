#include <unity.h>
#include <cstring>

// ── Replicate validation logic from wifi_config.cpp wifiConfigSave() ─────────

static bool wifiConfigSaveValid(const char* ssid, const char* pass) {
    if (strlen(ssid) == 0) return false;
    if (strlen(pass) > 0 && strlen(pass) < 8) return false;
    return true;
}

void test_valid_ssid_with_password() {
    TEST_ASSERT_TRUE(wifiConfigSaveValid("MyNetwork", "securepass"));
}

void test_valid_ssid_open_network_empty_pass() {
    TEST_ASSERT_TRUE(wifiConfigSaveValid("OpenNet", ""));
}

void test_invalid_empty_ssid() {
    TEST_ASSERT_FALSE(wifiConfigSaveValid("", "somepassword"));
}

void test_invalid_password_too_short() {
    TEST_ASSERT_FALSE(wifiConfigSaveValid("MyNetwork", "short"));
}

void test_valid_password_exactly_8_chars() {
    TEST_ASSERT_TRUE(wifiConfigSaveValid("MyNetwork", "exactly8"));
}

void test_valid_password_long() {
    TEST_ASSERT_TRUE(wifiConfigSaveValid("MyNetwork", "averylongpassword123"));
}

void test_invalid_empty_ssid_with_empty_pass() {
    TEST_ASSERT_FALSE(wifiConfigSaveValid("", ""));
}

void test_invalid_password_7_chars() {
    TEST_ASSERT_FALSE(wifiConfigSaveValid("MyNetwork", "seven77"));
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_valid_ssid_with_password);
    RUN_TEST(test_valid_ssid_open_network_empty_pass);
    RUN_TEST(test_invalid_empty_ssid);
    RUN_TEST(test_invalid_password_too_short);
    RUN_TEST(test_valid_password_exactly_8_chars);
    RUN_TEST(test_valid_password_long);
    RUN_TEST(test_invalid_empty_ssid_with_empty_pass);
    RUN_TEST(test_invalid_password_7_chars);
    return UNITY_END();
}