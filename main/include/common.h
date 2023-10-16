#ifndef _COMMON_H_
#define _COMMON_H_

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_log.h"

#include "nvs_flash.h"
#include "nvs.h"

#include "driver/gpio.h"

#define GPIO_OUTPUT_IO_0    22
#define GPIO_OUTPUT_IO_1    27
#define GPIO_OUTPUT_PIN_SEL  ((1ULL<<GPIO_OUTPUT_IO_0) | (1ULL<<GPIO_OUTPUT_IO_1))

extern uint8_t system_mode;

extern TaskHandle_t offline_task_handle;
extern EventGroupHandle_t offline_event_group;
#define OFFLINE_RUN_BIT BIT0

#endif