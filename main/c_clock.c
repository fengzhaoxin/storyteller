#include "common.h"
#include "c_clock.h"

#include "esp_event.h"

#include "esp_attr.h"
#include "nvs_flash.h"

static const char *TAG = "CLOCK";

__uint8_t obtain_time(void)
{
    __uint8_t state=0;
    time_t now;
    struct tm timeinfo;
    // Set timezone to China Standard Time
    setenv("TZ", "CST-8", 1);
    tzset();
    time(&now);
    localtime_r(&now, &timeinfo);
    if (timeinfo.tm_year < (2016 - 1900)) {
        ESP_LOGI(TAG, "Time is not set yet. Connecting to WiFi and getting time over NTP.");
    }else{
        ESP_LOGI(TAG, "The current date/time in Shanghai is: %d:%d:%d", timeinfo.tm_hour,timeinfo.tm_min,timeinfo.tm_sec);
        state=1;
    }
    return state;
}

static void time_sync_notification_cb(struct timeval *tv)
{
    ESP_LOGI(TAG, "Notification of a time synchronization event");
}

static void initialize_sntp(void)
{
    ESP_LOGI(TAG, "Initializing SNTP");
    sntp_setoperatingmode(SNTP_OPMODE_POLL);
    sntp_setservername(0, "ntp.aliyun.com");
    sntp_set_time_sync_notification_cb(time_sync_notification_cb);
    sntp_init();
}

void set_time(void)
{

    initialize_sntp();

    // wait for time to be set
    time_t now = 0;
    struct tm timeinfo = { 0 };
    int retry = 0;
    const int retry_count = 10;
    while (sntp_get_sync_status() == SNTP_SYNC_STATUS_RESET && ++retry < retry_count) {
        ESP_LOGI(TAG, "Waiting for system time to be set... (%d/%d)", retry, retry_count);
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }

    time(&now);
    localtime_r(&now, &timeinfo);

}
