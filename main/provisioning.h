#pragma once
#include "esp_err.h"

/* ============================================================
 * 配网模块（SoftAP + HTTP 网页）
 *
 * 流程：
 *   1. 开机先尝试读 NVS 里的 WiFi 配置
 *   2. 有配置 → 直接连接
 *   3. 无配置 / 连接失败 → 进入 SoftAP 配网模式
 *      （开热点 Bracelet-Setup，手机连上后浏览器打开 192.168.4.1 填 WiFi）
 *   4. 提交后存 NVS，重启设备生效
 * ============================================================ */

/* 尝试从 NVS 读取已保存的 WiFi 账号密码
 * 返回值：
 *   ESP_OK         —— 读取成功（ssid/pass 已填充）
 *   ESP_ERR_NOT_FOUND —— 没保存过
 *   ESP_FAIL       —— 读取失败
 */
esp_err_t prov_load_wifi(char *ssid, size_t ssid_len,
                         char *pass, size_t pass_len);

/* 把 WiFi 账号密码保存到 NVS（配网提交时调用） */
esp_err_t prov_save_wifi(const char *ssid, const char *pass);

/* 启动 SoftAP 配网模式（开热点 + HTTP 服务器 + 阻塞等待配网完成）
 * 配网成功后返回 ESP_OK（已存 NVS，调用方应重启设备）
 * 此函数会阻塞，直到手机提交了配置或超时
 */
esp_err_t prov_start_ap_mode(void);
