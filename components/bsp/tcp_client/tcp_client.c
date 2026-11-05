#include "tcp_client.h"

#include <errno.h>
#include <string.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "iic.h"
#include "lwip/inet.h"
#include "lwip/sockets.h"

#define TCP_SERVER_IP "192.168.1.112"
#define TCP_SERVER_PORT 5000
#define TCP_MESSAGE "hello from ESP32\n"
#define TCP_RESPONSE_TIMEOUT_MS 30000
#define TCP_RETRY_DELAY_MS 5000

typedef struct {
    const uint8_t *pcm;
    size_t length;
    TaskHandle_t waiter;
} audio_request_t;

static const char *TAG = "tcp_client";
static TaskHandle_t s_tcp_task;
static QueueHandle_t s_audio_queue;
static volatile esp_err_t s_upload_result;

static void tcp_oled_rows(const char *line0, const char *line1)
{
    if (line0 != NULL) iic_oled_set_text_row(0, line0);
    if (line1 != NULL) iic_oled_set_text_row(1, line1);
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
    ESP_LOGI(TAG, "connecting to TCP server %s:%d", TCP_SERVER_IP, TCP_SERVER_PORT);
    tcp_oled_rows("TCP connecting", "");
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
    tcp_oled_rows("TCP connected", "ACK received");
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
    tcp_oled_rows("TCP connected", response[0] == 'S' ? "ACK received" : "server error");
    s_upload_result = strncmp(response, "SAVED", 5) == 0 ? ESP_OK : ESP_FAIL;
    return true;
}

static void tcp_complete_request(const audio_request_t *request, esp_err_t result)
{
    s_upload_result = result;
    xTaskNotifyGive(request->waiter);
}

static void tcp_fail_pending_audio(void)
{
    audio_request_t request;
    while (xQueueReceive(s_audio_queue, &request, 0) == pdTRUE) {
        tcp_complete_request(&request, ESP_FAIL);
    }
}

static void tcp_client_task(void *arg)
{
    (void)arg;
    int socket_fd = -1;
    while (true) {
        if (socket_fd < 0) {
            socket_fd = tcp_connect_to_server();
            if (socket_fd < 0) {
                tcp_oled_rows("TCP connect fail", "retry in 5 seconds");
                tcp_fail_pending_audio();
                vTaskDelay(pdMS_TO_TICKS(TCP_RETRY_DELAY_MS));
                continue;
            }
        }

        audio_request_t request;
        if (xQueueReceive(s_audio_queue, &request, pdMS_TO_TICKS(TCP_RETRY_DELAY_MS)) == pdTRUE) {
            bool connected = tcp_upload_audio(socket_fd, &request);
            tcp_complete_request(&request, connected ? s_upload_result : ESP_FAIL);
            if (!connected) {
                tcp_oled_rows("TCP disconnected", "reconnecting");
                shutdown(socket_fd, SHUT_RDWR);
                close(socket_fd);
                socket_fd = -1;
            }
            continue;
        }

        if (!tcp_exchange_text(socket_fd)) {
            tcp_oled_rows("TCP disconnected", "reconnecting");
            shutdown(socket_fd, SHUT_RDWR);
            close(socket_fd);
            socket_fd = -1;
            vTaskDelay(pdMS_TO_TICKS(TCP_RETRY_DELAY_MS));
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
