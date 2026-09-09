#pragma once
#include "lvgl.h"
#include "multi_button.h"   /* PressEvent 类型 */

/* UI 创建（LVGL_Init 之后调用） */
void watch_ui_create(void);

/* MQTT 模块收到消息后调用：把消息压入历史列表并刷新显示 */
void watch_ui_push_message(const char *msg);

/* 更新连接状态文字（WiFi/MQTT 状态变化时调用） */
void watch_ui_set_status(const char *text);

/* 更新设置页 WiFi / MQTT 状态文字 */
void watch_ui_set_wifi_status(const char *text);
void watch_ui_set_mqtt_status(const char *text);

/* 更新表盘时间（由 NTP/定时任务调用，传入 "HH:MM" 格式） */
void watch_ui_set_time(const char *time_str);

/* 更新表盘日期（传入 "M月d日" 和 "星期X" 两段文字） */
void watch_ui_set_date(const char *date_str, const char *week_str);

/* 在底部预览卡片显示提示文字（配网等场景），传 NULL 恢复消息预览 */
void watch_ui_show_tip(const char *tip);

/* 更新电量显示（由电量采集任务调用，传入电压值 V） */
void watch_ui_set_battery(float volts);

/* 更新步数（由 IMU 计步任务调用） */
void watch_ui_set_steps(uint32_t steps);

/* ================= 页面切换 ================= */
typedef enum {
    PAGE_WATCH = 0,   /* 主表盘 */
    PAGE_MSG,         /* 消息页 */
    PAGE_STEPS,       /* 步数/运动页 */
    PAGE_SETTINGS,    /* 设置/信息页 */
    PAGE_COUNT
} ui_page_t;

/* 按键处理入口（由按键回调调用）：
 * SINGLE_CLICK -> 下一页；DOUBLE_CLICK -> 上一页；LONG_PRESS -> 回主表盘 */
void watch_ui_on_key(PressEvent ev);
