#include <string.h>
#include <stdio.h>
#include "watch_ui.h"
#include "LVGL_Driver.h"

#define MSG_MAX    5           /* 最多保留 5 条历史消息 */

/* ================= 控件 ================= */
static lv_obj_t *ui_time_label = NULL;      /* 顶部状态栏时间（小字） */
static lv_obj_t *ui_battery_label = NULL;   /* 电量 */
static lv_obj_t *ui_status_label = NULL;    /* 连接状态 */

/* 主表盘控件 */
static lv_obj_t *ui_clock_label = NULL;     /* 大号时间 */
static lv_obj_t *ui_date_label = NULL;      /* 日期 + 星期 */
static lv_obj_t *ui_preview_label = NULL;   /* 底部最新消息预览 */

/* 消息页控件 */
static lv_obj_t *ui_msg_list = NULL;        /* 消息列表容器 */

/* 步数页控件 */
static lv_obj_t *ui_steps_label = NULL;     /* 步数大数字 */

/* 设置页控件 */
static lv_obj_t *ui_wifi_label = NULL;      /* WiFi 状态 */
static lv_obj_t *ui_mqtt_label = NULL;      /* MQTT 状态 */
static lv_obj_t *ui_bat_label = NULL;       /* 电量详情 */
static lv_obj_t *ui_ver_label = NULL;       /* 固件版本 */

/* ================= 页面 ================= */
static lv_obj_t *page_container[PAGE_COUNT] = {NULL};
static ui_page_t s_cur_page = PAGE_WATCH;

/* ================= 消息环形队列 ================= */
static char msg_buf[MSG_MAX][160];
static int  msg_count = 0;
static int  msg_head = 0;   /* 最新一条消息的下标 */

static const char *s_tip = NULL;   /* 当前提示文字（非空时优先显示，覆盖消息预览） */

/* ================= 页面切换 ================= */
static void page_show(ui_page_t page)
{
    for (int i = 0; i < PAGE_COUNT; i++) {
        if (page_container[i] == NULL) continue;
        if (i == page) {
            lv_obj_clear_flag(page_container[i], LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(page_container[i], LV_OBJ_FLAG_HIDDEN);
        }
    }
    s_cur_page = page;
}

void watch_ui_on_key(PressEvent ev)
{
    lvgl_port_lock(0);
    switch (ev) {
    case SINGLE_CLICK:
        page_show((ui_page_t)((s_cur_page + 1) % PAGE_COUNT));
        break;
    case DOUBLE_CLICK:
        page_show((ui_page_t)((s_cur_page + PAGE_COUNT - 1) % PAGE_COUNT));
        break;
    case LONG_PRESS_START:
        page_show(PAGE_WATCH);
        break;
    default:
        break;
    }
    lvgl_port_unlock();
}

/* ================= 消息刷新 ================= */
static void preview_refresh(void)
{
    if (ui_preview_label == NULL) return;

    if (s_tip != NULL) {
        lv_label_set_text(ui_preview_label, s_tip);
        lv_obj_set_style_text_color(ui_preview_label, lv_color_hex(0xFFB6C1), 0);
    } else if (msg_count == 0) {
        lv_label_set_text(ui_preview_label, "\xE7\xAD\x89\xE5\xBE\x85\xE5\xAF\xB9\xE6\x96\xB9\xE7\x9A\x84\xE6\xB6\x88\xE6\x81\xAF\xE2\x80\xA6");  /* 等待对方的消息… */
        lv_obj_set_style_text_color(ui_preview_label, lv_color_hex(0x8A8A9A), 0);
    } else {
        lv_label_set_text(ui_preview_label, msg_buf[msg_head]);
        lv_obj_set_style_text_color(ui_preview_label, lv_color_hex(0xFFFFFF), 0);
    }
}

/* 消息页列表刷新：把 5 条消息按新→旧竖排显示 */
static void msg_list_refresh(void)
{
    if (ui_msg_list == NULL) return;

    /* 清空旧子对象 */
    lv_obj_clean(ui_msg_list);

    if (msg_count == 0) {
        lv_obj_t *empty = lv_label_create(ui_msg_list);
        lv_label_set_text(empty, "\xE6\x9A\x82\xE6\x97\xA0\xE6\xB6\x88\xE6\x81\xAF");  /* 暂无消息 */
        lv_obj_set_style_text_font(empty, &lv_font_wenkai_16, 0);
        lv_obj_set_style_text_color(empty, lv_color_hex(0x8A8A9A), 0);
        lv_obj_align(empty, LV_ALIGN_CENTER, 0, 0);
        return;
    }

    /* 从最新到最旧排列（msg_head 是最新） */
    int order[MSG_MAX];
    int n = msg_count;
    for (int i = 0; i < n; i++) {
        order[i] = (msg_head - i + MSG_MAX) % MSG_MAX;
    }

    /* 用 flex 布局竖直排列 */
    lv_obj_set_flex_flow(ui_msg_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(ui_msg_list, 6, 0);

    for (int i = 0; i < n; i++) {
        lv_obj_t *row = lv_obj_create(ui_msg_list);
        lv_obj_set_size(row, 156, 28);
        lv_obj_set_style_radius(row, 10, 0);
        lv_obj_set_style_bg_color(row, lv_color_hex(0x2A2B45), 0);
        lv_obj_set_style_border_width(row, 0, 0);
        lv_obj_set_style_pad_all(row, 6, 0);

        lv_obj_t *txt = lv_label_create(row);
        lv_label_set_text(txt, msg_buf[order[i]]);
        lv_label_set_long_mode(txt, LV_LABEL_LONG_DOT);
        lv_obj_set_width(txt, 144);
        lv_obj_set_style_text_font(txt, &lv_font_wenkai_16, 0);
        lv_obj_set_style_text_color(txt, lv_color_hex(0xFFFFFF), 0);
        lv_obj_align(txt, LV_ALIGN_LEFT_MID, 0, 0);
    }
}

/* 显示提示文字（配网等），传 NULL 恢复消息预览 */
void watch_ui_show_tip(const char *tip)
{
    s_tip = tip;
    lvgl_port_lock(0);
    preview_refresh();
    lvgl_port_unlock();
}

/* 消息入队（覆盖最旧的），并刷新底部预览 + 消息页 */
void watch_ui_push_message(const char *msg)
{
    if (msg == NULL) return;

    msg_head = (msg_head + 1) % MSG_MAX;
    snprintf(msg_buf[msg_head], sizeof(msg_buf[msg_head]), "%s", msg);
    if (msg_count < MSG_MAX) msg_count++;

    lvgl_port_lock(0);
    preview_refresh();
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

void watch_ui_set_wifi_status(const char *text)
{
    if (ui_wifi_label == NULL || text == NULL) return;
    lvgl_port_lock(0);
    lv_label_set_text(ui_wifi_label, text);
    lvgl_port_unlock();
}

void watch_ui_set_mqtt_status(const char *text)
{
    if (ui_mqtt_label == NULL || text == NULL) return;
    lvgl_port_lock(0);
    lv_label_set_text(ui_mqtt_label, text);
    lvgl_port_unlock();
}

void watch_ui_set_time(const char *time_str)
{
    if (ui_time_label == NULL || time_str == NULL) return;
    lvgl_port_lock(0);
    /* 顶部小字 + 主表盘大字同步更新 */
    lv_label_set_text(ui_time_label, time_str);
    lv_label_set_text(ui_clock_label, time_str);
    lvgl_port_unlock();
}

void watch_ui_set_date(const char *date_str, const char *week_str)
{
    if (ui_date_label == NULL) return;

    char buf[40];
    snprintf(buf, sizeof(buf), "%s  %s", date_str ? date_str : "", week_str ? week_str : "");

    lvgl_port_lock(0);
    lv_label_set_text(ui_date_label, buf);
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

    /* 设置页电量详情同步 */
    if (ui_bat_label != NULL) {
        char b2[48];
        snprintf(b2, sizeof(b2), "\xE7\x94\xB5\xE6\xB1\xA0  %d%%  (%.2fV)", pct, volts);  /* 电池  xx%  (x.xxV) */
        lv_label_set_text(ui_bat_label, b2);
    }
    lvgl_port_unlock();
}

void watch_ui_set_steps(uint32_t steps)
{
    if (ui_steps_label == NULL) return;
    lvgl_port_lock(0);
    lv_label_set_text_fmt(ui_steps_label, "%lu", (unsigned long)steps);
    lvgl_port_unlock();
}

/* ================= UI 构建 ================= */
/* 顶部状态栏 + 状态文字是所有页面共用的，直接挂在 screen 上 */
static void build_common_bar(lv_obj_t *scr)
{
    lv_obj_t *status_bar = lv_obj_create(scr);
    lv_obj_set_size(status_bar, 172, 34);
    lv_obj_align(status_bar, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_radius(status_bar, 0, 0);
    lv_obj_set_style_bg_color(status_bar, lv_color_hex(0x23243A), 0);
    lv_obj_set_style_border_width(status_bar, 0, 0);
    lv_obj_set_style_pad_all(status_bar, 0, 0);

    ui_time_label = lv_label_create(status_bar);
    lv_label_set_text(ui_time_label, "--:--");
    lv_obj_set_style_text_font(ui_time_label, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(ui_time_label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_align(ui_time_label, LV_ALIGN_LEFT_MID, 10, 0);

    ui_battery_label = lv_label_create(status_bar);
    lv_label_set_text(ui_battery_label, "--%");
    lv_obj_set_style_text_font(ui_battery_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(ui_battery_label, lv_color_hex(0x4ADE80), 0);
    lv_obj_align(ui_battery_label, LV_ALIGN_RIGHT_MID, -10, 0);

    ui_status_label = lv_label_create(scr);
    lv_label_set_text(ui_status_label, "Connecting...");
    lv_obj_set_style_text_font(ui_status_label, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(ui_status_label, lv_color_hex(0x9A9AB0), 0);
    lv_obj_align(ui_status_label, LV_ALIGN_TOP_MID, 0, 42);
}

/* 页面1：主表盘 */
static void build_page_watch(lv_obj_t *scr)
{
    lv_obj_t *page = lv_obj_create(scr);
    lv_obj_set_size(page, 172, 320);   /* 全屏（172×320 竖屏），内部坐标与 scr 一致 */
    lv_obj_align(page, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_clear_flag(page, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(page, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(page, 0, 0);
    lv_obj_set_style_pad_all(page, 0, 0);

    ui_clock_label = lv_label_create(page);
    lv_label_set_text(ui_clock_label, "--:--");
    lv_obj_set_style_text_font(ui_clock_label, &lv_font_montserrat_48, 0);
    lv_obj_set_style_text_color(ui_clock_label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_align(ui_clock_label, LV_ALIGN_TOP_MID, 0, 78);

    ui_date_label = lv_label_create(page);
    lv_label_set_text(ui_date_label, "\xE7\xAD\x89\xE5\xBE\x85\xE5\x90\x8C\xE6\xAD\xA5\xE2\x80\xA6");  /* 等待同步… */
    lv_obj_set_style_text_font(ui_date_label, &lv_font_wenkai_16, 0);
    lv_obj_set_style_text_color(ui_date_label, lv_color_hex(0xFFB6C1), 0);  /* 柔和粉 */
    lv_obj_align(ui_date_label, LV_ALIGN_TOP_MID, 0, 142);

    lv_obj_t *preview_card = lv_obj_create(page);
    lv_obj_set_size(preview_card, 156, 64);
    lv_obj_align(preview_card, LV_ALIGN_BOTTOM_MID, 0, -12);
    lv_obj_set_style_radius(preview_card, 16, 0);
    lv_obj_set_style_bg_color(preview_card, lv_color_hex(0x2A2B45), 0);
    lv_obj_set_style_border_width(preview_card, 0, 0);
    lv_obj_set_style_pad_all(preview_card, 10, 0);
    lv_obj_set_style_border_side(preview_card, LV_BORDER_SIDE_NONE, 0);

    ui_preview_label = lv_label_create(preview_card);
    lv_label_set_long_mode(ui_preview_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(ui_preview_label, 138);
    lv_obj_set_style_text_align(ui_preview_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(ui_preview_label, &lv_font_wenkai_16, 0);
    lv_obj_align(ui_preview_label, LV_ALIGN_CENTER, 0, 0);

    page_container[PAGE_WATCH] = page;
}

/* 页面2：消息页 */
static void build_page_msg(lv_obj_t *scr)
{
    lv_obj_t *page = lv_obj_create(scr);
    lv_obj_set_size(page, 172, 320);   /* 全屏（172×320 竖屏），内部坐标与 scr 一致 */
    lv_obj_align(page, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_clear_flag(page, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(page, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(page, 0, 0);
    lv_obj_set_style_pad_all(page, 8, 0);

    lv_obj_t *title = lv_label_create(page);
    lv_label_set_text(title, "\xE6\xB6\x88\xE6\x81\xAF");  /* 消息 */
    lv_obj_set_style_text_font(title, &lv_font_wenkai_16, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0xFFB6C1), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 60);

    ui_msg_list = lv_obj_create(page);
    lv_obj_set_size(ui_msg_list, 156, 220);
    lv_obj_align(ui_msg_list, LV_ALIGN_TOP_MID, 0, 88);
    lv_obj_set_style_bg_opa(ui_msg_list, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(ui_msg_list, 0, 0);
    lv_obj_set_style_pad_all(ui_msg_list, 0, 0);
    lv_obj_clear_flag(ui_msg_list, LV_OBJ_FLAG_SCROLLABLE);

    page_container[PAGE_MSG] = page;
}

/* 页面3：步数/运动页 */
static void build_page_steps(lv_obj_t *scr)
{
    lv_obj_t *page = lv_obj_create(scr);
    lv_obj_set_size(page, 172, 320);   /* 全屏（172×320 竖屏），内部坐标与 scr 一致 */
    lv_obj_align(page, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_clear_flag(page, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(page, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(page, 0, 0);
    lv_obj_set_style_pad_all(page, 0, 0);

    lv_obj_t *title = lv_label_create(page);
    lv_label_set_text(title, "\xE6\xAD\xA5\xE6\x95\xB0");  /* 步数 */
    lv_obj_set_style_text_font(title, &lv_font_wenkai_16, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0xFFB6C1), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 70);

    ui_steps_label = lv_label_create(page);
    lv_label_set_text(ui_steps_label, "0");
    lv_obj_set_style_text_font(ui_steps_label, &lv_font_montserrat_48, 0);
    lv_obj_set_style_text_color(ui_steps_label, lv_color_hex(0x4ADE80), 0);
    lv_obj_align(ui_steps_label, LV_ALIGN_CENTER, 0, -10);

    lv_obj_t *unit = lv_label_create(page);
    lv_label_set_text(unit, "\xE6\xAD\xA5");  /* 步 */
    lv_obj_set_style_text_font(unit, &lv_font_wenkai_16, 0);
    lv_obj_set_style_text_color(unit, lv_color_hex(0x8A8A9A), 0);
    lv_obj_align(unit, LV_ALIGN_CENTER, 0, 50);

    page_container[PAGE_STEPS] = page;
}

/* 页面4：设置/信息页 */
static void build_page_settings(lv_obj_t *scr)
{
    lv_obj_t *page = lv_obj_create(scr);
    lv_obj_set_size(page, 172, 320);   /* 全屏（172×320 竖屏），内部坐标与 scr 一致 */
    lv_obj_align(page, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_clear_flag(page, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(page, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(page, 0, 0);
    lv_obj_set_style_pad_all(page, 0, 0);

    lv_obj_t *title = lv_label_create(page);
    lv_label_set_text(title, "\xE8\xAE\xBE\xE7\xBD\xAE");  /* 设置 */
    lv_obj_set_style_text_font(title, &lv_font_wenkai_16, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0xFFB6C1), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 60);

    /* WiFi 状态 */
    ui_wifi_label = lv_label_create(page);
    lv_label_set_text(ui_wifi_label, "WiFi: --");
    lv_obj_set_style_text_font(ui_wifi_label, &lv_font_wenkai_16, 0);
    lv_obj_set_style_text_color(ui_wifi_label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_align(ui_wifi_label, LV_ALIGN_TOP_LEFT, 14, 96);

    /* MQTT 状态 */
    ui_mqtt_label = lv_label_create(page);
    lv_label_set_text(ui_mqtt_label, "MQTT: --");
    lv_obj_set_style_text_font(ui_mqtt_label, &lv_font_wenkai_16, 0);
    lv_obj_set_style_text_color(ui_mqtt_label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_align(ui_mqtt_label, LV_ALIGN_TOP_LEFT, 14, 126);

    /* 电量详情 */
    ui_bat_label = lv_label_create(page);
    lv_label_set_text(ui_bat_label, "\xE7\x94\xB5\xE6\xB1\xA0  --");  /* 电池  -- */
    lv_obj_set_style_text_font(ui_bat_label, &lv_font_wenkai_16, 0);
    lv_obj_set_style_text_color(ui_bat_label, lv_color_hex(0x4ADE80), 0);
    lv_obj_align(ui_bat_label, LV_ALIGN_TOP_LEFT, 14, 156);

    /* 固件版本 */
    ui_ver_label = lv_label_create(page);
    lv_label_set_text(ui_ver_label, "FW v1.0");
    lv_obj_set_style_text_font(ui_ver_label, &lv_font_wenkai_16, 0);
    lv_obj_set_style_text_color(ui_ver_label, lv_color_hex(0x8A8A9A), 0);
    lv_obj_align(ui_ver_label, LV_ALIGN_TOP_LEFT, 14, 186);

    page_container[PAGE_SETTINGS] = page;
}

void watch_ui_create(void)
{
    lv_obj_t *scr = lv_scr_act();

    /* 背景：柔和深蓝紫 */
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x1A1B2E), 0);

    build_common_bar(scr);
    build_page_watch(scr);
    build_page_msg(scr);
    build_page_steps(scr);
    build_page_settings(scr);

    /* 默认显示主表盘 */
    page_show(PAGE_WATCH);

    preview_refresh();
    msg_list_refresh();
}
