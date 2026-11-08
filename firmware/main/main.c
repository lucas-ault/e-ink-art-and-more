#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "esp_event.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "esp_heap_caps.h"
#include "nvs.h"
#include "nvs_flash.h"

#include "comm.h"
#include "display.h"
#include "pindefine.h"
#include "setup_page.h"

#if __has_include("secrets.h")
#include "secrets.h"
#else
#error "Copy firmware/main/secrets.h.example to firmware/main/secrets.h and set WIFI_SSID and WIFI_PASS"
#endif

static const char *TAG = "demo";

#define WIFI_CONNECTED_BIT BIT0
#define JOIN_TIMEOUT_MS 20000
#define SETUP_SSID "panel-setup"
#define SETUP_PASS "panel-setup"

typedef struct {
    char ssid[33];
    char password[64];
} wifi_creds_t;

static EventGroupHandle_t s_wifi_events;
static char s_ip[16] = "0.0.0.0";
static unsigned char *s_frame;
static volatile bool s_busy;
static volatile bool s_switching;
static volatile bool s_setup_ap;
static volatile bool s_closing_ap;
static const char *s_state = "connecting";
static wifi_creds_t s_current;
static wifi_creds_t s_pending;
static int64_t s_join_started_us;

static int json_string_field(const char *json, const char *key, char *out, size_t out_len)
{
    char pattern[40];
    snprintf(pattern, sizeof(pattern), "\"%s\"", key);
    const char *p = json;
    while ((p = strstr(p, pattern)) != NULL) {
        const char *after = p + strlen(pattern);
        while (*after == ' ' || *after == '\t' || *after == '\n' || *after == '\r') {
            after++;
        }
        if (*after != ':') {
            p = after;
            continue;
        }
        after++;
        while (*after == ' ' || *after == '\t' || *after == '\n' || *after == '\r') {
            after++;
        }
        if (*after != '"') {
            return -1;
        }
        after++;
        size_t n = 0;
        while (*after && *after != '"') {
            char c = *after++;
            if (c == '\\' && *after) {
                char escaped = *after++;
                if (escaped == 'n') {
                    c = '\n';
                } else if (escaped == 'r') {
                    c = '\r';
                } else if (escaped == 't') {
                    c = '\t';
                } else {
                    c = escaped;
                }
            }
            if (n + 1 >= out_len) {
                return -1;
            }
            out[n++] = c;
        }
        if (*after != '"') {
            return -1;
        }
        out[n] = '\0';
        return 1;
    }
    return 0;
}

static void json_escape(const char *text, char *out, size_t out_len)
{
    size_t n = 0;
    for (const unsigned char *p = (const unsigned char *)text; *p && n + 2 < out_len; p++) {
        if (*p < 0x20) {
            continue;
        }
        if (*p == '"' || *p == '\\') {
            out[n++] = '\\';
        }
        out[n++] = (char)*p;
    }
    out[n] = '\0';
}

static void load_creds(wifi_creds_t *out)
{
    memset(out, 0, sizeof(*out));
    nvs_handle_t handle;
    bool found = false;
    if (nvs_open("wifi", NVS_READONLY, &handle) == ESP_OK) {
        size_t len = sizeof(out->ssid);
        if (nvs_get_str(handle, "ssid", out->ssid, &len) == ESP_OK && out->ssid[0] != '\0') {
            len = sizeof(out->password);
            if (nvs_get_str(handle, "pass", out->password, &len) != ESP_OK) {
                out->password[0] = '\0';
            }
            found = true;
        }
        nvs_close(handle);
    }
    if (!found) {
        strncpy(out->ssid, WIFI_SSID, sizeof(out->ssid) - 1);
        strncpy(out->password, WIFI_PASS, sizeof(out->password) - 1);
    }
}

static void save_creds(const wifi_creds_t *creds)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open("wifi", NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs open failed: %s", esp_err_to_name(err));
        return;
    }
    err = nvs_set_str(handle, "ssid", creds->ssid);
    if (err == ESP_OK) {
        err = nvs_set_str(handle, "pass", creds->password);
    }
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs save failed: %s", esp_err_to_name(err));
    }
    nvs_close(handle);
}

static void fill_sta(wifi_config_t *cfg, const wifi_creds_t *creds)
{
    memset(cfg, 0, sizeof(*cfg));
    size_t ssid_len = strlen(creds->ssid);
    size_t pass_len = strlen(creds->password);
    if (ssid_len > sizeof(cfg->sta.ssid)) {
        ssid_len = sizeof(cfg->sta.ssid);
    }
    if (pass_len > sizeof(cfg->sta.password)) {
        pass_len = sizeof(cfg->sta.password);
    }
    memcpy(cfg->sta.ssid, creds->ssid, ssid_len);
    memcpy(cfg->sta.password, creds->password, pass_len);
    cfg->sta.threshold.authmode = pass_len ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;
    cfg->sta.scan_method = WIFI_ALL_CHANNEL_SCAN;
    cfg->sta.sae_pwe_h2e = WPA3_SAE_PWE_BOTH;
    cfg->sta.pmf_cfg.capable = true;
    cfg->sta.pmf_cfg.required = false;
}

static void apply_sta(const wifi_creds_t *creds)
{
    wifi_config_t cfg;
    fill_sta(&cfg, creds);
    xEventGroupClearBits(s_wifi_events, WIFI_CONNECTED_BIT);
    snprintf(s_ip, sizeof(s_ip), "0.0.0.0");
    esp_err_t err = esp_wifi_set_config(WIFI_IF_STA, &cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "wifi config failed: %s", esp_err_to_name(err));
        return;
    }
    esp_wifi_disconnect();
    err = esp_wifi_connect();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "wifi connect: %s", esp_err_to_name(err));
    }
    ESP_LOGI(TAG, "joining wifi ssid %s", creds->ssid);
}

static void stop_setup_ap(void)
{
    if (!s_setup_ap) {
        s_closing_ap = false;
        return;
    }
    s_setup_ap = false;
    esp_err_t err = esp_wifi_set_mode(WIFI_MODE_STA);
    if (err != ESP_OK) {
        s_setup_ap = true;
        ESP_LOGE(TAG, "closing setup network failed: %s", esp_err_to_name(err));
    } else {
        ESP_LOGI(TAG, "setup network closed");
    }
    s_closing_ap = false;
}

static void close_ap_task(void *arg)
{
    (void)arg;
    vTaskDelay(pdMS_TO_TICKS(200));
    stop_setup_ap();
    vTaskDelete(NULL);
}

static void start_setup_ap(void)
{
    if (s_setup_ap) {
        s_state = "setup";
        return;
    }
    wifi_config_t ap = {0};
    strncpy((char *)ap.ap.ssid, SETUP_SSID, sizeof(ap.ap.ssid) - 1);
    strncpy((char *)ap.ap.password, SETUP_PASS, sizeof(ap.ap.password) - 1);
    ap.ap.ssid_len = strlen(SETUP_SSID);
    ap.ap.channel = 1;
    ap.ap.authmode = WIFI_AUTH_WPA2_PSK;
    ap.ap.max_connection = 4;
    ap.ap.beacon_interval = 100;
    ap.ap.dtim_period = 1;
    ap.ap.pmf_cfg.required = false;

    esp_err_t err = esp_wifi_set_mode(WIFI_MODE_APSTA);
    if (err == ESP_OK) {
        err = esp_wifi_set_config(WIFI_IF_AP, &ap);
    }
    if (err != ESP_OK) {
        s_state = "failed";
        ESP_LOGE(TAG, "setup network failed: %s", esp_err_to_name(err));
        return;
    }
    s_setup_ap = true;
    s_state = "setup";
    ESP_LOGI(TAG, "setup network %s password %s  http://192.168.4.1/", SETUP_SSID, SETUP_PASS);
}

static bool wait_for_ip(int timeout_ms)
{
    if (timeout_ms < 0) {
        timeout_ms = 0;
    }
    EventBits_t bits = xEventGroupWaitBits(
        s_wifi_events, WIFI_CONNECTED_BIT, pdFALSE, pdTRUE, pdMS_TO_TICKS(timeout_ms));
    return (bits & WIFI_CONNECTED_BIT) != 0;
}

static void switch_task(void *arg)
{
    (void)arg;
    wifi_creds_t next = s_pending;
    wifi_creds_t previous = s_current;
    vTaskDelay(pdMS_TO_TICKS(400));

    s_state = "connecting";
    save_creds(&next);
    s_current = next;
    apply_sta(&next);
    if (wait_for_ip(JOIN_TIMEOUT_MS)) {
        s_state = "connected";
        stop_setup_ap();
        s_switching = false;
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGW(TAG, "join failed, restoring previous network");
    s_state = "failed";
    bool same = strcmp(previous.ssid, next.ssid) == 0 && strcmp(previous.password, next.password) == 0;
    if (previous.ssid[0] == '\0' || same) {
        s_current = previous;
        s_state = "setup";
        start_setup_ap();
        s_switching = false;
        vTaskDelete(NULL);
        return;
    }

    save_creds(&previous);
    s_current = previous;
    s_state = "connecting";
    apply_sta(&previous);
    if (wait_for_ip(JOIN_TIMEOUT_MS)) {
        s_state = "connected";
        stop_setup_ap();
    } else {
        s_state = "setup";
        start_setup_ap();
    }
    s_switching = false;
    vTaskDelete(NULL);
}

static void wifi_event(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    (void)arg;
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        wifi_event_sta_disconnected_t *disc = event_data;
        ESP_LOGW(TAG, "wifi disconnected, reason %d, retrying", disc->reason);
        xEventGroupClearBits(s_wifi_events, WIFI_CONNECTED_BIT);
        snprintf(s_ip, sizeof(s_ip), "0.0.0.0");
        if (strcmp(s_state, "connected") == 0) {
            s_state = s_setup_ap ? "setup" : "connecting";
        }
        esp_wifi_connect();
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = event_data;
        snprintf(s_ip, sizeof(s_ip), IPSTR, IP2STR(&event->ip_info.ip));
        s_state = "connected";
        ESP_LOGI(TAG, "wifi connected, ip %s", s_ip);
        xEventGroupSetBits(s_wifi_events, WIFI_CONNECTED_BIT);
        if (s_setup_ap && !s_closing_ap) {
            s_closing_ap = true;
            if (xTaskCreate(close_ap_task, "ap_off", 4096, NULL, 4, NULL) != pdPASS) {
                s_closing_ap = false;
            }
        }
    }
}

static void start_wifi(void)
{
    load_creds(&s_current);
    s_wifi_events = xEventGroupCreate();
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();
    esp_netif_create_default_wifi_ap();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event, NULL));

    wifi_config_t wifi_config;
    fill_sta(&wifi_config, &s_current);
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    s_join_started_us = esp_timer_get_time();
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));
    ESP_LOGI(TAG, "joining wifi ssid %s", s_current.ssid);
}

static const char *auth_name(wifi_auth_mode_t mode)
{
    switch (mode) {
    case WIFI_AUTH_OPEN:
        return "OPEN";
    case WIFI_AUTH_WEP:
        return "WEP";
    case WIFI_AUTH_WPA_PSK:
        return "WPA";
    case WIFI_AUTH_WPA2_PSK:
        return "WPA2";
    case WIFI_AUTH_WPA_WPA2_PSK:
        return "WPA/WPA2";
    case WIFI_AUTH_WPA3_PSK:
        return "WPA3";
    case WIFI_AUTH_WPA2_WPA3_PSK:
        return "WPA2/WPA3";
    default:
        return "OTHER";
    }
}

static esp_err_t send_text(httpd_req_t *req, const char *status, const char *type, const char *body)
{
    if (status != NULL) {
        httpd_resp_set_status(req, status);
    }
    httpd_resp_set_type(req, type);
    return httpd_resp_sendstr(req, body);
}

static esp_err_t health_get(httpd_req_t *req)
{
    char body[96];
    snprintf(body, sizeof(body), "{\"ip\":\"%s\",\"busy\":%s}\n", s_ip, s_busy ? "true" : "false");
    return send_text(req, NULL, "application/json", body);
}

static esp_err_t wifi_get(httpd_req_t *req)
{
    char ssid[80];
    char body[192];
    json_escape(s_current.ssid, ssid, sizeof(ssid));
    snprintf(
        body,
        sizeof(body),
        "{\"ssid\":\"%s\",\"ip\":\"%s\",\"switching\":%s,\"state\":\"%s\"}\n",
        ssid,
        s_ip,
        s_switching ? "true" : "false",
        s_state);
    return send_text(req, NULL, "application/json", body);
}

static esp_err_t read_body(httpd_req_t *req, char *buf, size_t cap)
{
    if (req->content_len == 0 || req->content_len >= cap) {
        return ESP_FAIL;
    }
    size_t got = 0;
    while (got < req->content_len) {
        int r = httpd_req_recv(req, buf + got, req->content_len - got);
        if (r == HTTPD_SOCK_ERR_TIMEOUT) {
            continue;
        }
        if (r <= 0) {
            return ESP_FAIL;
        }
        got += (size_t)r;
    }
    buf[got] = '\0';
    return ESP_OK;
}

static esp_err_t wifi_post(httpd_req_t *req)
{
    if (s_switching) {
        return send_text(req, "409 Conflict", "application/json", "{\"error\":\"switch in progress\"}\n");
    }
    char body[512];
    if (read_body(req, body, sizeof(body)) != ESP_OK) {
        return send_text(req, "400 Bad Request", "text/plain", "bad request\n");
    }
    wifi_creds_t next = {0};
    if (json_string_field(body, "ssid", next.ssid, sizeof(next.ssid)) != 1 || next.ssid[0] == '\0') {
        return send_text(req, "400 Bad Request", "text/plain", "ssid required\n");
    }
    int pass = json_string_field(body, "password", next.password, sizeof(next.password));
    if (pass < 0) {
        return send_text(req, "400 Bad Request", "text/plain", "bad password\n");
    }
    s_pending = next;
    s_switching = true;
    s_state = "connecting";
    if (xTaskCreate(switch_task, "wifi_sw", 8192, NULL, 5, NULL) != pdPASS) {
        s_switching = false;
        return send_text(req, "500 Internal Server Error", "text/plain", "busy\n");
    }
    return send_text(req, NULL, "application/json", "{\"ok\":true}\n");
}

static esp_err_t scan_get(httpd_req_t *req)
{
    wifi_scan_config_t scan = {
        .show_hidden = false,
    };
    esp_err_t err = esp_wifi_scan_start(&scan, true);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "scan failed: %s", esp_err_to_name(err));
        return send_text(req, "503 Service Unavailable", "application/json", "{\"error\":\"scan failed\"}\n");
    }

    wifi_ap_record_t *records = calloc(20, sizeof(*records));
    char *json = malloc(4096);
    if (records == NULL || json == NULL) {
        free(records);
        free(json);
        return send_text(req, "500 Internal Server Error", "text/plain", "oom\n");
    }
    uint16_t count = 20;
    err = esp_wifi_scan_get_ap_records(&count, records);
    if (err != ESP_OK) {
        free(records);
        free(json);
        return send_text(req, "503 Service Unavailable", "application/json", "{\"error\":\"scan failed\"}\n");
    }

    size_t used = 0;
    json[0] = '\0';
    strcpy(json, "{\"networks\":[");
    used = strlen(json);
    bool first = true;
    for (int i = 0; i < count; i++) {
        if (records[i].ssid[0] == '\0') {
            continue;
        }
        bool duplicate = false;
        for (int j = 0; j < i; j++) {
            if (strcmp((char *)records[j].ssid, (char *)records[i].ssid) == 0) {
                duplicate = true;
                break;
            }
        }
        if (duplicate) {
            continue;
        }
        char ssid[80];
        json_escape((char *)records[i].ssid, ssid, sizeof(ssid));
        char item[160];
        snprintf(
            item,
            sizeof(item),
            "%s{\"ssid\":\"%s\",\"rssi\":%d,\"auth\":\"%s\"}",
            first ? "" : ",",
            ssid,
            records[i].rssi,
            auth_name(records[i].authmode));
        if (used + strlen(item) + 3 >= 4096) {
            break;
        }
        memcpy(json + used, item, strlen(item));
        used += strlen(item);
        json[used] = '\0';
        first = false;
    }
    memcpy(json + used, "]}\n", 4);
    httpd_resp_set_type(req, "application/json");
    esp_err_t sent = httpd_resp_sendstr(req, json);
    free(records);
    free(json);
    return sent;
}

static esp_err_t setup_get(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    return httpd_resp_send(req, SETUP_PAGE, HTTPD_RESP_USE_STRLEN);
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

static void frame_task(void *arg)
{
    httpd_req_t *req = arg;
    esp_err_t err = recv_exact(req, s_frame, EPD_FRAME_BYTES);
    if (err != ESP_OK) {
        s_busy = false;
        send_text(req, "500 Internal Server Error", "text/plain", "short read\n");
        httpd_req_async_handler_complete(req);
        vTaskDelete(NULL);
        return;
    }
    ESP_LOGI(TAG, "frame received, refresh starting");
    epdDisplayFrame(s_frame);
    s_busy = false;
    ESP_LOGI(TAG, "frame received, refresh finished");
    send_text(req, NULL, "text/plain", "ok\n");
    httpd_req_async_handler_complete(req);
    vTaskDelete(NULL);
}

static void clear_task(void *arg)
{
    httpd_req_t *req = arg;
    ESP_LOGI(TAG, "clear starting");
    epdDisplayColor(WHITE);
    s_busy = false;
    ESP_LOGI(TAG, "clear finished");
    send_text(req, NULL, "text/plain", "ok\n");
    httpd_req_async_handler_complete(req);
    vTaskDelete(NULL);
}

static esp_err_t begin_display_task(httpd_req_t *req, TaskFunction_t task, const char *name)
{
    if (s_busy || s_frame == NULL) {
        return send_text(req, "409 Conflict", "text/plain", "busy\n");
    }
    s_busy = true;
    httpd_req_t *copy = NULL;
    if (httpd_req_async_handler_begin(req, &copy) != ESP_OK) {
        s_busy = false;
        return send_text(req, "500 Internal Server Error", "text/plain", "busy\n");
    }
    if (xTaskCreate(task, name, 10240, copy, 5, NULL) != pdPASS) {
        s_busy = false;
        send_text(copy, "500 Internal Server Error", "text/plain", "busy\n");
        httpd_req_async_handler_complete(copy);
        return ESP_OK;
    }
    return ESP_OK;
}

static esp_err_t frame_post(httpd_req_t *req)
{
    if (req->content_len != EPD_FRAME_BYTES) {
        return send_text(req, "400 Bad Request", "text/plain", "expected 960000 bytes\n");
    }
    return begin_display_task(req, frame_task, "frame");
}

static esp_err_t clear_post(httpd_req_t *req)
{
    return begin_display_task(req, clear_task, "clear");
}

static void start_http(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.stack_size = 10240;
    config.recv_wait_timeout = 30;
    config.send_wait_timeout = 180;
    config.max_uri_handlers = 12;
    config.lru_purge_enable = false;

    httpd_handle_t server = NULL;
    ESP_ERROR_CHECK(httpd_start(&server, &config));

    const httpd_uri_t routes[] = {
        {.uri = "/health", .method = HTTP_GET, .handler = health_get},
        {.uri = "/frame", .method = HTTP_POST, .handler = frame_post},
        {.uri = "/clear", .method = HTTP_POST, .handler = clear_post},
        {.uri = "/wifi", .method = HTTP_GET, .handler = wifi_get},
        {.uri = "/wifi", .method = HTTP_POST, .handler = wifi_post},
        {.uri = "/scan", .method = HTTP_GET, .handler = scan_get},
        {.uri = "/", .method = HTTP_GET, .handler = setup_get},
    };
    for (size_t i = 0; i < sizeof(routes) / sizeof(routes[0]); i++) {
        ESP_ERROR_CHECK(httpd_register_uri_handler(server, &routes[i]));
    }
    ESP_LOGI(TAG, "http ready");
}

static void open_setup_if_needed(void)
{
    if (xEventGroupGetBits(s_wifi_events) & WIFI_CONNECTED_BIT) {
        s_state = "connected";
        return;
    }
    int elapsed_ms = (int)((esp_timer_get_time() - s_join_started_us) / 1000);
    int remain = JOIN_TIMEOUT_MS - elapsed_ms;
    if (remain > 0 && wait_for_ip(remain)) {
        s_state = "connected";
        return;
    }
    if (xEventGroupGetBits(s_wifi_events) & WIFI_CONNECTED_BIT) {
        s_state = "connected";
        return;
    }
    start_setup_ap();
}

void app_main(void)
{
    esp_err_t nvs = nvs_flash_init();
    if (nvs == ESP_ERR_NVS_NO_FREE_PAGES || nvs == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        nvs = nvs_flash_init();
    }
    ESP_ERROR_CHECK(nvs);

    start_wifi();

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
    } else {
        ESP_LOGI(TAG, "frame buffer %d bytes in PSRAM", EPD_FRAME_BYTES);
    }

    start_http();
    open_setup_if_needed();
}
