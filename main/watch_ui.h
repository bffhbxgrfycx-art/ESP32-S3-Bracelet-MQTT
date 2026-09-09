#pragma once
#include "lvgl.h"

/* UI 创建（LVGL_Init 之后调用） */
void watch_ui_create(void);

/* MQTT 模块收到消息后调用：把消息压入历史列表并刷新显示 */
void watch_ui_push_message(const char *msg);

/* 更新连接状态文字（WiFi/MQTT 状态变化时调用） */
void watch_ui_set_status(const char *text);

/* 更新表盘时间（由 NTP/定时任务调用，传入 "HH:MM" 格式） */
void watch_ui_set_time(const char *time_str);

/* 更新电量显示（由电量采集任务调用，传入电压值 V） */
void watch_ui_set_battery(float volts);
