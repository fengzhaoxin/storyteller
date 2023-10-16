/*
 * ESPRESSIF MIT License
 *
 * Copyright (c) 2022 <ESPRESSIF SYSTEMS (SHANGHAI) CO., LTD>
 *
 * Permission is hereby granted for use on all ESPRESSIF SYSTEMS products, in which case,
 * it is free of charge, to any person obtaining a copy of this software and associated
 * documentation files (the "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the Software is furnished
 * to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all copies or
 * substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
 * FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 */
#include "common.h"
#include "c_init.h"
#include "c_wifi_setting.h"
#include "c_offline.h"
#include "c_clock_http.h"
#include "c_http.h"
#include "c_audio.h"
#include "c_vad.h"

TaskHandle_t offline_task_handle = NULL;

EventGroupHandle_t offline_event_group;

void app_main(void)
{
    log_clear();
    nvs_init();
    wifi_init();
    gpio_init();

    if(system_mode)
    {
        gpio_set_level(GPIO_OUTPUT_IO_0, 1);
        offline_event_group = xEventGroupCreate();
        xEventGroupSetBits(offline_event_group, OFFLINE_RUN_BIT);
        xTaskCreate(offline_control, "offline_control", 1024*4, NULL, 4, &offline_task_handle);
    }
    else
    {
        gpio_set_level(GPIO_OUTPUT_IO_0, 0);
        peripherals_init();
        baidu_get_token();
        process_task_start();
        vad_start();
        //create_wenxin_request("你好");

        // get_music_list();
        // vTaskDelay(1000 / portTICK_PERIOD_MS);
        // get_music_url();
        // vTaskDelay(1000 / portTICK_PERIOD_MS);
        // xTaskCreate(http_mp3_play, "http_music", 1024*4, &music_url, 5, &http_mp3_task_handle);
    }
    xTaskCreate(esp_receive_broadcast, "broadcast_task", 1024*4, NULL, 5, NULL);
    xTaskCreate(notice, "notice_task", 1024*4, NULL, 5, NULL);
    esp_start_webserver();
}
