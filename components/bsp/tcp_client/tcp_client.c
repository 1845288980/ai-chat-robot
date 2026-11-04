#include "tcp_client.h"

#include <errno.h>
#include <string.h>

#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "iic.h"
#include "lwip/inet.h"
#include "lwip/sockets.h"

#define TCP_SERVER_IP "192.168.1.142"
#define TCP_SERVER_PORT 5000
#define TCP_MESSAGE "hello from ESP32\n"
#define TCP_RESPONSE_TIMEOUT_MS 3000
#define TCP_RETRY_DELAY_MS 5000

static const char *TAG = "tcp_client";
static TaskHandle_t s_tcp_task;

static void tcp_oled_message(const char *line0, const char *line1)
{
    iic_oled_clear();
    iic_oled_write_string(0, 0, line0, true);
    if (line1 != NULL) {
        iic_oled_write_string(0, 1, line1, true);
    }
    esp_err_t ret = iic_oled_update();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "OLED update failed: %s", esp_err_to_name(ret));
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

    struct timeval timeout = {
        .tv_sec = TCP_RESPONSE_TIMEOUT_MS / 1000,
        .tv_usec = (TCP_RESPONSE_TIMEOUT_MS % 1000) * 1000,
    };
    if (setsockopt(socket_fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) != 0) {
        ESP_LOGW(TAG, "setsockopt(SO_RCVTIMEO) failed: errno=%d", errno);
    }

    ESP_LOGI(TAG, "connecting to TCP server %s:%d", TCP_SERVER_IP, TCP_SERVER_PORT);
    tcp_oled_message("TCP connecting", NULL);
    if (connect(socket_fd, (struct sockaddr *)&server_address, sizeof(server_address)) != 0) {
        ESP_LOGW(TAG, "connect() failed: errno=%d", errno);
        close(socket_fd);
        return -1;
    }
    ESP_LOGI(TAG, "TCP connected");
    return socket_fd;
}

static bool tcp_send_all(int socket_fd, const char *data, size_t length)
{
    size_t sent = 0;
    while (sent < length) {
        int result = send(socket_fd, data + sent, length - sent, 0);
        if (result <= 0) {
            ESP_LOGW(TAG, "send() failed: errno=%d", errno);
            return false;
        }
        sent += (size_t)result;
    }
    return true;
}

static bool tcp_exchange_message(int socket_fd)
{
    if (!tcp_send_all(socket_fd, TCP_MESSAGE, strlen(TCP_MESSAGE))) {
        return false;
    }
    ESP_LOGI(TAG, "sent: %s", "hello from ESP32");

    char response[128] = {0};
    int received = recv(socket_fd, response, sizeof(response) - 1, 0);
    if (received <= 0) {
        ESP_LOGW(TAG, "recv() failed or timed out: errno=%d", errno);
        return false;
    }
    response[received] = '\0';
    ESP_LOGI(TAG, "received: %s", response);
    tcp_oled_message("TCP connected", "ACK received");
    return true;
}

static void tcp_client_task(void *arg)
{
    (void)arg;
    int socket_fd = -1;
    while (true) {
        if (socket_fd < 0) {
            socket_fd = tcp_connect_to_server();
            if (socket_fd < 0) {
                tcp_oled_message("TCP connect fail", "retry in 5 seconds");
                vTaskDelay(pdMS_TO_TICKS(TCP_RETRY_DELAY_MS));
                continue;
            }
        }

        if (!tcp_exchange_message(socket_fd)) {
            tcp_oled_message("TCP disconnected", "reconnecting");
            shutdown(socket_fd, SHUT_RDWR);
            close(socket_fd);
            socket_fd = -1;
            vTaskDelay(pdMS_TO_TICKS(TCP_RETRY_DELAY_MS));
            continue;
        }
        vTaskDelay(pdMS_TO_TICKS(TCP_RETRY_DELAY_MS));
    }
}

esp_err_t tcp_client_start(void)
{
    if (s_tcp_task != NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    if (xTaskCreate(tcp_client_task, "tcp_client", 4096, NULL, 5, &s_tcp_task) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}
