#include "tcp_client.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "iic.h"
#include "lwip/inet.h"
#include "lwip/sockets.h"
#include "wifi_scan.h"

#define TCP_SERVER_IP "192.168.1.22"
#define TCP_SERVER_PORT 5000
#define TCP_MESSAGE "hello from ESP32\n"
#define TCP_RESPONSE_TIMEOUT_MS 30000
#define TCP_RETRY_DELAY_MS 5000
#define TCP_RETRY_LIMIT 5
#define TCP_SOCKET_POLL_MS 1000
#define TCP_KEEPALIVE_IDLE_SECONDS 10
#define TCP_KEEPALIVE_INTERVAL_SECONDS 5
#define TCP_KEEPALIVE_PROBES 3

typedef struct {
    const uint8_t *pcm;
    size_t length;
    TaskHandle_t waiter;
} audio_request_t;

static const char *TAG = "tcp_client";
static TaskHandle_t s_tcp_task;
static QueueHandle_t s_audio_queue;
static volatile esp_err_t s_upload_result;
static char s_displayed_local_ip[16];

static void tcp_set_status(const char *status)
{
    esp_err_t ret = iic_oled_set_text_row(2, status);
    if (ret != ESP_OK) ESP_LOGW(TAG, "OLED network status update failed: %s", esp_err_to_name(ret));
    ESP_LOGI(TAG, "network status: %s", status);
}

static void tcp_refresh_local_ip(void)
{
    char ip[16] = "no IP";
    if (wifi_scan_get_local_ip(ip, sizeof(ip)) != ESP_OK) {
        snprintf(ip, sizeof(ip), "no IP");
    }
    if (strcmp(ip, s_displayed_local_ip) == 0) return;
    snprintf(s_displayed_local_ip, sizeof(s_displayed_local_ip), "%s", ip);
    char row[22];
    snprintf(row, sizeof(row), "ESP:%s", ip);
    esp_err_t ret = iic_oled_set_text_row(0, row);
    if (ret != ESP_OK) ESP_LOGW(TAG, "OLED ESP32 IP update failed: %s", esp_err_to_name(ret));
    ESP_LOGI(TAG, "ESP32 IP display updated: %s", ip);
}

static void tcp_close_socket(int *socket_fd)
{
    if (*socket_fd >= 0) {
        shutdown(*socket_fd, SHUT_RDWR);
        close(*socket_fd);
        *socket_fd = -1;
    }
}

static bool tcp_socket_is_alive(int socket_fd)
{
    char byte;
    int result = recv(socket_fd, &byte, 1, MSG_PEEK | MSG_DONTWAIT);
    if (result == 0) {
        ESP_LOGW(TAG, "peer closed the TCP connection");
        return false;
    }
    if (result < 0 && errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) {
        ESP_LOGW(TAG, "TCP keepalive/socket check failed: errno=%d", errno);
        return false;
    }
    return true;
}

static void tcp_configure_keepalive(int socket_fd)
{
    int enabled = 1;
    int idle = TCP_KEEPALIVE_IDLE_SECONDS;
    int interval = TCP_KEEPALIVE_INTERVAL_SECONDS;
    int probes = TCP_KEEPALIVE_PROBES;
    if (setsockopt(socket_fd, SOL_SOCKET, SO_KEEPALIVE, &enabled, sizeof(enabled)) != 0 ||
        setsockopt(socket_fd, IPPROTO_TCP, TCP_KEEPIDLE, &idle, sizeof(idle)) != 0 ||
        setsockopt(socket_fd, IPPROTO_TCP, TCP_KEEPINTVL, &interval, sizeof(interval)) != 0 ||
        setsockopt(socket_fd, IPPROTO_TCP, TCP_KEEPCNT, &probes, sizeof(probes)) != 0) {
        ESP_LOGW(TAG, "could not configure all TCP keepalive settings: errno=%d", errno);
    } else {
        ESP_LOGI(TAG, "TCP keepalive enabled: idle=%ds interval=%ds probes=%d",
                 idle, interval, probes);
    }
}

static int tcp_connect_to_server(void)
{
    struct sockaddr_in server_address = {
        .sin_family = AF_INET,
        .sin_port = htons(TCP_SERVER_PORT),
    };
    if (inet_pton(AF_INET, TCP_SERVER_IP, &server_address.sin_addr) != 1) {
        ESP_LOGE(TAG, "invalid server address: %s", TCP_SERVER_IP);
        return -1;
    }
    int socket_fd = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
    if (socket_fd < 0) {
        ESP_LOGE(TAG, "socket() failed: errno=%d", errno);
        return -1;
    }
    struct timeval timeout = { .tv_sec = TCP_RESPONSE_TIMEOUT_MS / 1000, .tv_usec = 0 };
    setsockopt(socket_fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    setsockopt(socket_fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
    tcp_configure_keepalive(socket_fd);
    ESP_LOGI(TAG, "connecting to TCP server %s:%d", TCP_SERVER_IP, TCP_SERVER_PORT);
    if (connect(socket_fd, (struct sockaddr *)&server_address, sizeof(server_address)) != 0) {
        ESP_LOGW(TAG, "connect() failed: errno=%d", errno);
        close(socket_fd);
        return -1;
    }
    ESP_LOGI(TAG, "TCP connected");
    return socket_fd;
}

static bool tcp_send_all(int socket_fd, const void *data, size_t length)
{
    const uint8_t *bytes = data;
    size_t sent = 0;
    while (sent < length) {
        int result = send(socket_fd, bytes + sent, length - sent, 0);
        if (result <= 0) {
            ESP_LOGW(TAG, "send() failed: errno=%d", errno);
            return false;
        }
        sent += (size_t)result;
    }
    return true;
}

static bool tcp_receive_line(int socket_fd, char *response, size_t capacity)
{
    size_t used = 0;
    while (used + 1 < capacity) {
        char ch;
        int received = recv(socket_fd, &ch, 1, 0);
        if (received <= 0) {
            ESP_LOGW(TAG, "recv() failed or timed out: errno=%d", errno);
            return false;
        }
        if (ch == '\n') break;
        response[used++] = ch;
    }
    response[used] = '\0';
    return true;
}

static bool tcp_exchange_text(int socket_fd)
{
    if (!tcp_send_all(socket_fd, TCP_MESSAGE, strlen(TCP_MESSAGE))) return false;
    char response[128];
    if (!tcp_receive_line(socket_fd, response, sizeof(response))) return false;
    ESP_LOGI(TAG, "received: %s", response);
    if (strcmp(response, "ACK from PC") != 0) {
        ESP_LOGW(TAG, "initial TCP test returned unexpected response: %s", response);
        return false;
    }
    return true;
}

static bool tcp_upload_audio(int socket_fd, const audio_request_t *request)
{
    uint8_t header[16] = {
        'P', 'C', 'M', '1',
        0, 0, 0x3E, 0x80, /* 16000 Hz */
        0, 1,             /* mono */
        0, 16,            /* 16-bit */
        (uint8_t)(request->length >> 24),
        (uint8_t)(request->length >> 16),
        (uint8_t)(request->length >> 8),
        (uint8_t)request->length,
    };
    if (!tcp_send_all(socket_fd, header, sizeof(header)) ||
        !tcp_send_all(socket_fd, request->pcm, request->length)) return false;
    char response[128];
    if (!tcp_receive_line(socket_fd, response, sizeof(response))) return false;
    ESP_LOGI(TAG, "audio server response: %s", response);
    s_upload_result = strncmp(response, "SAVED", 5) == 0 ? ESP_OK : ESP_FAIL;
    return true;
}

static void tcp_complete_request(const audio_request_t *request, esp_err_t result)
{
    s_upload_result = result;
    xTaskNotifyGive(request->waiter);
}

static void tcp_fail_pending_audio(const char *reason)
{
    audio_request_t request;
    while (xQueueReceive(s_audio_queue, &request, 0) == pdTRUE) {
        ESP_LOGW(TAG, "failing queued audio upload: %s", reason);
        tcp_complete_request(&request, ESP_FAIL);
    }
}

static void tcp_client_task(void *arg)
{
    (void)arg;
    int socket_fd = -1;
    unsigned consecutive_failures = 0;
    bool link_was_up = false;

    s_displayed_local_ip[0] = '\0';
    iic_oled_set_text_row(0, "ESP:no IP");
    char pc_row[22];
    snprintf(pc_row, sizeof(pc_row), "PC:%s", TCP_SERVER_IP);
    iic_oled_set_text_row(1, pc_row);
    tcp_set_status("try to connect");

    while (true) {
        tcp_refresh_local_ip();

        if (!wifi_scan_is_connected()) {
            tcp_fail_pending_audio("Wi-Fi is not connected");
            if (socket_fd >= 0) {
                if (link_was_up) tcp_set_status("disconnect");
                ESP_LOGW(TAG, "Wi-Fi unavailable; closing TCP socket");
                tcp_close_socket(&socket_fd);
                link_was_up = false;
                tcp_set_status("try to connect");
            }
            vTaskDelay(pdMS_TO_TICKS(TCP_SOCKET_POLL_MS));
            continue;
        }

        if (socket_fd < 0) {
            tcp_set_status("try to connect");
            ESP_LOGI(TAG, "TCP connection attempt %u/%u", consecutive_failures + 1,
                     TCP_RETRY_LIMIT);
            socket_fd = tcp_connect_to_server();
            if (socket_fd < 0) {
                ++consecutive_failures;
                tcp_fail_pending_audio("TCP connection failed");
                if (consecutive_failures >= TCP_RETRY_LIMIT) {
                    tcp_set_status("failed,reset for 5s");
                    ESP_LOGW(TAG, "TCP failed %u consecutive attempts; restarting retry cycle",
                             consecutive_failures);
                    consecutive_failures = 0;
                }
                vTaskDelay(pdMS_TO_TICKS(TCP_RETRY_DELAY_MS));
                continue;
            }
            if (!tcp_exchange_text(socket_fd)) {
                ++consecutive_failures;
                tcp_close_socket(&socket_fd);
                tcp_fail_pending_audio("TCP round-trip test failed");
                if (consecutive_failures >= TCP_RETRY_LIMIT) {
                    tcp_set_status("failed,reset for 5s");
                    ESP_LOGW(TAG, "TCP test failed %u consecutive attempts; restarting retry cycle",
                             consecutive_failures);
                    consecutive_failures = 0;
                }
                vTaskDelay(pdMS_TO_TICKS(TCP_RETRY_DELAY_MS));
                continue;
            }
            ESP_LOGI(TAG, "one-time TCP round-trip test passed");
            consecutive_failures = 0;
            link_was_up = true;
            tcp_set_status("connecting");
        }

        audio_request_t request;
        if (xQueueReceive(s_audio_queue, &request, pdMS_TO_TICKS(TCP_SOCKET_POLL_MS)) == pdTRUE) {
            bool connected = tcp_upload_audio(socket_fd, &request);
            tcp_complete_request(&request, connected ? s_upload_result : ESP_FAIL);
            if (!connected) {
                ESP_LOGW(TAG, "TCP connection lost while uploading audio");
                tcp_set_status("disconnect");
                tcp_close_socket(&socket_fd);
                link_was_up = false;
                tcp_set_status("try to connect");
            }
            continue;
        }

        if (!wifi_scan_is_connected() || !tcp_socket_is_alive(socket_fd)) {
            tcp_set_status("disconnect");
            tcp_close_socket(&socket_fd);
            link_was_up = false;
            tcp_set_status("try to connect");
        }
    }
}

esp_err_t tcp_client_start(void)
{
    if (s_tcp_task != NULL) return ESP_ERR_INVALID_STATE;
    s_audio_queue = xQueueCreate(1, sizeof(audio_request_t));
    if (s_audio_queue == NULL) return ESP_ERR_NO_MEM;
    if (xTaskCreate(tcp_client_task, "tcp_client", 4096, NULL, 5, &s_tcp_task) != pdPASS) {
        vQueueDelete(s_audio_queue);
        s_audio_queue = NULL;
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

esp_err_t tcp_client_upload_pcm(const uint8_t *pcm, size_t length)
{
    if (pcm == NULL || length == 0 || s_audio_queue == NULL || s_tcp_task == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    audio_request_t request = { .pcm = pcm, .length = length, .waiter = xTaskGetCurrentTaskHandle() };
    ulTaskNotifyTake(pdTRUE, 0);
    if (xQueueSend(s_audio_queue, &request, 0) != pdTRUE) return ESP_ERR_TIMEOUT;
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    return s_upload_result;
}
