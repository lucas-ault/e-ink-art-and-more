#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "esp_event.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "esp_heap_caps.h"
#include "nvs_flash.h"

#include "comm.h"
#include "display.h"
#include "pindefine.h"

#if __has_include("secrets.h")
#include "secrets.h"
#else
#error "Copy firmware/main/secrets.h.example to firmware/main/secrets.h and set WIFI_SSID and WIFI_PASS"
#endif

static const char *TAG = "demo";

#define WIFI_CONNECTED_BIT BIT0

static EventGroupHandle_t s_wifi_events;
static char s_ip[16] = "0.0.0.0";
static unsigned char *s_frame;
static volatile bool s_busy;

static void wifi_event(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        wifi_event_sta_disconnected_t *disc = event_data;
        ESP_LOGW(TAG, "wifi disconnected, reason %d, retrying", disc->reason);
        xEventGroupClearBits(s_wifi_events, WIFI_CONNECTED_BIT);
        esp_wifi_connect();
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = event_data;
        snprintf(s_ip, sizeof(s_ip), IPSTR, IP2STR(&event->ip_info.ip));
        ESP_LOGI(TAG, "wifi connected, ip %s", s_ip);
        xEventGroupSetBits(s_wifi_events, WIFI_CONNECTED_BIT);
    }
}

static void start_wifi(void)
{
    s_wifi_events = xEventGroupCreate();
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event, NULL));

    wifi_config_t wifi_config = {0};
    strncpy((char *)wifi_config.sta.ssid, WIFI_SSID, sizeof(wifi_config.sta.ssid) - 1);
    strncpy((char *)wifi_config.sta.password, WIFI_PASS, sizeof(wifi_config.sta.password) - 1);
    wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    wifi_config.sta.sae_pwe_h2e = WPA3_SAE_PWE_BOTH;
    wifi_config.sta.pmf_cfg.capable = true;
    wifi_config.sta.pmf_cfg.required = false;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));
    ESP_LOGI(TAG, "joining wifi ssid %s", WIFI_SSID);
}

static esp_err_t health_get(httpd_req_t *req)
{
    char body[96];
    snprintf(body, sizeof(body), "{\"ip\":\"%s\",\"busy\":%s}\n", s_ip, s_busy ? "true" : "false");
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, body);
}

static esp_err_t recv_exact(httpd_req_t *req, unsigned char *dest, int total)
{
    int received = 0;
    while (received < total) {
        int r = httpd_req_recv(req, (char *)dest + received, total - received);
        if (r == HTTPD_SOCK_ERR_TIMEOUT) {
            continue;
        }
        if (r <= 0) {
            return ESP_FAIL;
        }
        received += r;
        if (received % (100 * 1024) < r) {
            ESP_LOGI(TAG, "frame %d / %d bytes", received, total);
        }
    }
    return ESP_OK;
}

static esp_err_t frame_post(httpd_req_t *req)
{
    if (req->content_len != EPD_FRAME_BYTES) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "expected 960000 bytes");
        return ESP_FAIL;
    }
    if (s_busy || s_frame == NULL) {
        httpd_resp_set_status(req, "409 Conflict");
        httpd_resp_set_type(req, "text/plain");
        httpd_resp_sendstr(req, "busy\n");
        return ESP_OK;
    }

    s_busy = true;
    ESP_LOGI(TAG, "frame received, reading %d bytes", req->content_len);
    esp_err_t err = recv_exact(req, s_frame, EPD_FRAME_BYTES);
    if (err != ESP_OK) {
        s_busy = false;
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "short read");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "frame received, refresh starting");
    epdDisplayFrame(s_frame);
    s_busy = false;
    ESP_LOGI(TAG, "frame received, refresh finished");
    httpd_resp_set_type(req, "text/plain");
    return httpd_resp_sendstr(req, "ok\n");
}

static void start_http(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.stack_size = 10240;
    config.recv_wait_timeout = 30;
    config.send_wait_timeout = 120;
    config.lru_purge_enable = true;

    httpd_handle_t server = NULL;
    ESP_ERROR_CHECK(httpd_start(&server, &config));

    httpd_uri_t health = {
        .uri = "/health",
        .method = HTTP_GET,
        .handler = health_get,
    };
    httpd_uri_t frame = {
        .uri = "/frame",
        .method = HTTP_POST,
        .handler = frame_post,
    };
    ESP_ERROR_CHECK(httpd_register_uri_handler(server, &health));
    ESP_ERROR_CHECK(httpd_register_uri_handler(server, &frame));
    ESP_LOGI(TAG, "listening on http://%s/  POST /frame  GET /health", s_ip);
}

void app_main(void)
{
    esp_err_t nvs = nvs_flash_init();
    if (nvs == ESP_ERR_NVS_NO_FREE_PAGES || nvs == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        nvs = nvs_flash_init();
    }
    ESP_ERROR_CHECK(nvs);

    initialGpio();
    ESP_ERROR_CHECK(initialSpi());
    setGpioLevel(LOAD_SW, GPIO_HIGH);
    epdHardwareReset();
    setPinCsAll(GPIO_HIGH);

    initEPD();
    ESP_LOGI(TAG, "white refresh starting");
    epdDisplayColor(WHITE);
    ESP_LOGI(TAG, "white refresh finished");

    s_frame = heap_caps_malloc(EPD_FRAME_BYTES, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (s_frame == NULL) {
        ESP_LOGE(TAG, "PSRAM frame buffer allocation failed");
        return;
    }
    ESP_LOGI(TAG, "frame buffer %d bytes in PSRAM", EPD_FRAME_BYTES);

    start_wifi();
    xEventGroupWaitBits(s_wifi_events, WIFI_CONNECTED_BIT, pdFALSE, pdTRUE, portMAX_DELAY);
    start_http();
}
