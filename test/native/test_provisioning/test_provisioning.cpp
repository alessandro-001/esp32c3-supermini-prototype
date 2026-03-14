#include <unity.h>
#include <cstring>
#include <ArduinoJson.h>

// ── Replicate JSON parsing logic from provisioning.cpp ────────────────────────

static bool parseProvisioningResponse(const char* json, char* tokenOut, size_t tokenLen) {
    StaticJsonDocument<256> doc;
    DeserializationError err = deserializeJson(doc, json);
    if (err) return false;

    const char* status = doc["status"];
    if (!status || strcmp(status, "SUCCESS") != 0) return false;

    const char* token = doc["credentialsValue"];
    if (!token || strlen(token) == 0) return false;

    strncpy(tokenOut, token, tokenLen - 1);
    tokenOut[tokenLen - 1] = '\0';
    return true;
}

void test_valid_provisioning_response() {
    const char* json = "{\"status\":\"SUCCESS\",\"credentialsValue\":\"abc123token\"}";
    char token[64] = {};
    TEST_ASSERT_TRUE(parseProvisioningResponse(json, token, sizeof(token)));
    TEST_ASSERT_EQUAL_STRING("abc123token", token);
}

void test_provisioning_response_failure_status() {
    const char* json = "{\"status\":\"FAILURE\",\"credentialsValue\":\"abc123token\"}";
    char token[64] = {};
    TEST_ASSERT_FALSE(parseProvisioningResponse(json, token, sizeof(token)));
}

void test_provisioning_response_missing_status() {
    const char* json = "{\"credentialsValue\":\"abc123token\"}";
    char token[64] = {};
    TEST_ASSERT_FALSE(parseProvisioningResponse(json, token, sizeof(token)));
}

void test_provisioning_response_missing_token() {
    const char* json = "{\"status\":\"SUCCESS\"}";
    char token[64] = {};
    TEST_ASSERT_FALSE(parseProvisioningResponse(json, token, sizeof(token)));
}

void test_provisioning_response_empty_token() {
    const char* json = "{\"status\":\"SUCCESS\",\"credentialsValue\":\"\"}";
    char token[64] = {};
    TEST_ASSERT_FALSE(parseProvisioningResponse(json, token, sizeof(token)));
}

void test_provisioning_response_invalid_json() {
    const char* json = "not valid json {{";
    char token[64] = {};
    TEST_ASSERT_FALSE(parseProvisioningResponse(json, token, sizeof(token)));
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_valid_provisioning_response);
    RUN_TEST(test_provisioning_response_failure_status);
    RUN_TEST(test_provisioning_response_missing_status);
    RUN_TEST(test_provisioning_response_missing_token);
    RUN_TEST(test_provisioning_response_empty_token);
    RUN_TEST(test_provisioning_response_invalid_json);
    return UNITY_END();
}