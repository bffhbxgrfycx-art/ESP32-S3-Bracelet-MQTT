#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "ST7789.h"
#include "LVGL_Driver.h"
#include "watch_ui.h"
#include "mqtt_app.h"
#include "Button_Driver.h"
#include "I2C_Driver.h"
#include "QMI8658.h"

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

    /* 6. 启动 WiFi + MQTT（独立任务，跑在核0） */
    wifi_mqtt_start();

    /* 7. LVGL 渲染主循环 + 消费按键事件 */
    while (1) {
        /* 消费按键事件（在 LVGL 任务上下文做页面切换，安全） */
        PressEvent ev;
        while (xQueueReceive(s_key_queue, &ev, 0) == pdTRUE) {
            watch_ui_on_key(ev);
        }

        vTaskDelay(pdMS_TO_TICKS(10));
        lv_timer_handler();
    }
}
