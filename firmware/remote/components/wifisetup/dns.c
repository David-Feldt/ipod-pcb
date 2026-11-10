#include <string.h>
#include "dns.h"
#include "esp_log.h"
#include "lwip/sockets.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

static const char *TAG = "dns";
static volatile bool s_run;
static SemaphoreHandle_t s_done;

#define AP_IP_BYTES 192, 168, 4, 1

static void dns_task(void *arg)
{
    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    struct sockaddr_in a = { .sin_family = AF_INET, .sin_port = htons(53), .sin_addr.s_addr = htonl(INADDR_ANY) };
    struct timeval tv = { .tv_sec = 1 };
    if (sock < 0 || bind(sock, (struct sockaddr *)&a, sizeof a) < 0) {
        ESP_LOGE(TAG, "cannot bind port 53");
        if (sock >= 0) close(sock);
        sock = -1;
        s_run = false;
    } else {
        setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
    }
    uint8_t buf[512];
    while (s_run) {
        struct sockaddr_in from;
        socklen_t fl = sizeof from;
        int n = recvfrom(sock, buf, sizeof buf - 16, 0, (struct sockaddr *)&from, &fl);
        if (n < 12) continue;
        // Standard query (QR=0, opcode 0) with exactly one question.
        if ((buf[2] & 0xF8) != 0 || buf[4] != 0 || buf[5] != 1) continue;
        int p = 12;
        while (p < n && buf[p]) {
            if (buf[p] & 0xC0) { p = n; break; }        // no compression in questions
            p += buf[p] + 1;
        }
        if (p + 5 > n) continue;
        int qend = p + 5;                               // zero byte + QTYPE + QCLASS
        bool is_a = buf[p + 1] == 0 && buf[p + 2] == 1 && buf[p + 3] == 0 && buf[p + 4] == 1;
        buf[2] = 0x84 | (buf[2] & 0x01);                // response, authoritative, keep RD
        buf[3] = 0x00;                                  // no error
        buf[6] = 0; buf[7] = is_a ? 1 : 0;              // answers
        memset(buf + 8, 0, 4);                          // no authority / additional records
        int len = qend;
        if (is_a) {
            const uint8_t ans[] = { 0xC0, 0x0C, 0, 1, 0, 1, 0, 0, 0, 10, 0, 4, AP_IP_BYTES };
            memcpy(buf + len, ans, sizeof ans);
            len += sizeof ans;
        }
        sendto(sock, buf, len, 0, (struct sockaddr *)&from, fl);
    }
    if (sock >= 0) close(sock);
    xSemaphoreGive(s_done);
    vTaskDelete(NULL);
}

esp_err_t dns_start(void)
{
    if (s_run) return ESP_OK;
    if (!s_done && !(s_done = xSemaphoreCreateBinary())) return ESP_ERR_NO_MEM;
    s_run = true;
    if (xTaskCreate(dns_task, "dns", 3072, NULL, 4, NULL) != pdPASS) { s_run = false; return ESP_ERR_NO_MEM; }
    return ESP_OK;
}

void dns_stop(void)
{
    if (!s_run) return;
    s_run = false;
    xSemaphoreTake(s_done, pdMS_TO_TICKS(3000));     // the task notices within 1 s
}
