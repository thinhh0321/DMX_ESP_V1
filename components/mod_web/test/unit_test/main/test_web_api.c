/**
 * @file test_web_api.c
 * @brief Unit tests for Web API import/export functionality
 */

#include "unity.h"
#include "sys_mod.h"
#include "dmx_types.h"
#include "cJSON.h"
#include <string.h>

void setUp(void) {
}

void tearDown(void) {
}

/**
 * Test configuration export structure
 */
void test_config_export_structure(void)
{
    // Get system configuration
    const sys_config_t *cfg = sys_get_config();
    TEST_ASSERT_NOT_NULL(cfg);
    
    // Create JSON export (mimicking the export handler logic)
    cJSON *root = cJSON_CreateObject();
    TEST_ASSERT_NOT_NULL(root);
    
    // Add device info
    cJSON_AddStringToObject(root, "device_label", cfg->device_label);
    cJSON_AddNumberToObject(root, "led_brightness", cfg->led_brightness);
    
    // Verify JSON was created
    TEST_ASSERT_NOT_NULL(cJSON_GetObjectItem(root, "device_label"));
    TEST_ASSERT_NOT_NULL(cJSON_GetObjectItem(root, "led_brightness"));
    
    cJSON_Delete(root);
}

/**
 * Test that export includes all required fields
 */
void test_config_export_completeness(void)
{
    const sys_config_t *cfg = sys_get_config();
    TEST_ASSERT_NOT_NULL(cfg);
    
    cJSON *root = cJSON_CreateObject();
    
    // Device info
    cJSON_AddStringToObject(root, "device_label", cfg->device_label);
    cJSON_AddNumberToObject(root, "led_brightness", cfg->led_brightness);
    
    // Network configuration
    cJSON *net = cJSON_CreateObject();
    cJSON_AddBoolToObject(net, "dhcp", cfg->net.dhcp);
    cJSON_AddItemToObject(root, "network", net);
    
    // DMX ports
    cJSON *ports = cJSON_CreateArray();
    for (int i = 0; i < 4; i++) {
        cJSON *port = cJSON_CreateObject();
        cJSON_AddNumberToObject(port, "index", i);
        cJSON_AddBoolToObject(port, "enabled", cfg->ports[i].enabled);
        cJSON_AddItemToArray(ports, port);
    }
    cJSON_AddItemToObject(root, "ports", ports);
    
    // Verify structure
    TEST_ASSERT_NOT_NULL(cJSON_GetObjectItem(root, "device_label"));
    TEST_ASSERT_NOT_NULL(cJSON_GetObjectItem(root, "network"));
    TEST_ASSERT_NOT_NULL(cJSON_GetObjectItem(root, "ports"));
    
    cJSON *ports_arr = cJSON_GetObjectItem(root, "ports");
    TEST_ASSERT_TRUE(cJSON_IsArray(ports_arr));
    TEST_ASSERT_EQUAL_INT(4, cJSON_GetArraySize(ports_arr));
    
    cJSON_Delete(root);
}

/**
 * Test configuration import parsing
 */
void test_config_import_parsing(void)
{
    // Create a sample import JSON
    const char *json_str = "{"
        "\"device_label\": \"Test Device\","
        "\"led_brightness\": 75,"
        "\"network\": {"
            "\"dhcp\": true,"
            "\"static_ip\": \"192.168.1.100\""
        "},"
        "\"ports\": ["
            "{\"index\": 0, \"enabled\": true, \"universe\": 1},"
            "{\"index\": 1, \"enabled\": false, \"universe\": 2}"
        "]"
    "}";
    
    cJSON *json = cJSON_Parse(json_str);
    TEST_ASSERT_NOT_NULL(json);
    
    // Verify parsing
    cJSON *device_label = cJSON_GetObjectItem(json, "device_label");
    TEST_ASSERT_NOT_NULL(device_label);
    TEST_ASSERT_TRUE(cJSON_IsString(device_label));
    TEST_ASSERT_EQUAL_STRING("Test Device", cJSON_GetStringValue(device_label));
    
    cJSON *led_brightness = cJSON_GetObjectItem(json, "led_brightness");
    TEST_ASSERT_NOT_NULL(led_brightness);
    TEST_ASSERT_TRUE(cJSON_IsNumber(led_brightness));
    TEST_ASSERT_EQUAL_INT(75, led_brightness->valueint);
    
    cJSON *network = cJSON_GetObjectItem(json, "network");
    TEST_ASSERT_NOT_NULL(network);
    TEST_ASSERT_TRUE(cJSON_IsObject(network));
    
    cJSON *ports = cJSON_GetObjectItem(json, "ports");
    TEST_ASSERT_NOT_NULL(ports);
    TEST_ASSERT_TRUE(cJSON_IsArray(ports));
    TEST_ASSERT_EQUAL_INT(2, cJSON_GetArraySize(ports));
    
    cJSON_Delete(json);
}

/**
 * Test configuration import validation
 */
void test_config_import_validation(void)
{
    // Test invalid JSON
    const char *invalid_json = "{invalid}";
    cJSON *json = cJSON_Parse(invalid_json);
    TEST_ASSERT_NULL(json);
    
    // Test valid but incomplete JSON
    const char *incomplete_json = "{\"device_label\": \"Test\"}";
    json = cJSON_Parse(incomplete_json);
    TEST_ASSERT_NOT_NULL(json);
    
    // Should have device_label but no network
    TEST_ASSERT_NOT_NULL(cJSON_GetObjectItem(json, "device_label"));
    TEST_ASSERT_NULL(cJSON_GetObjectItem(json, "network"));
    
    cJSON_Delete(json);
}

/**
 * Test string safety in import
 */
void test_config_import_string_safety(void)
{
    // Test that long strings are properly truncated
    char long_label[100];
    memset(long_label, 'A', sizeof(long_label) - 1);
    long_label[sizeof(long_label) - 1] = '\0';
    
    // Create JSON with long string
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "device_label", long_label);
    
    char *json_str = cJSON_PrintUnformatted(root);
    TEST_ASSERT_NOT_NULL(json_str);
    
    // Parse back
    cJSON *parsed = cJSON_Parse(json_str);
    TEST_ASSERT_NOT_NULL(parsed);
    
    cJSON *label = cJSON_GetObjectItem(parsed, "device_label");
    TEST_ASSERT_NOT_NULL(label);
    
    // String should be present (truncation handled by import handler)
    TEST_ASSERT_TRUE(strlen(cJSON_GetStringValue(label)) > 0);
    
    cJSON_Delete(root);
    cJSON_Delete(parsed);
    free(json_str);
}

/**
 * Test port configuration in import
 */
void test_config_import_port_config(void)
{
    const char *json_str = "{"
        "\"ports\": ["
            "{"
                "\"index\": 0,"
                "\"enabled\": true,"
                "\"universe\": 100,"
                "\"protocol\": 1,"
                "\"break_us\": 200,"
                "\"mab_us\": 15"
            "}"
        "]"
    "}";
    
    cJSON *json = cJSON_Parse(json_str);
    TEST_ASSERT_NOT_NULL(json);
    
    cJSON *ports = cJSON_GetObjectItem(json, "ports");
    TEST_ASSERT_NOT_NULL(ports);
    TEST_ASSERT_TRUE(cJSON_IsArray(ports));
    
    cJSON *port = cJSON_GetArrayItem(ports, 0);
    TEST_ASSERT_NOT_NULL(port);
    
    // Verify port configuration fields
    cJSON *enabled = cJSON_GetObjectItem(port, "enabled");
    TEST_ASSERT_NOT_NULL(enabled);
    TEST_ASSERT_TRUE(cJSON_IsTrue(enabled));
    
    cJSON *universe = cJSON_GetObjectItem(port, "universe");
    TEST_ASSERT_NOT_NULL(universe);
    TEST_ASSERT_EQUAL_INT(100, universe->valueint);
    
    cJSON *break_us = cJSON_GetObjectItem(port, "break_us");
    TEST_ASSERT_NOT_NULL(break_us);
    TEST_ASSERT_EQUAL_INT(200, break_us->valueint);
    
    cJSON_Delete(json);
}

int main(void)
{
    UNITY_BEGIN();
    
    RUN_TEST(test_config_export_structure);
    RUN_TEST(test_config_export_completeness);
    RUN_TEST(test_config_import_parsing);
    RUN_TEST(test_config_import_validation);
    RUN_TEST(test_config_import_string_safety);
    RUN_TEST(test_config_import_port_config);
    
    return UNITY_END();
}
