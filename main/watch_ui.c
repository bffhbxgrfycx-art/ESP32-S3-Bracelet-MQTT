#include <string.h>
#include <stdio.h>
#include "watch_ui.h"
#include "LVGL_Driver.h"

#define MSG_MAX    5           /* 最多保留 5 条历史消息 */

/* 控件 */
static lv_obj_t *ui_time_label = NULL;      /* 时间 */
static lv_obj_t *ui_battery_label = NULL;   /* 电量 */
static lv_obj_t *ui_status_label = NULL;    /* 连接状态 */
static lv_obj_t *ui_msg_list = NULL;        /* 消息列表容器 */

/* 消息环形队列 */
static char msg_buf[MSG_MAX][160];
static int  msg_count = 0;
static int  msg_head = 0;   /* 最新一条消息的下标 */

/* 刷新消息列表显示（把队列里的消息按新→旧渲染出来） */
static void msg_list_refresh(void)
{
    if (ui_msg_list == NULL) return;

    /* 清空旧的子控件 */
    lv_obj_clean(ui_msg_list);

    if (msg_count == 0) {
        lv_obj_t *empty = lv_label_create(ui_msg_list);
        lv_label_set_text(empty, "No messages");
        lv_obj_set_style_text_color(empty, lv_color_hex(0x888888), 0);
        lv_obj_center(empty);
        return;
    }

    /* 从最新到最旧渲染：i=0 是 head（最新），i 递增越旧 */
    for (int i = 0; i < msg_count; i++) {
        int idx = (msg_head - i + MSG_MAX) % MSG_MAX;

        /* 每条消息一张卡片 */
        lv_obj_t *card = lv_obj_create(ui_msg_list);
        lv_obj_set_size(card, 150, 44);
        lv_obj_set_style_radius(card, 10, 0);
        lv_obj_set_style_bg_color(card, lv_color_hex(0x2A2A3C), 0);
        lv_obj_set_style_border_width(card, 0, 0);
        lv_obj_set_style_pad_all(card, 6, 0);

        lv_obj_t *label = lv_label_create(card);
        lv_label_set_text(label, msg_buf[idx]);
        lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
        lv_obj_set_width(label, 138);
        lv_obj_set_style_text_color(label, lv_color_hex(0xFFFFFF), 0);
        lv_obj_align(label, LV_ALIGN_LEFT_MID, 0, 0);
    }
}

/* 消息入队（覆盖最旧的） */
void watch_ui_push_message(const char *msg)
{
    if (msg == NULL) return;

    /* 先移动 head 指针到新位置，再写入，这样 head 永远指向最新消息 */
    msg_head = (msg_head + 1) % MSG_MAX;
    snprintf(msg_buf[msg_head], sizeof(msg_buf[msg_head]), "%s", msg);
    if (msg_count < MSG_MAX) msg_count++;

    /* 加锁刷新 UI */
    lvgl_port_lock(0);
    msg_list_refresh();
    lvgl_port_unlock();
}

void watch_ui_set_status(const char *text)
{
    if (ui_status_label == NULL || text == NULL) return;
    lvgl_port_lock(0);
    lv_label_set_text(ui_status_label, text);
    lvgl_port_unlock();
}

void watch_ui_set_time(const char *time_str)
{
    if (ui_time_label == NULL || time_str == NULL) return;
    lvgl_port_lock(0);
    lv_label_set_text(ui_time_label, time_str);
    lvgl_port_unlock();
}

void watch_ui_set_battery(float volts)
{
    if (ui_battery_label == NULL) return;

    /* 锂电池电压 → 电量百分比粗略换算：
       4.2V ≈ 100%，3.3V ≈ 0%，线性映射 */
    int pct = (int)((volts - 3.3f) / (4.2f - 3.3f) * 100.0f);
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;

    char buf[16];
    snprintf(buf, sizeof(buf), "%d%%", pct);

    lvgl_port_lock(0);
    lv_label_set_text(ui_battery_label, buf);
    lvgl_port_unlock();
}

void watch_ui_create(void)
{
    lv_obj_t *scr = lv_scr_act();
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x14141C), 0);

    /* ============ 顶部状态栏 ============ */
    lv_obj_t *status_bar = lv_obj_create(scr);
    lv_obj_set_size(status_bar, 172, 34);
    lv_obj_align(status_bar, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_radius(status_bar, 0, 0);
    lv_obj_set_style_bg_color(status_bar, lv_color_hex(0x1C1C28), 0);
    lv_obj_set_style_border_width(status_bar, 0, 0);
    lv_obj_set_style_pad_all(status_bar, 0, 0);

    /* 时间（左） */
    ui_time_label = lv_label_create(status_bar);
    lv_label_set_text(ui_time_label, "--:--");
    lv_obj_set_style_text_font(ui_time_label, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(ui_time_label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_align(ui_time_label, LV_ALIGN_LEFT_MID, 8, 0);

    /* 电量（右） */
    ui_battery_label = lv_label_create(status_bar);
    lv_label_set_text(ui_battery_label, "--%");
    lv_obj_set_style_text_font(ui_battery_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(ui_battery_label, lv_color_hex(0x4ADE80), 0);
    lv_obj_align(ui_battery_label, LV_ALIGN_RIGHT_MID, -8, 0);

    /* 连接状态（状态栏下方，一行小字） */
    ui_status_label = lv_label_create(scr);
    lv_label_set_text(ui_status_label, "Connecting...");
    lv_obj_set_style_text_font(ui_status_label, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(ui_status_label, lv_color_hex(0x888888), 0);
    lv_obj_align(ui_status_label, LV_ALIGN_TOP_MID, 0, 38);

    /* ============ 标题 ============ */
    lv_obj_t *title = lv_label_create(scr);
    lv_label_set_text(title, "Messages");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0xFFFFFF), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 58);

    /* ============ 消息列表容器 ============ */
    ui_msg_list = lv_obj_create(scr);
    lv_obj_set_size(ui_msg_list, 160, 230);
    lv_obj_align(ui_msg_list, LV_ALIGN_TOP_MID, 0, 84);
    lv_obj_set_style_bg_color(ui_msg_list, lv_color_hex(0x14141C), 0);
    lv_obj_set_style_border_width(ui_msg_list, 0, 0);
    lv_obj_set_style_pad_all(ui_msg_list, 0, 0);
    lv_obj_set_flex_flow(ui_msg_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(ui_msg_list, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    /* 初始空状态 */
    msg_list_refresh();
}
