#include <string.h>
#include <stdbool.h>
#include <time.h>
#include <sys/time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_sntp.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "mqtt_client.h"

#include "mqtt_app.h"
#include "watch_ui.h"
#include "BAT_Driver.h"
#include "provisioning.h"
#include "QMI8658.h"
#include "power_mgmt.h"

/* 由 main.c 提供：收到新消息时 RGB 灯闪烁提醒 */
extern void rgb_notify(void);

static const char *TAG = "MQTT_APP";

static esp_mqtt_client_handle_t mqtt_client = NULL;
static int s_retry_num = 0;
static volatile bool s_time_synced = false;   /* NTP 是否已同步成功 */
static volatile bool s_provisioning = false;  /* 配网模式中（抑制 STA 重连/状态更新） */

/* ---------- WiFi 事件回调 ---------- */
static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        /* 配网模式下不主动连接（避免 AP 模式里反复触发 STA 重连） */
        if (s_provisioning) return;
        ESP_LOGI(TAG, "WiFi started, connecting...");
        watch_ui_set_status("WiFi connecting...");
        watch_ui_set_wifi_status("WiFi: connecting");
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_provisioning) return;   /* 配网模式下忽略断连，不重连、不改状态 */
        if (s_retry_num < 5) {
            ESP_LOGW(TAG, "WiFi disconnected, retry %d", s_retry_num);
            watch_ui_set_status("WiFi lost, retry...");
            watch_ui_set_wifi_status("WiFi: retry");
            esp_wifi_connect();
            s_retry_num++;
        } else {
            watch_ui_set_status("WiFi failed");
            watch_ui_set_wifi_status("WiFi: failed");
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&event->ip_info.ip));
        s_retry_num = 0;
        watch_ui_set_status("WiFi OK");
        watch_ui_set_wifi_status("WiFi: OK");
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
        watch_ui_set_mqtt_status("MQTT: Online");
        esp_mqtt_client_subscribe(mqtt_client, MQTT_TOPIC, 0);
        break;

    case MQTT_EVENT_DATA: {
        char msg[160] = {0};
        int len = event->data_len < (int)sizeof(msg) - 1 ? event->data_len : (int)sizeof(msg) - 1;
        memcpy(msg, event->data, len);
        msg[len] = '\0';
        ESP_LOGI(TAG, "topic=%s msg=%s", event->topic, msg);

        /* 压入历史消息列表（内部已加锁）+ 收到新消息亮屏 + 切回表盘页显示预览 + RGB 灯闪烁提醒 */
        watch_ui_push_message(msg);
        watch_ui_goto_watch();
        backlight_wake();
        rgb_notify();
        break;
    }

    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGI(TAG, "MQTT disconnected");
        watch_ui_set_status("Offline");
        watch_ui_set_mqtt_status("MQTT: Offline");
        break;

    case MQTT_EVENT_ERROR:
        ESP_LOGE(TAG, "MQTT error");
        break;

    default:
        break;
    }
}

/* ---------- 连接 WiFi（用传入的账号密码） ---------- */
/* 底层初始化（netif/event loop）已在主任务开头做过一次，这里只做 STA 专属初始化 */
static bool s_wifi_base_inited = false;
static bool s_wifi_inited = false;   /* esp_wifi_init 是否已调用 */

static void wifi_stop_clean(void)
{
    if (s_wifi_inited) {
        esp_wifi_stop();
        esp_wifi_deinit();
        s_wifi_inited = false;
    }
}

static void wifi_connect(const char *ssid, const char *pass)
{
    if (!s_wifi_base_inited) {
        esp_netif_create_default_wifi_sta();

        ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                                             &wifi_event_handler, NULL, NULL));
        ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                                             &wifi_event_handler, NULL, NULL));
        s_wifi_base_inited = true;
    }

    /* 确保干净状态再 init */
    wifi_stop_clean();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    s_wifi_inited = true;

    wifi_config_t wifi_config = { 0 };
    strcpy((char *)wifi_config.sta.ssid, ssid);
    strcpy((char *)wifi_config.sta.password, pass);
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

/* ---------- 周期任务：读电量 + 刷时间 + 计步 ---------- */
static void sensor_refresh_task(void *arg)
{
    /* 计步：基于加速度幅值阈值的简单峰值检测 */
    float acc_mag_prev = 0.0f;
    bool  was_above = false;
    uint32_t steps = 0;

    while (1) {
        /* 电量 */
        float volts = BAT_Get_Volts();
        watch_ui_set_battery(volts);

        /* 计步：读加速度，检测一步（幅值过阈值后回落的峰值） */
        getAccelerometer();
        float ax = Accel.x, ay = Accel.y, az = Accel.z;
        float mag = ax * ax + ay * ay + az * az;  /* 用平方，避免 sqrt 开销 */

        /* 阈值：走路时加速度幅值会明显偏离重力(≈1g)，这里取相对变化 */
        const float step_thresh = 1.6f;  /* (g^2) 相当于约 1.26g 的幅值 */

        if (!was_above && mag > step_thresh) {
            was_above = true;
        } else if (was_above && mag < step_thresh) {
            /* 幅值越过峰值回落 → 记一步 */
            was_above = false;
            steps++;
            watch_ui_set_steps(steps);
        }

        (void)acc_mag_prev;

        /* 息屏管理：抬腕检测 + 无操作超时自动息屏（复用上面已读的 Accel） */
        power_mgmt_poll();

        /* 时间（NTP 同步成功后） */
        if (s_time_synced) {
            time_t now = 0;
            struct tm timeinfo = {0};
            time(&now);
            localtime_r(&now, &timeinfo);

            char buf[16];
            snprintf(buf, sizeof(buf), "%02d:%02d", timeinfo.tm_hour, timeinfo.tm_min);
            watch_ui_set_time(buf);

            /* 日期 + 星期（中文用 UTF-8 转义字节，规避 Windows 下 GCC 源文件编码问题） */
            static const char *week_cn[] = {
                "\xE6\x98\x9F\xE6\x9C\x9F\xE6\x97\xA5",   /* 星期日 */
                "\xE6\x98\x9F\xE6\x9C\x9F\xE4\xB8\x80",   /* 星期一 */
                "\xE6\x98\x9F\xE6\x9C\x9F\xE4\xBA\x8C",   /* 星期二 */
                "\xE6\x98\x9F\xE6\x9C\x9F\xE4\xB8\x89",   /* 星期三 */
                "\xE6\x98\x9F\xE6\x9C\x9F\xE5\x9B\x9B",   /* 星期四 */
                "\xE6\x98\x9F\xE6\x9C\x9F\xE4\xBA\x94",   /* 星期五 */
                "\xE6\x98\x9F\xE6\x9C\x9F\xE5\x85\xAD"    /* 星期六 */
            };
            char datebuf[32];
            snprintf(datebuf, sizeof(datebuf), "%d\xE6\x9C\x88%d\xE6\x97\xA5",
                     timeinfo.tm_mon + 1, timeinfo.tm_mday);   /* "%d月%d日" */
            watch_ui_set_date(datebuf, week_cn[timeinfo.tm_wday]);
        }

        vTaskDelay(pdMS_TO_TICKS(50));  /* 50ms 刷一次，计步更灵敏 */
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

    /* 统一做一次底层初始化（netif + 事件循环），不管走连接还是配网路径都依赖它。
     * 这里不在 wifi_connect / prov_start_ap_mode 里重复 init，避免非幂等函数重复调用报错。 */
    esp_netif_init();
    esp_event_loop_create_default();

    /* ===== 1. 尝试读 NVS 里保存的 WiFi 配置 ===== */
    char ssid[33] = {0};
    char pass[65] = {0};
    esp_err_t load_ret = prov_load_wifi(ssid, sizeof(ssid), pass, sizeof(pass));

    bool got_ip = false;

    if (load_ret == ESP_OK) {
        /* 有配置，直接连接 */
        ESP_LOGI(TAG, "从 NVS 读取到 WiFi: %s", ssid);
        wifi_connect(ssid, pass);

        /* 等 WiFi 拿到 IP（最多 15 秒） */
        for (int i = 0; i < 150; i++) {
            esp_netif_t *sta = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
            if (sta != NULL) {
                esp_netif_ip_info_t ip;
                esp_netif_get_ip_info(sta, &ip);
                if (ip.ip.addr != 0) { got_ip = true; break; }
            }
            vTaskDelay(pdMS_TO_TICKS(100));
        }
    }

    /* ===== 2. 连接失败 / 无配置 → 进配网 ===== */
    if (!got_ip) {
        if (load_ret == ESP_OK) {
            ESP_LOGW(TAG, "连接失败，进入配网模式");
        } else {
            ESP_LOGI(TAG, "首次使用，进入配网模式");
        }

        /* 清掉当前 wifi 状态，让配网函数干净地 init */
        wifi_stop_clean();

        /* 进入配网：抑制 STA 断连重连逻辑，避免 AP 模式里反复触发重连导致刷屏闪烁 */
        s_provisioning = true;
        esp_err_t prov_ret = prov_start_ap_mode();
        s_provisioning = false;

        if (prov_ret == ESP_OK) {
            esp_restart();  /* 配网成功，重启用新配置连接 */
        }
        /* 配网超时：往下走，若有旧配置再兜底连一次 */

        if (load_ret == ESP_OK) {
            /* 兜底：再用旧配置连一次（可能配网时用户没操作，旧 WiFi 其实恢复了） */
            ESP_LOGW(TAG, "配网超时，重试旧配置");
            wifi_connect(ssid, pass);
            for (int i = 0; i < 150; i++) {
                esp_netif_t *sta = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
                if (sta != NULL) {
                    esp_netif_ip_info_t ip;
                    esp_netif_get_ip_info(sta, &ip);
                    if (ip.ip.addr != 0) { got_ip = true; break; }
                }
                vTaskDelay(pdMS_TO_TICKS(100));
            }
        }
    }

    /* ===== 3. 启动 NTP + MQTT + 传感器任务（即使没连上 WiFi 也启动，等重连） ===== */
    sntp_init_and_sync();
    mqtt_start();

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
