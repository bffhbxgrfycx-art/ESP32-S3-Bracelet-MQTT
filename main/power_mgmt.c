#include "power_mgmt.h"
#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "ST7789.h"
#include "QMI8658.h"

static const char *TAG = "PWR";

/* 无操作自动息屏时间（毫秒） */
#define SCREEN_OFF_MS       10000   /* 10 秒无操作自动息屏 */

/* 背光亮度（亮屏时的亮度，0-100） */
#define BACKLIGHT_ON_LEVEL  90

/* 抬腕检测阈值：手腕翻转时，加速度矢量的方向会明显变化。
 * 用「相邻两次采样加速度矢量的夹角/方向差」判断，比绝对幅值更稳。 */
#define WRIST_FLIP_THRESH   0.6f    /* 方向变化阈值（归一化矢量点积下降幅度） */

static volatile bool     s_screen_on   = true;   /* 当前是否点亮 */
static volatile uint32_t s_last_active_ms = 0;   /* 最近一次活动时间戳 */

/* 抬腕检测状态：上一次的归一化重力方向 */
static float s_prev_ax = 0.0f, s_prev_ay = 0.0f, s_prev_az = 0.0f;
static bool  s_prev_valid = false;

void backlight_wake(void)
{
    if (!s_screen_on) {
        Set_Backlight(BACKLIGHT_ON_LEVEL);
        s_screen_on = true;
        ESP_LOGI(TAG, "screen wake");
    }
    /* 无论是否已亮，都刷新活动时间戳，延后息屏 */
    s_last_active_ms = (uint32_t)(esp_timer_get_time() / 1000);
}

void power_mgmt_poll(void)
{
    /* 1. 抬腕检测：复用 sensor 任务已通过 getAccelerometer() 更新的全局 Accel，
     * 这里不重复读 I2C。 */
    float ax = Accel.x, ay = Accel.y, az = Accel.z;

    /* 归一化重力矢量（加速度模长 ≈ 1g 时即静止/平稳，方向即手腕姿态） */
    float mag = ax * ax + ay * ay + az * az;
    if (mag > 0.01f) {
        float inv = 1.0f / sqrtf(mag);
        float nx = ax * inv, ny = ay * inv, nz = az * inv;

        if (s_prev_valid) {
            /* 两次方向的内积，1=方向不变，越小说明手腕翻转越大 */
            float dot = nx * s_prev_ax + ny * s_prev_ay + nz * s_prev_az;
            if (dot < WRIST_FLIP_THRESH) {
                /* 手腕明显翻转 → 抬腕亮屏 */
                backlight_wake();
            }
        }
        s_prev_ax = nx; s_prev_ay = ny; s_prev_az = nz;
        s_prev_valid = true;
    }

    /* 2. 无操作超时 → 自动息屏 */
    if (s_screen_on) {
        uint32_t now = (uint32_t)(esp_timer_get_time() / 1000);
        if ((now - s_last_active_ms) >= SCREEN_OFF_MS) {
            Set_Backlight(0);
            s_screen_on = false;
            ESP_LOGI(TAG, "screen off (idle timeout)");
        }
    }
}

bool power_mgmt_is_on(void)
{
    return s_screen_on;
}
