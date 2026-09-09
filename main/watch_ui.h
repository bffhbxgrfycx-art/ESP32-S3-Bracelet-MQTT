#pragma once
#include "lvgl.h"
#include "multi_button.h"   /* PressEvent 类型 */

/* UI 创建（LVGL_Init 之后调用） */
void watch_ui_create(void);

/* MQTT 模块收到消息后调用：把消息压入历史列表并刷新显示 */
void watch_ui_push_message(const char *msg);

/* 清空所有历史消息（消息页「清空消息」选项触发） */
void watch_ui_clear_messages(void);

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

/* ================= 二级导航：页面选项 =================
 * 每个页面可以声明 0 个或多个「选项」。
 * - 无选项的页：单击=下一页，双击=上一页，长按=回主表盘
 * - 有选项的页：单击=进入选项模式（聚焦第1个选项），双击=上一页，长按=回主表盘
 * - 选项模式内：单击=下一选项（末尾则退出并翻下一页），双击=上一选项（开头则退出并翻上一页），长按=触发当前选项
 */
typedef struct {
    const char *label;          /* 选项显示文字（静态字符串） */
    void (*on_activate)(void);  /* 长按触发时的回调，可为 NULL */
} ui_option_t;

/* 获取指定页面的选项数组和数量（由 watch_ui 内部维护），
 * 返回 NULL 表示该页没有选项。 */
const ui_option_t *watch_ui_get_options(ui_page_t page, int *count);

/* 按键处理入口（由按键回调调用）——二级导航状态机 */
void watch_ui_on_key(PressEvent ev);

/* 直接切回主表盘页（收到消息亮屏等场景，让预览卡片可见） */
void watch_ui_goto_watch(void);

/* 注册设置页 RGB 选项的激活回调（进入选项模式后，长按 RGB 选项时触发） */
void watch_ui_set_rgb_toggle_cb(void (*cb)(void));

/* 更新设置页 RGB 灯状态显示 */
void watch_ui_set_rgb_status(bool on);
