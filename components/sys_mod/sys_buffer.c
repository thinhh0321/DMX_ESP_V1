/**
 * @file sys_buffer.c
 * @brief DMX buffer management and activity tracking
 */

#include "sys_mod.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include <string.h>

static const char* TAG = "SYS_BUF";

/* Forward declaration */
extern sys_state_t* sys_get_state(void);

/* ========== FPS TRACKING ========== */

#define FPS_WINDOW_SIZE 100  // Track last 100 packets

typedef struct {
    int64_t timestamps[FPS_WINDOW_SIZE];  // Circular buffer of timestamps
    uint32_t write_idx;                    // Current write position
    uint32_t count;                        // Number of samples (0-100)
} fps_tracker_t;

static fps_tracker_t s_fps_trackers[SYS_MAX_PORTS] = {0};

static void fps_tracker_add_sample(int port_idx, int64_t timestamp) {
    if (port_idx < 0 || port_idx >= SYS_MAX_PORTS) return;
    
    fps_tracker_t* tracker = &s_fps_trackers[port_idx];
    
    // Add timestamp to circular buffer
    tracker->timestamps[tracker->write_idx] = timestamp;
    tracker->write_idx = (tracker->write_idx + 1) % FPS_WINDOW_SIZE;
    
    // Update count (max 100)
    if (tracker->count < FPS_WINDOW_SIZE) {
        tracker->count++;
    }
}

static uint16_t fps_tracker_calculate(int port_idx) {
    if (port_idx < 0 || port_idx >= SYS_MAX_PORTS) return 0;
    
    fps_tracker_t* tracker = &s_fps_trackers[port_idx];
    
    // Need at least 2 samples to calculate FPS
    if (tracker->count < 2) return 0;
    
    // Calculate the index of the oldest sample by going back tracker->count 
    // positions from the current write position in the circular buffer
    uint32_t oldest_idx = (tracker->write_idx + FPS_WINDOW_SIZE - tracker->count) % FPS_WINDOW_SIZE;
    uint32_t newest_idx = (tracker->write_idx + FPS_WINDOW_SIZE - 1) % FPS_WINDOW_SIZE;
    
    int64_t oldest_time = tracker->timestamps[oldest_idx];
    int64_t newest_time = tracker->timestamps[newest_idx];
    
    int64_t time_diff = newest_time - oldest_time;
    
    // Avoid division by zero
    if (time_diff <= 0) return 0;
    
    // Calculate FPS: (count - 1) packets / time_diff_seconds
    // time_diff is in microseconds, so divide by 1,000,000
    // Note: tracker->count is capped at 100, so (count - 1) * 1000000 <= 99,000,000 (no overflow)
    uint64_t fps = ((uint64_t)(tracker->count - 1) * 1000000ULL) / (uint64_t)time_diff;
    
    // Cap at reasonable max
    if (fps > 200) fps = 200;
    
    return (uint16_t)fps;
}

/* ========== BUFFER INITIALIZATION ========== */

esp_err_t sys_buffer_init(void) {
    sys_state_t* state = sys_get_state();
    
    ESP_LOGI(TAG, "Allocating DMX buffers...");
    
    for (int i = 0; i < SYS_MAX_PORTS; i++) {
        // Allocate DMA-capable memory
        state->dmx_buffers[i] = heap_caps_malloc(DMX_UNIVERSE_SIZE, MALLOC_CAP_DMA);
        
        if (!state->dmx_buffers[i]) {
            ESP_LOGE(TAG, "Failed to allocate buffer for port %d", i);
            
            // Cleanup already allocated buffers
            for (int j = 0; j < i; j++) {
                free(state->dmx_buffers[j]);
                state->dmx_buffers[j] = NULL;
            }
            
            return ESP_ERR_NO_MEM;
        }
        
        // Initialize to zeros
        memset(state->dmx_buffers[i], 0, DMX_UNIVERSE_SIZE);
        
        // Initialize activity timestamp
        state->last_activity[i] = 0;
        
        ESP_LOGI(TAG, "Buffer %d allocated at %p", i, state->dmx_buffers[i]);
    }
    
    ESP_LOGI(TAG, "All DMX buffers allocated successfully");
    return ESP_OK;
}

/* ========== BUFFER ACCESS ========== */

uint8_t* sys_get_dmx_buffer(int port_idx) {
    if (port_idx < 0 || port_idx >= SYS_MAX_PORTS) {
        ESP_LOGE(TAG, "Invalid port index: %d", port_idx);
        return NULL;
    }
    
    sys_state_t* state = sys_get_state();
    return state->dmx_buffers[port_idx];
}

/* ========== ACTIVITY TRACKING ========== */

void sys_notify_activity(int port_idx) {
    if (port_idx < 0 || port_idx >= SYS_MAX_PORTS) {
        return; // Silent fail for performance
    }
    
    int64_t now = esp_timer_get_time();
    
    sys_state_t* state = sys_get_state();
    state->last_activity[port_idx] = now;
    
    // Track FPS
    fps_tracker_add_sample(port_idx, now);
}

int64_t sys_get_last_activity(int port_idx) {
    if (port_idx < 0 || port_idx >= SYS_MAX_PORTS) {
        return 0;
    }
    
    sys_state_t* state = sys_get_state();
    return state->last_activity[port_idx];
}

uint16_t sys_get_port_fps(int port_idx) {
    return fps_tracker_calculate(port_idx);
}
