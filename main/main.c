#include "ST7789.h"
#include "LVGL_Driver.h"
#include "watch_ui.h"
#include "mqtt_app.h"

void app_main(void)
{
    /* 1. 初始化屏幕（SPI + ST7789 + 背光） */
    LCD_Init();

    /* 2. 初始化 LVGL 图形引擎 */
    LVGL_Init();

    /* 3. 创建 UI 页面（状态栏 + 消息卡片） */
    watch_ui_create();

    /* 4. 启动 WiFi + MQTT（独立任务，跑在核0） */
    wifi_mqtt_start();

    /* 5. LVGL 渲染主循环 */
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(10));
        lv_timer_handler();
    }
}
