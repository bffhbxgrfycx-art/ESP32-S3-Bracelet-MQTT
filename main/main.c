#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "ST7789.h"
#include "LVGL_Driver.h"
#include "watch_ui.h"
#include "mqtt_app.h"
#include "Button_Driver.h"
#include "I2C_Driver.h"
#include "QMI8658.h"
#include "power_mgmt.h"
#include "RGB.h"

/* 按键事件队列：按键回调运行在 esp_timer 回调上下文（高优先级定时器任务），
 * 那里不能做阻塞/加锁/LVGL 操作，否则会死锁或触发看门狗重启。
 * 所以回调里只往队列塞事件，由主循环（LVGL 任务）安全地消费。 */
static QueueHandle_t s_key_queue = NULL;

/* 按键事件回调：只投递事件，不做任何 LVGL/加锁操作。
 * esp_timer 回调默认运行在 esp_timer 任务（普通任务上下文，非 ISR），
 * 用 xQueueSend 即可（队列满时丢弃，不阻塞）。 */
static void Button_SINGLE_CLICK_Callback(void* btn)
{
    PressEvent ev = SINGLE_CLICK;
    xQueueSend(s_key_queue, &ev, 0);
}
static void Button_DOUBLE_CLICK_Callback(void* btn)
{
    PressEvent ev = DOUBLE_CLICK;
    xQueueSend(s_key_queue, &ev, 0);
}
static void Button_LONG_PRESS_START_Callback(void* btn)
{
    PressEvent ev = LONG_PRESS_START;
    xQueueSend(s_key_queue, &ev, 0);
}

/* ================= RGB 灯开关 ================= */
static bool s_rgb_on = false;   /* 默认关闭省电 */

static void rgb_toggle(void)
{
    s_rgb_on = !s_rgb_on;
    if (s_rgb_on) {
        Set_RGB(20, 20, 30);     /* 柔和蓝紫光，低亮度省电 */
    } else {
        RGB_Off();
    }
    watch_ui_set_rgb_status(s_rgb_on);
}

/* ================= 来消息闪灯通知 =================
 * 收到新消息时，RGB 灯闪烁几下提醒，闪完恢复到之前的开关状态。
 * 用独立任务做，避免阻塞 MQTT 事件回调。 */
static void rgb_flash_task(void *arg)
{
    /* 闪烁 3 次蓝色提醒 */
    for (int i = 0; i < 3; i++) {
        Set_RGB(40, 120, 255);
        vTaskDelay(pdMS_TO_TICKS(120));
        RGB_Off();
        vTaskDelay(pdMS_TO_TICKS(120));
    }

    /* 恢复：如果 RGB 常亮开关是开的，恢复柔和蓝紫光，否则保持熄灭 */
    if (s_rgb_on) {
        Set_RGB(20, 20, 30);
    } else {
        RGB_Off();
    }
    vTaskDelete(NULL);
}

/* 收到新消息时调用（由 mqtt_app.c 调用，非阻塞） */
void rgb_notify(void)
{
    xTaskCreate(rgb_flash_task, "rgb_flash", 2048, NULL, 5, NULL);
}

void app_main(void)
{
    /* 1. 初始化屏幕（SPI + ST7789 + 背光） */
    LCD_Init();

    /* 2. 初始化 LVGL 图形引擎 */
    LVGL_Init();

    /* 3. 创建 UI 页面（状态栏 + 四页面） */
    watch_ui_create();

    /* 4. 初始化按键（BOOT 键，GPIO0）；按键事件经队列转发到主循环 */
    s_key_queue = xQueueCreate(8, sizeof(PressEvent));
    button_Init();
    button_attach(&BUTTON1, SINGLE_CLICK, Button_SINGLE_CLICK_Callback);
    button_attach(&BUTTON1, DOUBLE_CLICK, Button_DOUBLE_CLICK_Callback);
    button_attach(&BUTTON1, LONG_PRESS_START, Button_LONG_PRESS_START_Callback);

    /* 5. 初始化 I2C + 六轴 IMU（步数采集用） */
    I2C_Init();
    QMI8658_Init();

    /* 6. 初始化 RGB 灯（默认关闭省电），注册设置页长按切换回调 */
    RGB_Init();
    watch_ui_set_rgb_toggle_cb(rgb_toggle);
    watch_ui_set_rgb_status(false);

    /* 7. 启动 WiFi + MQTT（独立任务，跑在核0） */
    wifi_mqtt_start();

    /* 8. LVGL 渲染主循环 + 消费按键事件 */
    while (1) {
        /* 消费按键事件（在 LVGL 任务上下文做页面切换，安全） */
        PressEvent ev;
        while (xQueueReceive(s_key_queue, &ev, 0) == pdTRUE) {
            /* 息屏状态下的第一次按键只负责点亮屏幕，不触发翻页，
             * 避免「一点亮就跳到别的页面」。 */
            if (!power_mgmt_is_on()) {
                backlight_wake();
                continue;   /* 吞掉这次翻页动作 */
            }
            backlight_wake();          /* 亮屏状态下按键：刷新计时 + 正常翻页 */
            watch_ui_on_key(ev);
        }

        vTaskDelay(pdMS_TO_TICKS(10));

        /* 渲染也必须加锁，否则会与 MQTT 任务里的 UI 更新（lvgl_port_lock）
         * 并发访问 LVGL 内部状态，导致「消息已更新但屏幕不重绘」。 */
        lvgl_port_lock(0);
        lv_timer_handler();
        lvgl_port_unlock();
    }
}
