#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_http_server.h"

#include "provisioning.h"
#include "watch_ui.h"

static const char *TAG = "PROV";

#define PROV_NAMESPACE   "wifi_cfg"      /* NVS 命名空间 */
#define PROV_SSID_KEY    "ssid"          /* SSID 键 */
#define PROV_PASS_KEY    "pass"          /* 密码键 */

#define AP_SSID          "Bracelet-Setup"  /* 配网热点名 */
#define AP_PASS          "12345678"        /* 热点密码（至少 8 位） */

/* 配网成功后置 true，让阻塞函数退出 */
static volatile bool s_config_done = false;

/* ============ NVS 读写 ============ */

esp_err_t prov_load_wifi(char *ssid, size_t ssid_len,
                         char *pass, size_t pass_len)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(PROV_NAMESPACE, NVS_READONLY, &handle);
    if (err != ESP_OK) {
        return err;
    }

    err = nvs_get_str(handle, PROV_SSID_KEY, ssid, &ssid_len);
    if (err != ESP_OK) {
        nvs_close(handle);
        return err;
    }

    err = nvs_get_str(handle, PROV_PASS_KEY, pass, &pass_len);
    nvs_close(handle);
    return err;
}

esp_err_t prov_save_wifi(const char *ssid, const char *pass)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(PROV_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        return err;
    }

    err = nvs_set_str(handle, PROV_SSID_KEY, ssid);
    if (err == ESP_OK) {
        err = nvs_set_str(handle, PROV_PASS_KEY, pass);
    }
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);
    return err;
}

/* ============ HTTP 配网网页 ============ */

/* 返回给手机的配网网页（HTML，纯英文避免源文件编码问题） */
static const char *PAGE_HTML =
    "<!DOCTYPE html><html><head><meta charset='utf-8'>"
    "<meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<title>Bracelet Setup</title>"
    "<style>body{font-family:sans-serif;background:#1a1b2e;color:#fff;"
    "padding:20px;text-align:center}h2{color:#ffb6c1}"
    "input{width:90%;padding:12px;margin:8px 0;border:none;border-radius:10px;"
    "background:#2a2b45;color:#fff;font-size:16px}"
    "button{width:95%;padding:14px;margin-top:12px;border:none;border-radius:10px;"
    "background:#ff7a9c;color:#fff;font-size:18px;font-weight:bold}</style></head>"
    "<body><h2>Bracelet Setup</h2>"
    "<p>Enter your WiFi info</p>"
    "<form action='/save' method='post'>"
    "<input name='ssid' placeholder='WiFi Name' required>"
    "<input name='pass' type='password' placeholder='WiFi Password' required>"
    "<button type='submit'>Save & Connect</button>"
    "</form></body></html>";

static const char *PAGE_OK =
    "<!DOCTYPE html><html><head><meta charset='utf-8'>"
    "<meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<title>Done</title></head>"
    "<body style='background:#1a1b2e;color:#fff;font-family:sans-serif;"
    "text-align:center;padding:40px'>"
    "<h2 style='color:#4ade80'>Success!</h2>"
    "<p>Bracelet is rebooting to connect WiFi...</p>"
    "<p>You can close this page now</p></body></html>";

/* favicon：返回 204 空响应，避免浏览器自动请求 favicon 时的 404 日志噪音 */
static esp_err_t favicon_handler(httpd_req_t *req)
{
    httpd_resp_set_status(req, "204 No Content");
    return httpd_resp_send(req, NULL, 0);
}

/* 首页：显示配网表单 */
static esp_err_t root_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    return httpd_resp_send(req, PAGE_HTML, strlen(PAGE_HTML));
}

/* 保存：处理 POST body (application/x-www-form-urlencoded) 或 GET query，格式 ssid=xxx&pass=yyy */
static esp_err_t save_handler(httpd_req_t *req)
{
    char ssid[33] = {0};
    char pass[65] = {0};

    char buf[256];
    bool got = false;

    /* 优先读 POST body */
    if (req->method == HTTP_POST) {
        int remaining = req->content_len;
        int total = 0;
        while (remaining > 0) {
            int read = httpd_req_recv(req, buf + total, remaining);
            if (read <= 0) break;
            remaining -= read;
            total += read;
            if (total >= (int)sizeof(buf) - 1) break;
        }
        buf[total] = '\0';
        got = (total > 0);
    } else {
        /* GET：从 URL query 串解析 */
        if (httpd_req_get_url_query_str(req, buf, sizeof(buf)) == ESP_OK) {
            got = true;
        }
    }

    if (got) {
        char val[128];
        if (httpd_query_key_value(buf, "ssid", val, sizeof(val)) == ESP_OK) {
            strlcpy(ssid, val, sizeof(ssid));
        }
        if (httpd_query_key_value(buf, "pass", val, sizeof(val)) == ESP_OK) {
            strlcpy(pass, val, sizeof(pass));
        }
    }

    if (ssid[0] == '\0' || pass[0] == '\0') {
        httpd_resp_set_type(req, "text/html");
        return httpd_resp_send(req, "SSID or pass empty", strlen("SSID or pass empty"));
    }

    ESP_LOGI(TAG, "收到配网: ssid=%s pass=%s", ssid, pass);

    esp_err_t err = prov_save_wifi(ssid, pass);
    if (err == ESP_OK) {
        httpd_resp_set_type(req, "text/html");
        httpd_resp_send(req, PAGE_OK, strlen(PAGE_OK));
        s_config_done = true;
        return ESP_OK;
    } else {
        httpd_resp_set_type(req, "text/html");
        return httpd_resp_send(req, "Save failed", strlen("Save failed"));
    }
}

/* ============ SoftAP 配网模式 ============ */

esp_err_t prov_start_ap_mode(void)
{
    ESP_LOGI(TAG, "进入 SoftAP 配网模式");

    s_config_done = false;
    watch_ui_set_status("Setup mode");
    /* 屏幕底部提示（中文用 UTF-8 转义），分两行精简显示：
       连热点 Bracelet-Setup / 开 192.168.4.1 */
    watch_ui_show_tip("\xE8\xBF\x9E\xE7\x83\xAD\xE7\x82\xB9 Bracelet-Setup\n"
                      "\xE6\x89\x93\xE5\xBC\x80 192.168.4.1");

    /* 前提：调用方已做过 esp_netif_init + esp_event_loop_create_default，
     * 且 wifi 处于 deinit 状态（调用方负责）。这里重新 init 并切到 AP 模式。 */

    /* 创建 AP netif（幂等保护） */
    if (esp_netif_get_handle_from_ifkey("WIFI_AP_DEF") == NULL) {
        esp_netif_create_default_wifi_ap();
    }

    /* 重新初始化 wifi（调用方已 deinit） */
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    /* 配置 AP */
    wifi_config_t ap_cfg = {0};
    strcpy((char *)ap_cfg.ap.ssid, AP_SSID);
    strcpy((char *)ap_cfg.ap.password, AP_PASS);
    ap_cfg.ap.ssid_len = strlen(AP_SSID);
    ap_cfg.ap.max_connection = 4;
    ap_cfg.ap.authmode = WIFI_AUTH_WPA2_PSK;
    ap_cfg.ap.channel = 6;

    esp_wifi_set_mode(WIFI_MODE_AP);
    esp_wifi_set_config(WIFI_IF_AP, &ap_cfg);
    esp_wifi_start();

    /* 启动 HTTP 服务器。
     * 关键：手机浏览器提交表单时请求头很长，默认缓冲区会触发
     * "request URI/header too long" (431)。加大 URI 长度和 URI handler 数。 */
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 80;
    config.max_uri_handlers = 16;       /* 默认 8，加大 */
    config.uri_match_fn = httpd_uri_match_wildcard;  /* 支持通配，避免 favicon 等 404 */
    config.max_open_sockets = 4;        /* 默认 7，够用即可 */
    config.recv_wait_timeout = 10;      /* 秒 */
    config.send_wait_timeout = 10;
    httpd_handle_t server = NULL;
    if (httpd_start(&server, &config) != ESP_OK) {
        ESP_LOGE(TAG, "HTTP 服务器启动失败");
        return ESP_FAIL;
    }

    httpd_uri_t root = {
        .uri = "/",
        .method = HTTP_GET,
        .handler = root_handler,
    };
    httpd_uri_t save = {
        .uri = "/save",
        .method = HTTP_POST,
        .handler = save_handler,
    };
    httpd_uri_t save_get = {
        .uri = "/save",
        .method = HTTP_GET,
        .handler = save_handler,
    };
    /* favicon 返回 204 空响应，消除浏览器自动请求的 404 噪音 */
    httpd_uri_t favicon = {
        .uri = "/favicon.ico",
        .method = HTTP_GET,
        .handler = favicon_handler,
    };
    httpd_register_uri_handler(server, &root);
    httpd_register_uri_handler(server, &save);
    httpd_register_uri_handler(server, &save_get);
    httpd_register_uri_handler(server, &favicon);

    ESP_LOGI(TAG, "配网热点已开启: %s / 密码 %s", AP_SSID, AP_PASS);
    ESP_LOGI(TAG, "手机连上热点后浏览器访问 http://192.168.4.1");

    /* 阻塞等待配网完成（最多 5 分钟） */
    int waited = 0;
    while (!s_config_done && waited < 300) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        waited++;
    }

    /* 停止服务器和热点，并 deinit，让调用方可以干净地重新 init STA */
    httpd_stop(server);
    esp_wifi_stop();
    esp_wifi_deinit();

    watch_ui_show_tip(NULL);   /* 恢复消息预览 */

    if (s_config_done) {
        ESP_LOGI(TAG, "配网成功，重启生效");
        return ESP_OK;
    }
    ESP_LOGW(TAG, "配网超时");
    return ESP_ERR_TIMEOUT;
}
