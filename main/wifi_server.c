/*
 * WiFi and HTTP server - separate file to avoid include conflicts with spi_master_lobo.h
 */
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "freertos/event_groups.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_http_server.h"
#include "lwip/apps/sntp.h"
#include <string.h>

#include "wifi_server.h"
#include <stdio.h>

static const char *TAG = "wifi_server";

static EventGroupHandle_t wifi_event_group;
static esp_netif_t *sta_netif = NULL;
const int WIFI_CONNECTED_BIT = 0x00000001;

static void event_handler(void *ctx, esp_event_base_t event_base,
                         int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        esp_wifi_connect();
        xEventGroupClearBits(wifi_event_group, WIFI_CONNECTED_BIT);
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        xEventGroupSetBits(wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

void wifi_server_init(const char *ssid, const char *password)
{
    wifi_event_group = xEventGroupCreate();
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    sta_netif = esp_netif_create_default_wifi_sta();

    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, NULL));

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));

    wifi_config_t wifi_config = {0};
    strncpy((char *)wifi_config.sta.ssid, ssid, sizeof(wifi_config.sta.ssid) - 1);
    strncpy((char *)wifi_config.sta.password, password, sizeof(wifi_config.sta.password) - 1);

    ESP_LOGI(TAG, "Connecting to SSID: %s", ssid);
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
}

void wifi_server_wait_connected(void)
{
    xEventGroupWaitBits(wifi_event_group, WIFI_CONNECTED_BIT, false, true, portMAX_DELAY);
}

int wifi_server_wait_connected_timeout(uint32_t ms)
{
    TickType_t ticks = (ms / portTICK_RATE_MS) > 0 ? (ms / portTICK_RATE_MS) : 1;
    return (xEventGroupWaitBits(wifi_event_group, WIFI_CONNECTED_BIT, false, true, ticks) & WIFI_CONNECTED_BIT) ? 1 : 0;
}

void wifi_server_reconnect(void)
{
    esp_wifi_connect();
}

void wifi_server_print_ip(void)
{
    char buf[16];
    if (wifi_server_get_ip(buf, sizeof(buf))) {
        printf("WiFi connected. IP: %s\r\n", buf);
    } else {
        printf("WiFi connected but IP not available\r\n");
    }
}

int wifi_server_get_ip(char *buf, size_t len)
{
    esp_netif_ip_info_t ip_info = {0};
    if (!buf || len < 8) return 0;
    if (!sta_netif || esp_netif_get_ip_info(sta_netif, &ip_info) != ESP_OK) return 0;
    int n = snprintf(buf, len, "%d.%d.%d.%d",
                     (int)(ip_info.ip.addr & 0xff),
                     (int)((ip_info.ip.addr >> 8) & 0xff),
                     (int)((ip_info.ip.addr >> 16) & 0xff),
                     (int)((ip_info.ip.addr >> 24) & 0xff));
    return (n > 0 && (size_t)n < len) ? 1 : 0;
}

static esp_err_t root_get_handler(httpd_req_t *req)
{
    esp_netif_ip_info_t ip_info = {0};
    if (sta_netif) {
        esp_netif_get_ip_info(sta_netif, &ip_info);
    }
    char ip_str[16];
    snprintf(ip_str, sizeof(ip_str), "%d.%d.%d.%d",
             (int)((ip_info.ip.addr) & 0xff),
             (int)((ip_info.ip.addr >> 8) & 0xff),
             (int)((ip_info.ip.addr >> 16) & 0xff),
             (int)((ip_info.ip.addr >> 24) & 0xff));

    const char *html = "<!DOCTYPE html><html><head><meta charset='UTF-8'><title>ESP32 ePaper</title></head>"
        "<body style='font-family:sans-serif; margin:2em;'>"
        "<h1>ESP32 ePaper Display</h1>"
        "<p>Connected to WiFi. IP: %s</p>"
        "<p><a href='/hello'>Say hello</a></p>"
        "</body></html>";

    char resp[512];
    snprintf(resp, sizeof(resp), html, ip_str);
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, resp, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

static const httpd_uri_t root_uri = {
    .uri       = "/",
    .method    = HTTP_GET,
    .handler   = root_get_handler,
    .user_ctx  = NULL
};

static esp_err_t hello_get_handler(httpd_req_t *req)
{
    const char *resp = "Hello from ESP32 ePaper!";
    httpd_resp_send(req, resp, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

static const httpd_uri_t hello_uri = {
    .uri       = "/hello",
    .method    = HTTP_GET,
    .handler   = hello_get_handler,
    .user_ctx  = NULL
};

void wifi_server_start_http(void)
{
    httpd_handle_t server = NULL;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    if (httpd_start(&server, &config) == ESP_OK) {
        httpd_register_uri_handler(server, &root_uri);
        httpd_register_uri_handler(server, &hello_uri);
        ESP_LOGI(TAG, "Web server started on port %d", config.server_port);
    } else {
        ESP_LOGE(TAG, "Failed to start web server");
    }
}

void wifi_server_init_sntp(void)
{
    sntp_setoperatingmode(SNTP_OPMODE_POLL);
    sntp_setservername(0, "pool.ntp.org");
    sntp_init();
}
