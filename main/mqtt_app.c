#include <string.h>
#include <time.h>
#include <sys/time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_sntp.h"
#include "nvs_flash.h"
#include "mqtt_client.h"

#include "mqtt_app.h"
#include "watch_ui.h"
#include "BAT_Driver.h"

static const char *TAG = "MQTT_APP";

static esp_mqtt_client_handle_t mqtt_client = NULL;
static int s_retry_num = 0;
static volatile bool s_time_synced = false;   /* NTP 是否已同步成功 */

/* ---------- WiFi 事件回调 ---------- */
static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        ESP_LOGI(TAG, "WiFi started, connecting...");
        watch_ui_set_status("WiFi connecting...");
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_retry_num < 5) {
            ESP_LOGW(TAG, "WiFi disconnected, retry %d", s_retry_num);
            watch_ui_set_status("WiFi lost, retry...");
            esp_wifi_connect();
            s_retry_num++;
        } else {
            watch_ui_set_status("WiFi failed");
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&event->ip_info.ip));
        s_retry_num = 0;
        watch_ui_set_status("WiFi OK");
    }
}

/* ---------- MQTT 事件回调 ---------- */
static void mqtt_event_handler(void *arg, esp_event_base_t base,
                               int32_t event_id, void *event_data)
{
    esp_mqtt_event_handle_t event = event_data;

    switch ((esp_mqtt_event_id_t)event_id) {
    case MQTT_EVENT_CONNECTED:
        ESP_LOGI(TAG, "MQTT connected, subscribe %s", MQTT_TOPIC);
        watch_ui_set_status("Online");
        esp_mqtt_client_subscribe(mqtt_client, MQTT_TOPIC, 0);
        break;

    case MQTT_EVENT_DATA: {
        char msg[160] = {0};
        int len = event->data_len < (int)sizeof(msg) - 1 ? event->data_len : (int)sizeof(msg) - 1;
        memcpy(msg, event->data, len);
        msg[len] = '\0';
        ESP_LOGI(TAG, "topic=%s msg=%s", event->topic, msg);

        /* 压入历史消息列表（内部已加锁） */
        watch_ui_push_message(msg);
        break;
    }

    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGI(TAG, "MQTT disconnected");
        watch_ui_set_status("Offline");
        break;

    case MQTT_EVENT_ERROR:
        ESP_LOGE(TAG, "MQTT error");
        break;

    default:
        break;
    }
}

/* ---------- 连接 WiFi ---------- */
static void wifi_connect(void)
{
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                                         &wifi_event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                                         &wifi_event_handler, NULL, NULL));

    wifi_config_t wifi_config = { 0 };
    strcpy((char *)wifi_config.sta.ssid, WIFI_SSID);
    strcpy((char *)wifi_config.sta.password, WIFI_PASS);
    wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
    esp_wifi_connect();
}

/* ---------- NTP 时间同步 ---------- */

/* SNTP 同步完成的回调：把 SNTP 时间写入系统时钟 */
static void time_sync_notification_cb(struct timeval *tv)
{
    ESP_LOGI(TAG, "NTP time synced, epoch=%lld", (long long)tv->tv_sec);

    /* 关键：主动把 SNTP 时间写进系统时钟，否则 time() 不更新 */
    settimeofday(tv, NULL);
    s_time_synced = true;
}

static void sntp_init_and_sync(void)
{
    ESP_LOGI(TAG, "Initializing SNTP");

    /* 设置时区为北京时间（UTC+8），标准 POSIX TZ 写法 */
    setenv("TZ", "UTC-8", 1);
    tzset();

    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "ntp.aliyun.com");
    esp_sntp_set_time_sync_notification_cb(time_sync_notification_cb);
    esp_sntp_init();
}

/* ---------- 启动 MQTT ---------- */
static void mqtt_start(void)
{
    esp_mqtt_client_config_t cfg = {
        .broker.address.uri = MQTT_BROKER_URI,
    };
    mqtt_client = esp_mqtt_client_init(&cfg);
    esp_mqtt_client_register_event(mqtt_client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    esp_mqtt_client_start(mqtt_client);
}

/* ---------- 周期任务：读电量 + 刷时间 ---------- */
static void sensor_refresh_task(void *arg)
{
    while (1) {
        /* 电量 */
        float volts = BAT_Get_Volts();
        watch_ui_set_battery(volts);

        /* 时间（NTP 同步成功后） */
        if (s_time_synced) {
            time_t now = 0;
            struct tm timeinfo = {0};
            time(&now);
            localtime_r(&now, &timeinfo);

            char buf[16];
            snprintf(buf, sizeof(buf), "%02d:%02d", timeinfo.tm_hour, timeinfo.tm_min);
            watch_ui_set_time(buf);
        }

        vTaskDelay(pdMS_TO_TICKS(1000));  /* 1 秒刷新一次 */
    }
}

/* ---------- 对外入口：WiFi + MQTT + NTP + 传感器任务 ---------- */
static void wifi_mqtt_task(void *arg)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    wifi_connect();

    /* 等 WiFi 拿到 IP（最多 20 秒） */
    for (int i = 0; i < 200; i++) {
        esp_netif_t *sta = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
        if (sta != NULL) {
            esp_netif_ip_info_t ip;
            esp_netif_get_ip_info(sta, &ip);
            if (ip.ip.addr != 0) break;
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    /* 启动 NTP 时间同步 */
    sntp_init_and_sync();

    /* 启动 MQTT */
    mqtt_start();

    /* 启动传感器/时间刷新任务 */
    xTaskCreatePinnedToCore(sensor_refresh_task, "sensor_refresh", 4096,
                            NULL, 2, NULL, 0);

    vTaskDelete(NULL);
}

void wifi_mqtt_start(void)
{
    /* 先初始化电池采集 */
    BAT_Init();

    xTaskCreatePinnedToCore(wifi_mqtt_task, "wifi_mqtt_task", 8192,
                            NULL, 3, NULL, 0);
}
