#pragma once
#include "esp_err.h"

/* WiFi 账号密码不再写死，改为配网（SoftAP + 网页）动态配置，存 NVS。
 * 首次开机 / 连不上 WiFi 时，手环会开热点 "Bracelet-Setup"，
 * 手机连上后浏览器打开 http://192.168.4.1 填写即可。 */

/* MQTT 配置 */
#define MQTT_BROKER_URI  "mqtt://broker.emqx.io"   /* 免费公共 broker */
#define MQTT_TOPIC       "bracelet/msg/device1"     /* 本手环订阅的主题 */

/* 启动 WiFi + MQTT（在 main 里调用一次） */
void wifi_mqtt_start(void);
