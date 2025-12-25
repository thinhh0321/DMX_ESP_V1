/**
 * @file sys_config.c
 * @brief Configuration management with thread safety
 */

#include "sys_mod.h"
#include "mod_dmx.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_crc.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <string.h>

static const char* TAG = "SYS_CFG";

/* ========== GLOBAL VARIABLES ========== */

// Global configuration (512 bytes in .bss)
static sys_config_t g_sys_config;

// Runtime state
static sys_state_t g_sys_state;

// Default configuration template
static const sys_config_t DEFAULT_CONFIG = {
    .magic_number = SYS_CONFIG_MAGIC,
    .version = SYS_CONFIG_VERSION,
    .device_label = "DMX-Node-V4",
    .led_brightness = 50,
    .reserved1 = {0},
    
    .net = {
        .dhcp_enabled = true,
        .ip = "192.168.1.100",
        .netmask = "255.255.255.0",
        .gateway = "192.168.1.1",
        .wifi_ssid = "",
        .wifi_pass = "",
        .wifi_channel = 6,
        .wifi_tx_power = 78,  // Max power
        .hostname = "dmx-node",
        .reserved = {0}
    },
    
    .ports = {
        {.enabled = true, .protocol = PROTOCOL_ARTNET, .universe = 0, .rdm_enabled = false, 
         .timing = {.break_us = 176, .mab_us = 12, .refresh_rate = 40}},
        {.enabled = true, .protocol = PROTOCOL_ARTNET, .universe = 1, .rdm_enabled = false,
         .timing = {.break_us = 176, .mab_us = 12, .refresh_rate = 40}},
        {.enabled = false, .protocol = PROTOCOL_ARTNET, .universe = 2, .rdm_enabled = false,
         .timing = {.break_us = 176, .mab_us = 12, .refresh_rate = 40}},
        {.enabled = false, .protocol = PROTOCOL_ARTNET, .universe = 3, .rdm_enabled = false,
         .timing = {.break_us = 176, .mab_us = 12, .refresh_rate = 40}}
    },
    
    .failsafe = {
        .mode = FAILSAFE_HOLD,
        .timeout_ms = 2000,
        .has_snapshot = false
    },
    
    .reserved2 = {0},
    .crc32 = 0  // Calculated before save
};

/* ========== FORWARD DECLARATIONS ========== */
esp_err_t sys_load_config_from_nvs(void);
esp_err_t sys_save_config_to_nvs(void);
void sys_lazy_save_callback(void* arg);  // Non-static, used by timer in sys_setup.c
uint32_t sys_calculate_config_crc(const sys_config_t* cfg);  // Non-static for sys_nvs.c

/* ========== CONFIGURATION ACCESS ========== */

const sys_config_t* sys_get_config(void) {
    return &g_sys_config;
}

esp_err_t sys_update_port_cfg(int port_idx, const dmx_port_cfg_t* new_cfg) {
    // Input validation
    if (port_idx < 0 || port_idx >= SYS_MAX_PORTS) {
        ESP_LOGE(TAG, "Invalid port index: %d", port_idx);
        return ESP_ERR_INVALID_ARG;
    }
    if (!new_cfg) {
        ESP_LOGE(TAG, "NULL config pointer");
        return ESP_ERR_INVALID_ARG;
    }
    
    // Range validation
    if (new_cfg->timing.break_us < 88 || new_cfg->timing.break_us > 500) {
        ESP_LOGE(TAG, "Invalid break_us: %d (must be 88-500)", new_cfg->timing.break_us);
        return ESP_ERR_INVALID_ARG;
    }
    if (new_cfg->timing.mab_us < 8 || new_cfg->timing.mab_us > 100) {
        ESP_LOGE(TAG, "Invalid mab_us: %d (must be 8-100)", new_cfg->timing.mab_us);
        return ESP_ERR_INVALID_ARG;
    }
    if (new_cfg->timing.refresh_rate < 20 || new_cfg->timing.refresh_rate > 44) {
        ESP_LOGW(TAG, "Refresh rate %d out of range, clamping to 20-44Hz", new_cfg->timing.refresh_rate);
        // Clamp instead of rejecting
    }
    
    // Critical Section Start
    SemaphoreHandle_t mutex = (SemaphoreHandle_t)g_sys_state.config_mutex;
    xSemaphoreTake(mutex, portMAX_DELAY);
    
    // Apply changes
    memcpy(&g_sys_config.ports[port_idx], new_cfg, sizeof(dmx_port_cfg_t));
    
    // Mark dirty
    g_sys_state.config_dirty = true;
    g_sys_state.last_change_time = esp_timer_get_time();
    
    xSemaphoreGive(mutex);
    // Critical Section End
    
    // Trigger lazy save timer (5 seconds)
    esp_timer_handle_t timer = (esp_timer_handle_t)g_sys_state.save_timer;
    esp_timer_stop(timer);
    esp_timer_start_once(timer, 5000000); // 5s in microseconds
    
    // Apply timing changes immediately to DMX driver
    dmx_apply_new_timing(port_idx, &new_cfg->timing);
    
    ESP_LOGI(TAG, "Port %d config updated (Universe=%d, Protocol=%d)", 
             port_idx, new_cfg->universe, new_cfg->protocol);
    return ESP_OK;
}

esp_err_t sys_update_net_cfg(const net_config_t* new_net) {
    if (!new_net) return ESP_ERR_INVALID_ARG;
    
    SemaphoreHandle_t mutex = (SemaphoreHandle_t)g_sys_state.config_mutex;
    xSemaphoreTake(mutex, portMAX_DELAY);
    
    memcpy(&g_sys_config.net, new_net, sizeof(net_config_t));
    g_sys_state.config_dirty = true;
    g_sys_state.last_change_time = esp_timer_get_time();
    
    xSemaphoreGive(mutex);
    
    esp_timer_handle_t timer = (esp_timer_handle_t)g_sys_state.save_timer;
    esp_timer_stop(timer);
    esp_timer_start_once(timer, 5000000);
    
    ESP_LOGI(TAG, "Network config updated");
    return ESP_OK;
}

esp_err_t sys_update_device_label(const char* label) {
    if (!label) return ESP_ERR_INVALID_ARG;
    
    SemaphoreHandle_t mutex = (SemaphoreHandle_t)g_sys_state.config_mutex;
    xSemaphoreTake(mutex, portMAX_DELAY);
    
    strncpy(g_sys_config.device_label, label, sizeof(g_sys_config.device_label) - 1);
    g_sys_config.device_label[sizeof(g_sys_config.device_label) - 1] = '\0';
    g_sys_state.config_dirty = true;
    
    xSemaphoreGive(mutex);
    
    esp_timer_handle_t timer = (esp_timer_handle_t)g_sys_state.save_timer;
    esp_timer_stop(timer);
    esp_timer_start_once(timer, 5000000);
    
    ESP_LOGI(TAG, "Device label updated: %s", label);
    return ESP_OK;
}

esp_err_t sys_update_led_brightness(uint8_t brightness) {
    if (brightness > 100) {
        ESP_LOGW(TAG, "Brightness %d > 100, clamping", brightness);
        brightness = 100;
    }
    
    SemaphoreHandle_t mutex = (SemaphoreHandle_t)g_sys_state.config_mutex;
    xSemaphoreTake(mutex, portMAX_DELAY);
    
    g_sys_config.led_brightness = brightness;
    g_sys_state.config_dirty = true;
    
    xSemaphoreGive(mutex);
    
    esp_timer_handle_t timer = (esp_timer_handle_t)g_sys_state.save_timer;
    esp_timer_stop(timer);
    esp_timer_start_once(timer, 5000000);
    
    return ESP_OK;
}

esp_err_t sys_save_config_now(void) {
    ESP_LOGI(TAG, "Force save config to NVS");
    esp_err_t ret = sys_save_config_to_nvs();
    if (ret == ESP_OK) {
        g_sys_state.config_dirty = false;
    }
    return ret;
}

/* ========== CRC CALCULATION ========== */

uint32_t sys_calculate_config_crc(const sys_config_t* cfg) {
    // CRC over entire struct except the crc32 field itself (last 4 bytes)
    return esp_crc32_le(0, (uint8_t*)cfg, sizeof(sys_config_t) - sizeof(uint32_t));
}

/* ========== LAZY SAVE CALLBACK ========== */

void sys_lazy_save_callback(void* arg) {
    (void)arg;
    
    if (!g_sys_state.config_dirty) {
        return; // Nothing to save
    }
    
    ESP_LOGI(TAG, "Lazy save triggered");
    esp_err_t ret = sys_save_config_to_nvs();
    if (ret == ESP_OK) {
        g_sys_state.config_dirty = false;
        ESP_LOGI(TAG, "Config saved successfully");
    } else {
        ESP_LOGE(TAG, "Failed to save config: %d", ret);
    }
}

/* ========== GETTERS FOR INTERNAL USE ========== */

// Used by other sys_mod files
sys_state_t* sys_get_state(void) {
    return &g_sys_state;
}

sys_config_t* sys_get_config_mutable(void) {
    return &g_sys_config;
}

const sys_config_t* sys_get_default_config(void) {
    return &DEFAULT_CONFIG;
}
