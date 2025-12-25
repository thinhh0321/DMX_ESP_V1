/**
 * @file mod_web_api.c
 * @brief REST API Handlers Implementation
 * 
 * Implements all REST API endpoints for system, DMX, and network configuration.
 */

#include "mod_web_api.h"
#include "mod_web_json.h"
#include "mod_web_validation.h"
#include "mod_web_error.h"
#include "sys_mod.h"
#include "mod_net.h"
#include "mod_proto.h"
#include "mod_web_auth.h"
#include "dmx_types.h"
#include "esp_log.h"
#include "esp_http_server.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "cJSON.h"
#include <string.h>

static const char *TAG = "MOD_WEB_API";

/* ========== HELPER FUNCTIONS ========== */

/**
 * @brief Get FPS for a DMX port
 * 
 * Uses sys_get_port_fps() for accurate FPS calculation.
 */
static uint16_t get_port_fps(int port_idx)
{
    if (port_idx < 0 || port_idx >= 4) {
        return 0;
    }
    
    // Use the accurate FPS tracker from sys_mod
    return sys_get_port_fps(port_idx);
}

/* ========== SYSTEM API HANDLERS ========== */

esp_err_t mod_web_api_system_info(httpd_req_t *req)
{
    ESP_LOGD(TAG, "GET /api/sys/info");

    // Get system configuration
    const sys_config_t *cfg = sys_get_config();
    if (cfg == NULL) {
        return mod_web_error_send_500(req, "Failed to get system config");
    }

    // Get network status
    net_status_t net_status;
    net_get_status(&net_status);

    // Calculate uptime (seconds)
    int64_t uptime_us = esp_timer_get_time();
    uint32_t uptime_sec = (uint32_t)(uptime_us / 1000000);

    // Get free heap
    uint32_t free_heap = esp_get_free_heap_size();

    // Build JSON response
    // Per MOD_WEB.md: Response format should be simple, mapping to sys_config_t
    cJSON *root = cJSON_CreateObject();

    cJSON_AddStringToObject(root, "device", cfg->device_label);
    cJSON_AddStringToObject(root, "version", "4.0.0");
    cJSON_AddNumberToObject(root, "uptime", uptime_sec);
    cJSON_AddNumberToObject(root, "free_heap", free_heap);
    cJSON_AddBoolToObject(root, "eth_up", net_status.eth_connected);
    cJSON_AddBoolToObject(root, "wifi_up", net_status.wifi_connected);
    
    // IP addresses
    if (net_status.has_ip && strlen(net_status.current_ip) > 0) {
        cJSON_AddStringToObject(root, "ip", net_status.current_ip);
    } else {
        cJSON_AddItemToObject(root, "ip", cJSON_CreateNull());
    }

    // Send response
    esp_err_t ret = mod_web_json_send_response(req, root);
    cJSON_Delete(root);

    return ret;
}

esp_err_t mod_web_api_system_reboot(httpd_req_t *req)
{
    ESP_LOGI(TAG, "POST /api/sys/reboot");

    // Require auth for admin actions
    if (mod_web_auth_is_enabled()) {
        if (!mod_web_auth_check_request(req)) {
            return mod_web_error_send_401(req, "Authentication required");
        }
    }

    // Send response before rebooting
    // Per MOD_WEB.md: Response format {"status": "ok"}
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "status", "ok");

    mod_web_json_send_response(req, root);
    cJSON_Delete(root);

    // Reboot after short delay
    vTaskDelay(pdMS_TO_TICKS(100));
    esp_restart();

    return ESP_OK;
}

esp_err_t mod_web_api_system_factory(httpd_req_t *req)
{
    ESP_LOGI(TAG, "POST /api/sys/factory");

    // Require auth for admin actions
    if (mod_web_auth_is_enabled()) {
        if (!mod_web_auth_check_request(req)) {
            return mod_web_error_send_401(req, "Authentication required");
        }
    }

    // Parse request body to check for confirmation
    char buf[256];
    int ret = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (ret > 0) {
        buf[ret] = '\0';
        cJSON *json = cJSON_Parse(buf);
        if (json != NULL) {
            cJSON *confirm = cJSON_GetObjectItem(json, "confirm");
            if (confirm == NULL || !cJSON_IsTrue(confirm)) {
                cJSON_Delete(json);
                return mod_web_error_send_400(req, "Confirmation required");
            }
            cJSON_Delete(json);
        }
    }

    // Perform factory reset
    esp_err_t err = sys_factory_reset();
    if (err != ESP_OK) {
        return mod_web_error_send_500(req, "Factory reset failed");
    }

    // Send response
    // Per MOD_WEB.md: Response format {"status": "ok"}
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "status", "ok");

    mod_web_json_send_response(req, root);
    cJSON_Delete(root);

    // Reboot after short delay
    vTaskDelay(pdMS_TO_TICKS(100));
    esp_restart();

    return ESP_OK;
}

esp_err_t mod_web_api_auth_login(httpd_req_t *req)
{
    ESP_LOGI(TAG, "POST /api/auth/login");

    // Read body
    char buf[256];
    int ret = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (ret <= 0) {
        return mod_web_error_send_400(req, "Empty request body");
    }
    buf[ret] = '\0';

    cJSON *json = cJSON_Parse(buf);
    if (json == NULL) {
        return mod_web_error_send_400(req, "Invalid JSON");
    }

    cJSON *pw = cJSON_GetObjectItem(json, "password");
    if (!cJSON_IsString(pw)) {
        cJSON_Delete(json);
        return mod_web_error_send_400(req, "Missing password field");
    }

    const char *password = pw->valuestring;

    if (!mod_web_auth_verify_password(password)) {
        cJSON_Delete(json);
        return mod_web_error_send_401(req, "Invalid credentials");
    }

    // Generate token (8 hours)
    char *token = mod_web_auth_generate_token(8 * 60 * 60);
    if (!token) {
        cJSON_Delete(json);
        return mod_web_error_send_500(req, "Failed to generate token");
    }

    // Build response: { token: "...", expires_seconds: 28800 }
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "token", token);
    cJSON_AddNumberToObject(root, "expires_seconds", (double)(8 * 60 * 60));

    esp_err_t r = mod_web_json_send_response(req, root);

    cJSON_Delete(root);
    free(token);
    cJSON_Delete(json);

    return r;
}

esp_err_t mod_web_api_auth_set_password(httpd_req_t *req)
{
    ESP_LOGI(TAG, "POST /api/auth/set_password");

    // Read body
    char buf[256];
    int ret = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (ret <= 0) {
        return mod_web_error_send_400(req, "Empty request body");
    }
    buf[ret] = '\0';

    cJSON *json = cJSON_Parse(buf);
    if (json == NULL) {
        return mod_web_error_send_400(req, "Invalid JSON");
    }

    cJSON *pw = cJSON_GetObjectItem(json, "password");
    if (!cJSON_IsString(pw)) {
        cJSON_Delete(json);
        return mod_web_error_send_400(req, "Missing password field");
    }

    const char *password = pw->valuestring;

    // If auth already enabled, require auth for password change
    if (mod_web_auth_is_enabled() && !mod_web_auth_check_request(req)) {
        cJSON_Delete(json);
        return mod_web_error_send_401(req, "Authentication required to change password");
    }

    esp_err_t err = mod_web_auth_set_password(password);
    if (err != ESP_OK) {
        cJSON_Delete(json);
        return mod_web_error_send_500(req, "Failed to set password");
    }

    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "status", "ok");

    esp_err_t r = mod_web_json_send_response(req, root);
    cJSON_Delete(root);
    cJSON_Delete(json);

    return r;
}

/* ========== DMX API HANDLERS ========== */

esp_err_t mod_web_api_dmx_status(httpd_req_t *req)
{
    ESP_LOGD(TAG, "GET /api/dmx/status");

    // Get system configuration
    const sys_config_t *cfg = sys_get_config();
    if (cfg == NULL) {
        return mod_web_error_send_500(req, "Failed to get system config");
    }

    // Build JSON response
    // Per MOD_WEB.md: JSON Models mapping to sys_config_t
    cJSON *root = cJSON_CreateObject();
    cJSON *ports = cJSON_CreateArray();

    // Get activity timestamps for FPS calculation
    for (int i = 0; i < 4; i++) {
        cJSON *port = cJSON_CreateObject();
        // Copy to local variable to avoid packed member address warning
        dmx_port_cfg_t port_cfg = cfg->ports[i];

        cJSON_AddNumberToObject(port, "port", i);
        cJSON_AddNumberToObject(port, "universe", port_cfg.universe);
        cJSON_AddBoolToObject(port, "enabled", port_cfg.enabled);

        // Calculate FPS from activity with improved estimation
        uint16_t fps = get_port_fps(i);
        cJSON_AddNumberToObject(port, "fps", fps);

        // Backend type (RMT or UART - simplified)
        cJSON_AddStringToObject(port, "backend", "RMT");

        // Activity counter (simplified)
        cJSON_AddNumberToObject(port, "activity_counter", last_activity > 0 ? 1 : 0);

        cJSON_AddItemToArray(ports, port);
    }

    cJSON_AddItemToObject(root, "ports", ports);

    // Send response
    esp_err_t ret = mod_web_json_send_response(req, root);
    cJSON_Delete(root);

    return ret;
}

esp_err_t mod_web_api_dmx_config(httpd_req_t *req)
{
    ESP_LOGI(TAG, "POST /api/dmx/config");

    // Require auth for admin actions
    if (mod_web_auth_is_enabled()) {
        if (!mod_web_auth_check_request(req)) {
            return mod_web_error_send_401(req, "Authentication required");
        }
    }

    // Read request body
    char buf[512];
    int ret = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (ret <= 0) {
        return mod_web_error_send_400(req, "Empty request body");
    }
    buf[ret] = '\0';

    // Parse JSON
    cJSON *json = cJSON_Parse(buf);
    if (json == NULL) {
        return mod_web_error_send_400(req, "Invalid JSON");
    }

    // Extract and validate fields
    cJSON *port_item = cJSON_GetObjectItem(json, "port");
    cJSON *universe_item = cJSON_GetObjectItem(json, "universe");
    cJSON *enabled_item = cJSON_GetObjectItem(json, "enabled");
    cJSON *break_us_item = cJSON_GetObjectItem(json, "break_us");
    cJSON *mab_us_item = cJSON_GetObjectItem(json, "mab_us");

    if (!cJSON_IsNumber(port_item) || !cJSON_IsNumber(universe_item) || !cJSON_IsBool(enabled_item)) {
        cJSON_Delete(json);
        return mod_web_error_send_400(req, "Missing required fields");
    }

    int port = port_item->valueint;
    int universe = universe_item->valueint;
    bool enabled = cJSON_IsTrue(enabled_item);

    // Validate port
    if (!mod_web_validation_port(port)) {
        cJSON_Delete(json);
        return mod_web_error_send_400(req, "Invalid port (0-3)");
    }

    // Validate universe
    if (!mod_web_validation_universe(universe)) {
        cJSON_Delete(json);
        return mod_web_error_send_400(req, "Invalid universe (0-32767)");
    }

    // Build port configuration
    dmx_port_cfg_t new_cfg = {0};
    new_cfg.enabled = enabled;
    new_cfg.universe = (uint16_t)universe;
    new_cfg.protocol = PROTOCOL_ARTNET; // Default

    // Timing configuration (if provided)
    if (cJSON_IsNumber(break_us_item)) {
        int break_us = break_us_item->valueint;
        if (!mod_web_validation_break_us(break_us)) {
            cJSON_Delete(json);
            return mod_web_error_send_400(req, "Invalid break_us (88-500)");
        }
        new_cfg.timing.break_us = (uint16_t)break_us;
    } else {
        new_cfg.timing.break_us = 176; // Default
    }

    if (cJSON_IsNumber(mab_us_item)) {
        int mab_us = mab_us_item->valueint;
        if (!mod_web_validation_mab_us(mab_us)) {
            cJSON_Delete(json);
            return mod_web_error_send_400(req, "Invalid mab_us (8-100)");
        }
        new_cfg.timing.mab_us = (uint16_t)mab_us;
    } else {
        new_cfg.timing.mab_us = 12; // Default
    }

    new_cfg.timing.refresh_rate = 40; // Default

    cJSON_Delete(json);

    // Apply configuration via SYS_MOD
    esp_err_t err = sys_update_port_cfg(port, &new_cfg);
    if (err != ESP_OK) {
        return mod_web_error_send_500(req, "Failed to update port config");
    }

    // Send success response
    // Per MOD_WEB.md: Response format {"status": "ok"}
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "status", "ok");

    esp_err_t resp_ret = mod_web_json_send_response(req, root);
    cJSON_Delete(root);

    return resp_ret;
}

/* ========== NETWORK API HANDLERS ========== */

esp_err_t mod_web_api_network_status(httpd_req_t *req)
{
    ESP_LOGD(TAG, "GET /api/net/status");

    // Get network status
    net_status_t net_status;
    net_get_status(&net_status);

    // Get system configuration for SSID
    const sys_config_t *cfg = sys_get_config();
    if (cfg == NULL) {
        return mod_web_error_send_500(req, "Failed to get system config");
    }

    // Build JSON response
    // Per MOD_WEB.md: JSON Models mapping to sys_config_t
    cJSON *root = cJSON_CreateObject();

    cJSON_AddBoolToObject(root, "eth_up", net_status.eth_connected);
    cJSON_AddBoolToObject(root, "wifi_up", net_status.wifi_connected);

    // IP address (from net_status)
    if (net_status.has_ip && strlen(net_status.current_ip) > 0) {
        cJSON_AddStringToObject(root, "ip", net_status.current_ip);
    } else {
        cJSON_AddItemToObject(root, "ip", cJSON_CreateNull());
    }

    // WiFi SSID (from config)
    if (strlen(cfg->net.wifi_ssid) > 0) {
        cJSON_AddStringToObject(root, "wifi_ssid", cfg->net.wifi_ssid);
    } else {
        cJSON_AddItemToObject(root, "wifi_ssid", cJSON_CreateNull());
    }

    // Send response
    esp_err_t ret = mod_web_json_send_response(req, root);
    cJSON_Delete(root);

    return ret;
}

esp_err_t mod_web_api_network_config(httpd_req_t *req)
{
    ESP_LOGI(TAG, "POST /api/net/config");

    // Require auth for admin actions
    if (mod_web_auth_is_enabled()) {
        if (!mod_web_auth_check_request(req)) {
            return mod_web_error_send_401(req, "Authentication required");
        }
    }

    // Read request body
    char buf[1024];
    int ret = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (ret <= 0) {
        return mod_web_error_send_400(req, "Empty request body");
    }
    buf[ret] = '\0';

    // Parse JSON
    cJSON *json = cJSON_Parse(buf);
    if (json == NULL) {
        return mod_web_error_send_400(req, "Invalid JSON");
    }

    // Build network configuration
    net_config_t new_net = {0};

    // Ethernet configuration
    cJSON *eth = cJSON_GetObjectItem(json, "ethernet");
    if (eth != NULL) {
        cJSON *dhcp = cJSON_GetObjectItem(eth, "dhcp");
        cJSON *ip = cJSON_GetObjectItem(eth, "ip");
        cJSON *netmask = cJSON_GetObjectItem(eth, "netmask");
        cJSON *gateway = cJSON_GetObjectItem(eth, "gateway");

        new_net.dhcp_enabled = (dhcp != NULL && cJSON_IsTrue(dhcp));

        if (ip != NULL && cJSON_IsString(ip)) {
            strncpy(new_net.ip, ip->valuestring, sizeof(new_net.ip) - 1);
            new_net.ip[sizeof(new_net.ip) - 1] = '\0';
        }
        if (netmask != NULL && cJSON_IsString(netmask)) {
            strncpy(new_net.netmask, netmask->valuestring, sizeof(new_net.netmask) - 1);
            new_net.netmask[sizeof(new_net.netmask) - 1] = '\0';
        }
        if (gateway != NULL && cJSON_IsString(gateway)) {
            strncpy(new_net.gateway, gateway->valuestring, sizeof(new_net.gateway) - 1);
            new_net.gateway[sizeof(new_net.gateway) - 1] = '\0';
        }
    }

    // WiFi Station configuration
    cJSON *wifi_sta = cJSON_GetObjectItem(json, "wifi_sta");
    if (wifi_sta != NULL) {
        cJSON *ssid = cJSON_GetObjectItem(wifi_sta, "ssid");
        cJSON *password = cJSON_GetObjectItem(wifi_sta, "password");

        if (ssid != NULL && cJSON_IsString(ssid)) {
            strncpy(new_net.wifi_ssid, ssid->valuestring, sizeof(new_net.wifi_ssid) - 1);
            new_net.wifi_ssid[sizeof(new_net.wifi_ssid) - 1] = '\0';
        }
        if (password != NULL && cJSON_IsString(password)) {
            strncpy(new_net.wifi_pass, password->valuestring, sizeof(new_net.wifi_pass) - 1);
            new_net.wifi_pass[sizeof(new_net.wifi_pass) - 1] = '\0';
        }
    }

    cJSON_Delete(json);

    // Apply configuration via SYS_MOD
    esp_err_t err = sys_update_net_cfg(&new_net);
    if (err != ESP_OK) {
        return mod_web_error_send_500(req, "Failed to update network config");
    }
    
    // Apply changes to active network interfaces immediately
    net_reload_config();

    // Send success response
    // Per MOD_WEB.md: Response format {"status": "ok"}
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "status", "ok");
    cJSON_AddStringToObject(root, "message", "Network configuration updated and applied");

    esp_err_t resp_ret = mod_web_json_send_response(req, root);
    cJSON_Delete(root);

    return resp_ret;
}


/* ========== FILE API HANDLERS ========== */

esp_err_t mod_web_api_file_export(httpd_req_t *req)
{
    ESP_LOGD(TAG, "GET /api/file/export");

    // Require auth for sensitive config export
    if (mod_web_auth_is_enabled()) {
        if (!mod_web_auth_check_request(req)) {
            return mod_web_error_send_401(req, "Authentication required");
        }
    }

    // Get system configuration
    const sys_config_t *cfg = sys_get_config();
    if (cfg == NULL) {
        return mod_web_error_send_500(req, "Failed to get system config");
    }

    // Build JSON response with full configuration
    cJSON *root = cJSON_CreateObject();
    
    // Device info
    cJSON_AddStringToObject(root, "device_label", cfg->device_label);
    cJSON_AddNumberToObject(root, "led_brightness", cfg->led_brightness);
    
    // Network configuration
    cJSON *net = cJSON_CreateObject();
    cJSON_AddBoolToObject(net, "dhcp", cfg->net.dhcp);
    cJSON_AddStringToObject(net, "static_ip", cfg->net.static_ip);
    cJSON_AddStringToObject(net, "static_netmask", cfg->net.static_netmask);
    cJSON_AddStringToObject(net, "static_gateway", cfg->net.static_gateway);
    cJSON_AddStringToObject(net, "wifi_ssid", cfg->net.wifi_ssid);
    cJSON_AddStringToObject(net, "wifi_psk", cfg->net.wifi_psk);
    cJSON_AddBoolToObject(net, "wifi_enabled", cfg->net.wifi_enabled);
    cJSON_AddBoolToObject(net, "eth_enabled", cfg->net.eth_enabled);
    cJSON_AddItemToObject(root, "network", net);
    
    // DMX ports configuration
    cJSON *ports = cJSON_CreateArray();
    for (int i = 0; i < 4; i++) {
        cJSON *port = cJSON_CreateObject();
        cJSON_AddNumberToObject(port, "index", i);
        cJSON_AddBoolToObject(port, "enabled", cfg->ports[i].enabled);
        cJSON_AddNumberToObject(port, "universe", cfg->ports[i].universe);
        cJSON_AddNumberToObject(port, "protocol", cfg->ports[i].protocol);
        cJSON_AddNumberToObject(port, "break_us", cfg->ports[i].timing.break_us);
        cJSON_AddNumberToObject(port, "mab_us", cfg->ports[i].timing.mab_us);
        cJSON_AddItemToArray(ports, port);
    }
    cJSON_AddItemToObject(root, "ports", ports);
    
    // Failsafe configuration
    cJSON *failsafe = cJSON_CreateObject();
    cJSON_AddNumberToObject(failsafe, "mode", cfg->failsafe.mode);
    cJSON_AddNumberToObject(failsafe, "timeout_ms", cfg->failsafe.timeout_ms);
    cJSON_AddItemToObject(root, "failsafe", failsafe);

    // Set headers for file download
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Content-Disposition", "attachment; filename=\"dmx_config.json\"");

    esp_err_t resp_ret = mod_web_json_send_response(req, root);
    cJSON_Delete(root);

    return resp_ret;
}

esp_err_t mod_web_api_file_import(httpd_req_t *req)
{
    ESP_LOGD(TAG, "POST /api/file/import");

    // Require auth for sensitive config import
    if (mod_web_auth_is_enabled()) {
        if (!mod_web_auth_check_request(req)) {
            return mod_web_error_send_401(req, "Authentication required");
        }
    }

    // Receive JSON body
    cJSON *json = NULL;
    esp_err_t ret = mod_web_json_receive_body(req, &json);
    if (ret != ESP_OK || json == NULL) {
        return mod_web_error_send_400(req, "Invalid JSON body");
    }

    // Validate and parse configuration
    // Device label
    cJSON *device_label = cJSON_GetObjectItem(json, "device_label");
    if (device_label && cJSON_IsString(device_label)) {
        sys_update_device_label(cJSON_GetStringValue(device_label));
    }

    // LED brightness
    cJSON *led_brightness = cJSON_GetObjectItem(json, "led_brightness");
    if (led_brightness && cJSON_IsNumber(led_brightness)) {
        sys_update_led_brightness((uint8_t)led_brightness->valueint);
    }

    // Network configuration
    cJSON *net_obj = cJSON_GetObjectItem(json, "network");
    if (net_obj && cJSON_IsObject(net_obj)) {
        net_config_t new_net = {0};
        
        cJSON *dhcp = cJSON_GetObjectItem(net_obj, "dhcp");
        if (dhcp && cJSON_IsBool(dhcp)) {
            new_net.dhcp = cJSON_IsTrue(dhcp);
        }
        
        cJSON *static_ip = cJSON_GetObjectItem(net_obj, "static_ip");
        if (static_ip && cJSON_IsString(static_ip)) {
            strncpy(new_net.static_ip, cJSON_GetStringValue(static_ip), sizeof(new_net.static_ip) - 1);
            new_net.static_ip[sizeof(new_net.static_ip) - 1] = '\0'; // Ensure null termination
        }
        
        cJSON *static_netmask = cJSON_GetObjectItem(net_obj, "static_netmask");
        if (static_netmask && cJSON_IsString(static_netmask)) {
            strncpy(new_net.static_netmask, cJSON_GetStringValue(static_netmask), sizeof(new_net.static_netmask) - 1);
            new_net.static_netmask[sizeof(new_net.static_netmask) - 1] = '\0';
        }
        
        cJSON *static_gateway = cJSON_GetObjectItem(net_obj, "static_gateway");
        if (static_gateway && cJSON_IsString(static_gateway)) {
            strncpy(new_net.static_gateway, cJSON_GetStringValue(static_gateway), sizeof(new_net.static_gateway) - 1);
            new_net.static_gateway[sizeof(new_net.static_gateway) - 1] = '\0';
        }
        
        cJSON *wifi_ssid = cJSON_GetObjectItem(net_obj, "wifi_ssid");
        if (wifi_ssid && cJSON_IsString(wifi_ssid)) {
            strncpy(new_net.wifi_ssid, cJSON_GetStringValue(wifi_ssid), sizeof(new_net.wifi_ssid) - 1);
            new_net.wifi_ssid[sizeof(new_net.wifi_ssid) - 1] = '\0';
        }
        
        cJSON *wifi_psk = cJSON_GetObjectItem(net_obj, "wifi_psk");
        if (wifi_psk && cJSON_IsString(wifi_psk)) {
            strncpy(new_net.wifi_psk, cJSON_GetStringValue(wifi_psk), sizeof(new_net.wifi_psk) - 1);
            new_net.wifi_psk[sizeof(new_net.wifi_psk) - 1] = '\0';
        }
        
        cJSON *wifi_enabled = cJSON_GetObjectItem(net_obj, "wifi_enabled");
        if (wifi_enabled && cJSON_IsBool(wifi_enabled)) {
            new_net.wifi_enabled = cJSON_IsTrue(wifi_enabled);
        }
        
        cJSON *eth_enabled = cJSON_GetObjectItem(net_obj, "eth_enabled");
        if (eth_enabled && cJSON_IsBool(eth_enabled)) {
            new_net.eth_enabled = cJSON_IsTrue(eth_enabled);
        }
        
        sys_update_net_cfg(&new_net);
    }

    // DMX ports configuration
    cJSON *ports = cJSON_GetObjectItem(json, "ports");
    if (ports && cJSON_IsArray(ports)) {
        int port_count = cJSON_GetArraySize(ports);
        for (int i = 0; i < port_count && i < 4; i++) {
            cJSON *port_obj = cJSON_GetArrayItem(ports, i);
            if (!cJSON_IsObject(port_obj)) continue;
            
            dmx_port_cfg_t port_cfg = {0};
            
            cJSON *enabled = cJSON_GetObjectItem(port_obj, "enabled");
            if (enabled && cJSON_IsBool(enabled)) {
                port_cfg.enabled = cJSON_IsTrue(enabled);
            }
            
            cJSON *universe = cJSON_GetObjectItem(port_obj, "universe");
            if (universe && cJSON_IsNumber(universe)) {
                port_cfg.universe = (uint16_t)universe->valueint;
            }
            
            cJSON *protocol = cJSON_GetObjectItem(port_obj, "protocol");
            if (protocol && cJSON_IsNumber(protocol)) {
                port_cfg.protocol = (uint8_t)protocol->valueint;
            }
            
            cJSON *break_us = cJSON_GetObjectItem(port_obj, "break_us");
            if (break_us && cJSON_IsNumber(break_us)) {
                port_cfg.timing.break_us = (uint16_t)break_us->valueint;
            }
            
            cJSON *mab_us = cJSON_GetObjectItem(port_obj, "mab_us");
            if (mab_us && cJSON_IsNumber(mab_us)) {
                port_cfg.timing.mab_us = (uint16_t)mab_us->valueint;
            }
            
            // Validate and update
            esp_err_t err = sys_update_port_cfg(i, &port_cfg);
            if (err != ESP_OK) {
                ESP_LOGW(TAG, "Failed to update port %d config", i);
            }
        }
    }

    // Failsafe configuration
    bool failsafe_skipped = false;
    cJSON *failsafe_obj = cJSON_GetObjectItem(json, "failsafe");
    if (failsafe_obj && cJSON_IsObject(failsafe_obj)) {
        // Note: Need to add sys_update_failsafe_cfg function to sys_mod
        // For now, log that it's not yet implemented
        ESP_LOGW(TAG, "Failsafe config import not yet implemented in sys_mod");
        failsafe_skipped = true;
    }

    cJSON_Delete(json);

    // Force save to NVS
    sys_save_config_now();

    // Send success response with note about skipped items
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "status", "ok");
    if (failsafe_skipped) {
        cJSON_AddStringToObject(root, "warning", "Failsafe configuration was not imported (not yet implemented)");
    }
    cJSON_AddStringToObject(root, "message", "Configuration imported successfully");

    esp_err_t resp_ret = mod_web_json_send_response(req, root);
    cJSON_Delete(root);

    return resp_ret;
}

/* ========== OTA API HANDLER ========== */

esp_err_t mod_web_api_system_ota(httpd_req_t *req)
{
    ESP_LOGI(TAG, "POST /api/sys/ota - OTA Update initiated");

    // Check content length
    size_t content_len = req->content_len;
    if (content_len == 0) {
        return mod_web_error_send_400(req, "Empty firmware file");
    }

    // Note: Full OTA implementation requires esp_ota_* APIs
    // This is a placeholder that needs proper implementation
    ESP_LOGW(TAG, "OTA handler is a stub - full implementation needed");
    
    // TODO: Implement full OTA logic:
    // 1. esp_ota_begin() to start OTA partition write
    // 2. Read chunks from request body and write with esp_ota_write()
    // 3. esp_ota_end() to finalize
    // 4. esp_ota_set_boot_partition() to mark new partition
    // 5. esp_restart() to reboot
    
    // Return 501 Not Implemented (more accurate than 500)
    httpd_resp_set_status(req, "501 Not Implemented");
    httpd_resp_set_type(req, "application/json");
    
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "status", "error");
    cJSON_AddStringToObject(root, "error", "OTA functionality not yet implemented");
    
    esp_err_t ret = mod_web_json_send_response(req, root);
    cJSON_Delete(root);
    
    return ret;
}

/* ========== HEALTH/TELEMETRY API HANDLER ========== */

esp_err_t mod_web_api_system_health(httpd_req_t *req)
{
    ESP_LOGD(TAG, "GET /api/sys/health");

    // Get system configuration
    const sys_config_t *cfg = sys_get_config();
    if (cfg == NULL) {
        return mod_web_error_send_500(req, "Failed to get system config");
    }

    // Get network status
    net_status_t net_status;
    net_get_status(&net_status);

    // Calculate uptime
    int64_t uptime_us = esp_timer_get_time();
    uint32_t uptime_sec = (uint32_t)(uptime_us / 1000000);

    // Get memory info
    uint32_t free_heap = esp_get_free_heap_size();
    uint32_t min_free_heap = esp_get_minimum_free_heap_size();

    // Build JSON response
    cJSON *root = cJSON_CreateObject();

    // System status
    cJSON_AddNumberToObject(root, "uptime", uptime_sec);
    cJSON_AddNumberToObject(root, "free_heap", free_heap);
    cJSON_AddNumberToObject(root, "min_free_heap", min_free_heap);

    // Network status
    cJSON *network = cJSON_CreateObject();
    cJSON_AddBoolToObject(network, "eth_connected", net_status.eth_connected);
    cJSON_AddBoolToObject(network, "wifi_connected", net_status.wifi_connected);
    cJSON_AddBoolToObject(network, "has_ip", net_status.has_ip);
    if (net_status.has_ip) {
        cJSON_AddStringToObject(network, "ip", net_status.current_ip);
    }
    cJSON_AddItemToObject(root, "network", network);

    // DMX port health
    cJSON *ports = cJSON_CreateArray();
    for (int i = 0; i < 4; i++) {
        if (!cfg->ports[i].enabled) continue;
        
        cJSON *port = cJSON_CreateObject();
        cJSON_AddNumberToObject(port, "port", i);
        cJSON_AddNumberToObject(port, "universe", cfg->ports[i].universe);
        
        // Calculate activity status
        int64_t last_activity = sys_get_last_activity(i);
        int64_t now = esp_timer_get_time();
        int64_t time_since_activity = (now - last_activity) / 1000; // Convert to ms
        
        cJSON_AddNumberToObject(port, "last_activity_ms", (int)time_since_activity);
        cJSON_AddBoolToObject(port, "active", time_since_activity < 2000);
        
        // FPS estimate
        uint16_t fps = get_port_fps(i);
        cJSON_AddNumberToObject(port, "fps", fps);
        
        cJSON_AddItemToArray(ports, port);
    }
    cJSON_AddItemToObject(root, "ports", ports);

    // Protocol telemetry
    cJSON *telemetry = cJSON_CreateObject();
    
    // Get protocol metrics
    mod_proto_metrics_t proto_metrics;
    mod_proto_get_metrics(&proto_metrics);
    
    cJSON_AddNumberToObject(telemetry, "malformed_artnet_packets", proto_metrics.malformed_artnet_packets);
    cJSON_AddNumberToObject(telemetry, "malformed_sacn_packets", proto_metrics.malformed_sacn_packets);
    cJSON_AddNumberToObject(telemetry, "socket_errors", proto_metrics.socket_errors);
    cJSON_AddNumberToObject(telemetry, "igmp_failures", proto_metrics.igmp_failures);
    
    cJSON_AddItemToObject(root, "telemetry", telemetry);

    esp_err_t resp_ret = mod_web_json_send_response(req, root);
    cJSON_Delete(root);

    return resp_ret;
}
