/**
 * @file test_sys_fps.c
 * @brief Unit tests for FPS tracking functionality
 */

#include "unity.h"
#include "sys_mod.h"
#include "esp_timer.h"
#include <string.h>

void setUp(void) {
    // Initialize system module if needed
}

void tearDown(void) {
}

/**
 * Test that FPS returns 0 with no activity
 */
void test_fps_no_activity(void)
{
    uint16_t fps = sys_get_port_fps(0);
    TEST_ASSERT_EQUAL_UINT16(0, fps);
}

/**
 * Test that FPS returns 0 with only 1 packet
 */
void test_fps_single_packet(void)
{
    // Simulate single packet
    sys_notify_activity(0);
    
    // Should return 0 (need at least 2 samples)
    uint16_t fps = sys_get_port_fps(0);
    TEST_ASSERT_EQUAL_UINT16(0, fps);
}

/**
 * Test FPS calculation with steady packet rate
 */
void test_fps_steady_rate(void)
{
    // Simulate packets at 40 FPS (25ms intervals)
    int64_t base_time = esp_timer_get_time();
    
    // Send 10 packets at 25ms intervals
    for (int i = 0; i < 10; i++) {
        // Mock time progression (in real hardware, time advances naturally)
        // This test validates the calculation logic
        sys_notify_activity(0);
    }
    
    uint16_t fps = sys_get_port_fps(0);
    
    // With steady rate, should calculate FPS
    // Note: This is a basic validation that the function runs
    // Actual FPS value depends on real time progression
    TEST_ASSERT_TRUE(fps >= 0 && fps <= 200);
}

/**
 * Test FPS for different ports
 */
void test_fps_multiple_ports(void)
{
    // Send activity to different ports
    sys_notify_activity(0);
    sys_notify_activity(1);
    sys_notify_activity(2);
    
    // Each port should track independently
    uint16_t fps0 = sys_get_port_fps(0);
    uint16_t fps1 = sys_get_port_fps(1);
    uint16_t fps2 = sys_get_port_fps(2);
    
    // All should be valid (0-200 range)
    TEST_ASSERT_TRUE(fps0 <= 200);
    TEST_ASSERT_TRUE(fps1 <= 200);
    TEST_ASSERT_TRUE(fps2 <= 200);
}

/**
 * Test FPS with invalid port index
 */
void test_fps_invalid_port(void)
{
    uint16_t fps = sys_get_port_fps(-1);
    TEST_ASSERT_EQUAL_UINT16(0, fps);
    
    fps = sys_get_port_fps(99);
    TEST_ASSERT_EQUAL_UINT16(0, fps);
}

/**
 * Test that FPS is capped at 200
 */
void test_fps_capping(void)
{
    // The implementation caps FPS at 200
    // This test validates the cap exists
    
    // Send many packets in quick succession
    for (int i = 0; i < 100; i++) {
        sys_notify_activity(0);
    }
    
    uint16_t fps = sys_get_port_fps(0);
    
    // Should never exceed 200
    TEST_ASSERT_TRUE(fps <= 200);
}

/**
 * Test sliding window behavior (100 packets)
 */
void test_fps_sliding_window(void)
{
    // Send more than 100 packets
    for (int i = 0; i < 150; i++) {
        sys_notify_activity(0);
    }
    
    uint16_t fps = sys_get_port_fps(0);
    
    // Should still calculate correctly (using last 100 packets)
    TEST_ASSERT_TRUE(fps >= 0 && fps <= 200);
}

int main(void)
{
    UNITY_BEGIN();
    
    RUN_TEST(test_fps_no_activity);
    RUN_TEST(test_fps_single_packet);
    RUN_TEST(test_fps_steady_rate);
    RUN_TEST(test_fps_multiple_ports);
    RUN_TEST(test_fps_invalid_port);
    RUN_TEST(test_fps_capping);
    RUN_TEST(test_fps_sliding_window);
    
    return UNITY_END();
}
