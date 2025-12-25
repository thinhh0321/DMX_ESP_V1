/**
 * @file sys_mod.h
 * @brief SYS_MOD Public API - System Core Module
 * 
 * This module provides:
 * - Configuration management (Load/Save/Validate)
 * - DMX buffer access
 * - Routing table
 * - Activity tracking
 * - Event system
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "dmx_types.h"
#include "esp_err.h"
#include "sys_event.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ========== SYS_MOD API Contract (from SYS_MOD_API.md) ========== */

typedef enum {
    SYS_OK = 0,
    SYS_ERR_INVALID,
    SYS_ERR_BUSY,
    SYS_ERR_UNSUPPORTED,
} sys_status_t;

typedef enum {
    SYS_CMD_SET_DMX_CONFIG,
    SYS_CMD_SET_NETWORK,
    SYS_CMD_REBOOT,
    SYS_CMD_FACTORY_RESET
} sys_cmd_type_t;

typedef struct {
    bool dhcp;
    char ssid[32];
    char psk[64];
    bool eth_enabled;
} network_cfg_t;

typedef struct {
    uint32_t uptime;      // seconds
    uint8_t cpu_load;     // 0-100
    uint32_t free_heap;   // bytes
    bool eth_up;
    bool wifi_up;
} sys_system_status_t;

typedef struct {
    uint8_t port;         // DMX port index
    uint16_t universe;    // 0..63999
    bool enabled;
    uint16_t fps;
} sys_dmx_port_status_t;

/* Public API (non-blocking, see SYS_MOD_API.md for semantics) */
sys_status_t sys_get_system_status(sys_system_status_t *out);
sys_status_t sys_get_dmx_status(sys_dmx_port_status_t *out, size_t max_port);
sys_status_t sys_apply_dmx_config(const sys_dmx_port_status_t *cfg, size_t count);
sys_status_t sys_apply_network_config(const network_cfg_t *cfg);
sys_status_t sys_system_control(sys_cmd_type_t cmd);


/* ========== INITIALIZATION ========== */

/**
 * @brief Initialize SYS_MOD module
 * 
 * Call sequence:
 * 1. Initialize NVS flash
 * 2. Create mutexes
 * 3. Load configuration from NVS (or defaults)
 * 4. Allocate DMX buffers
 * 5. Create lazy save timer
 * 
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t sys_mod_init(void);

/* ========== CONFIGURATION ACCESS ========== */

/**
 * @brief Get read-only pointer to system configuration
 * 
 * Thread-safety: YES (read-only pointer)
 * Performance: O(1), no mutex
 * 
 * @return Pointer to global config (do NOT modify directly)
 */
const sys_config_t* sys_get_config(void);

/**
 * @brief Update port configuration (Hot-swap capable)
 * 
 * Validates input:
 * - break_us: 88-500
 * - mab_us: 8-100
 * - universe: 0-32767
 * 
 * Thread-safety: YES (mutex protected)
 * Triggers: Lazy save timer (5 seconds)
 * 
 * @param port_idx Port index (0-3)
 * @param new_cfg New configuration
 * @return ESP_OK if valid, ESP_ERR_INVALID_ARG otherwise
 */
esp_err_t sys_update_port_cfg(int port_idx, const dmx_port_cfg_t* new_cfg);

/**
 * @brief Update network configuration
 * 
 * Thread-safety: YES (mutex protected)
 * Note: Network changes require reboot to take effect
 * 
 * @param new_net New network configuration
 * @return ESP_OK on success
 */
esp_err_t sys_update_net_cfg(const net_config_t* new_net);

/**
 * @brief Update device label
 * 
 * Thread-safety: YES (mutex protected)
 * 
 * @param label New device label (max 31 chars)
 * @return ESP_OK on success
 */
esp_err_t sys_update_device_label(const char* label);

/**
 * @brief Update LED brightness
 * 
 * @param brightness 0-100
 * @return ESP_OK on success
 */
esp_err_t sys_update_led_brightness(uint8_t brightness);

/**
 * @brief Force immediate save to NVS (bypass lazy timer)
 * 
 * Use case: User clicks "Save" button, OTA update preparation
 * 
 * @return ESP_OK on success, ESP_ERR_NVS_* on failure
 */
esp_err_t sys_save_config_now(void);

/**
 * @brief Factory reset - erase NVS and load defaults
 * 
 * @return ESP_OK on success
 */
esp_err_t sys_factory_reset(void);

/* ========== DMX BUFFER ACCESS ========== */

/**
 * @brief Get pointer to DMX buffer for a port
 * 
 * Usage:
 * - MOD_PROTO: Write incoming packet data
 * - MOD_DMX: Read for transmission
 * 
 * Thread-safety: YES (pointer is stable, never freed)
 * Performance: O(1)
 * 
 * @param port_idx Port index (0-3)
 * @return Pointer to 512-byte buffer, or NULL if invalid port
 */
uint8_t* sys_get_dmx_buffer(int port_idx);

/**
 * @brief Notify activity on port (reset watchdog timer)
 * 
 * Called by MOD_PROTO when packet is received.
 * Used for fail-safe timeout detection.
 * 
 * Thread-safety: YES (atomic 64-bit write)
 * Performance: < 1us
 * 
 * @param port_idx Port index (0-3)
 */
void sys_notify_activity(int port_idx);

/**
 * @brief Get timestamp of last activity on port
 * 
 * @param port_idx Port index (0-3)
 * @return Timestamp in microseconds (esp_timer_get_time)
 */
int64_t sys_get_last_activity(int port_idx);

/**
 * @brief Get current FPS for a port
 * 
 * Calculates FPS based on packet timestamps over sliding window.
 * 
 * @param port_idx Port index (0-3)
 * @return FPS (frames per second), or 0 if insufficient data
 */
uint16_t sys_get_port_fps(int port_idx);

/* ========== INTERNAL / Advanced Accessors ==========
 * These helpers are used by initialization and internal modules.
 * Consider them advanced APIs; use sparingly.
 */

/**
 * @brief Get pointer to mutable runtime state (used by sys_setup)
 */
sys_state_t* sys_get_state(void);

/**
 * @brief Get pointer to mutable config for init/load routines
 */
sys_config_t* sys_get_config_mutable(void);

/**
 * @brief Get read-only pointer to the default configuration template
 */
const sys_config_t* sys_get_default_config(void);

/* ========== ROUTING ========== */

/**
 * @brief Find port index for given protocol and universe
 * 
 * Used by MOD_PROTO to route incoming packets.
 * 
 * Algorithm: Linear search through ports[] array
 * Performance: O(4) worst case
 * 
 * @param protocol PROTOCOL_ARTNET or PROTOCOL_SACN
 * @param universe Universe ID (0-32767)
 * @return Port index (0-3) if found, -1 if no match
 */
int8_t sys_route_find_port(uint8_t protocol, uint16_t universe);

/* ========== SNAPSHOT ========== */

/**
 * @brief Record current buffer state to NVS
 * 
 * Saves current DMX buffer to NVS for snapshot failsafe mode.
 * NVS key: "snap_port{port_idx}"
 * 
 * @param port_idx Port index (0-3)
 * @return ESP_OK on success
 */
esp_err_t sys_snapshot_record(int port_idx);

/**
 * @brief Restore snapshot from NVS to output buffer
 * 
 * @param port_idx Port index (0-3)
 * @param out_buffer Output buffer (512 bytes)
 * @return ESP_OK if snapshot exists, ESP_ERR_NOT_FOUND otherwise
 */
esp_err_t sys_snapshot_restore(int port_idx, uint8_t* out_buffer);

/* ========== EVENT SYSTEM ========== */

/**
 * @brief Send system event
 * 
 * Used for module coordination (e.g., MOD_LED reacts to network events)
 * 
 * @param event_id Event type (sys_event_id_t)
 * @param data Optional event data (can be NULL)
 * @param data_size Size of data
 * @return ESP_OK on success
 */
esp_err_t sys_send_event(sys_event_id_t event_id, void* data, size_t data_size);

#ifdef __cplusplus
}
#endif
