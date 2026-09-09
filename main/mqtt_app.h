#pragma once
#include "esp_err.h"

/* WiFi 配置 —— 改成你自己的热点 */
#define WIFI_SSID      "esp32"
#define WIFI_PASS      "00000000"

/* MQTT 配置 */
#define MQTT_BROKER_URI  "mqtt://broker.emqx.io"   /* 免费公共 broker */
#define MQTT_TOPIC       "bracelet/msg/device1"     /* 本手环订阅的主题 */

/* 启动 WiFi + MQTT（在 main 里调用一次） */
void wifi_mqtt_start(void);
