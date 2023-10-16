#include "common.h"
#include "c_clock.h"
#include "c_clock_http.h"
#include "c_notice.h"
#include "c_offline.h"
#include "c_http.h"
#include "c_audio.h"

#include "esp_event.h"
#include "nvs_flash.h"

#include "lwip/sockets.h"
#include "lwip/err.h"
#include "lwip/sys.h"

#include <esp_http_server.h>
#include "cJSON.h"

static const char *TAG = "clock_http";

EXT_RAM_ATTR static char alarm_status = 0;
EXT_RAM_ATTR static char alarm_hour = 0;
EXT_RAM_ATTR static char alarm_min = 0;
EXT_RAM_ATTR static char sleep_status = 0;
EXT_RAM_ATTR static char sleep_hour = 0;
EXT_RAM_ATTR static char sleep_min = 0;
// HTTP GET 请求回调处理函数
static esp_err_t esp_get_handler(httpd_req_t *req)
{
    cJSON* cjson_test = NULL;
    cjson_test = cJSON_CreateObject();
    cJSON_AddNumberToObject(cjson_test, "alarm_status", alarm_status);
    cJSON_AddNumberToObject(cjson_test, "alarm_hour", alarm_hour);
    cJSON_AddNumberToObject(cjson_test, "alarm_minute", alarm_min);
    cJSON_AddNumberToObject(cjson_test, "sleep_status", sleep_status);
    cJSON_AddNumberToObject(cjson_test, "sleep_hour", sleep_hour);
    cJSON_AddNumberToObject(cjson_test, "sleep_minute", sleep_min);
    char *buf = cJSON_Print(cjson_test);
    printf("%s\n", buf);
    // 发送含有灯的状态的 JSON 格式数据给客户端
    httpd_resp_send(req, buf, strlen(buf));
    cJSON_Delete(cjson_test);
    return ESP_OK;
}

// HTTP POST 请求回调处理函数
static esp_err_t esp_set_handler(httpd_req_t *req)
{
    char buf[128];
    char *url = (char *)malloc(strlen(req->uri) + 1);
    strcpy(url, req->uri);
    int ret, remaining = req->content_len;
    memset(buf, 0 ,sizeof(buf));
    while (remaining > 0) {
        // 读取 http 请求数据
        if ((ret = httpd_req_recv(req, buf, remaining)) <= 0) {
            if (ret == HTTPD_SOCK_ERR_TIMEOUT) {
                continue;
            }
            return ESP_FAIL;
        }
        remaining -= ret;
    }
    ESP_LOGI(TAG, "%.*s\n", req->content_len, buf);
    ESP_LOGI(TAG, "url:%s\n", url);
    httpd_resp_send(req, "OK", strlen("OK"));
    // TODO: 读到数据后操作
    cJSON* cjson_test = cJSON_Parse(buf);
    if(strcmp(url, "/ctrl") == 0)
    {
        cJSON* cjson_alarm_status = cJSON_GetObjectItem(cjson_test, "alarm_status");
        cJSON* cjson_sleep_status = cJSON_GetObjectItem(cjson_test, "sleep_status");
        alarm_status = cjson_alarm_status->valueint;
        sleep_status = cjson_sleep_status->valueint;
        cJSON_Delete(cjson_test);
    }else if(strcmp(url, "/alarm") == 0){
        cJSON* cjson_alarm_hour = cJSON_GetObjectItem(cjson_test, "alarm_hour");
        cJSON* cjson_alarm_min = cJSON_GetObjectItem(cjson_test, "alarm_min");
        alarm_hour = cjson_alarm_hour->valueint;
        alarm_min = cjson_alarm_min->valueint;
        cJSON_Delete(cjson_test);
    }else if(strcmp(url, "/sleep") == 0){
        cJSON* cjson_sleep_hour = cJSON_GetObjectItem(cjson_test, "sleep_hour");
        cJSON* cjson_sleep_min = cJSON_GetObjectItem(cjson_test, "sleep_min");
        sleep_hour = cjson_sleep_hour->valueint;
        sleep_min = cjson_sleep_min->valueint;
        cJSON_Delete(cjson_test);
    }else if(strcmp(url, "/time") == 0){
        struct timeval timeinfo;
        cJSON* cjson_time_sec = cJSON_GetObjectItem(cjson_test, "time_sec");
        // timeinfo.tv_sec = ((long)(cjson_time_sec->valuedouble))/1000;
        // timeinfo.tv_usec = ((long)(cjson_time_sec->valuedouble))%1000;    
        timeinfo.tv_sec = (__uint64_t)((__uint64_t)(cjson_time_sec->valuedouble))/1000;
        timeinfo.tv_usec = (__uint64_t)((__uint64_t)(cjson_time_sec->valuedouble))%1000;
        settimeofday(&timeinfo, NULL);
        //sntp_sync_time(&timeinfo);
        vTaskDelay(2000 / portTICK_PERIOD_MS);
        cJSON_Delete(cjson_test);

    }
    free(url);
    return ESP_OK;
}

// Get 对应的处理回调函数
static const httpd_uri_t status = {
    .uri       = "/status",
    .method    = HTTP_GET,
    .handler   = esp_get_handler,
};

// Post 对应的处理回调函数
static const httpd_uri_t ctrl = {
    .uri       = "/ctrl",
    .method    = HTTP_POST,
    .handler   = esp_set_handler,
};

// Post 对应的处理回调函数
static const httpd_uri_t alarm_set = {
    .uri       = "/alarm",
    .method    = HTTP_POST,
    .handler   = esp_set_handler,
};

// Post 对应的处理回调函数
static const httpd_uri_t sleep_set = {
    .uri       = "/sleep",
    .method    = HTTP_POST,
    .handler   = esp_set_handler,
};

// Post 对应的处理回调函数
static const httpd_uri_t time_set = {
    .uri       = "/time",
    .method    = HTTP_POST,
    .handler   = esp_set_handler,
};

esp_err_t esp_start_webserver(void)
{
    httpd_handle_t server = NULL;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.lru_purge_enable = true;

    // 启动 http 服务器
    ESP_LOGI(TAG, "Starting server on port: '%d'", config.server_port);
    if (httpd_start(&server, &config) == ESP_OK) {
        // 设置 HTTP URI 对应的回调处理函数
        ESP_LOGI(TAG, "Registering URI handlers");
        httpd_register_uri_handler(server, &status);
        httpd_register_uri_handler(server, &time_set);
        httpd_register_uri_handler(server, &ctrl);
        httpd_register_uri_handler(server, &alarm_set);
        httpd_register_uri_handler(server, &sleep_set);
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Error starting server!");
    return ESP_FAIL;
}

void esp_receive_broadcast(void *args)
{
   struct sockaddr_in from_addr = {0};
   socklen_t from_addr_len = sizeof(struct sockaddr_in);
   char udp_server_buf[64 + 1] = {0};

   // 创建 IPv4 UDP 套接字
   int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
   if (sockfd == -1) {
      ESP_LOGE(TAG, "Create UDP socket fail");
      vTaskDelete(NULL);
   }

   // 设置广播目的地址和端口
   struct sockaddr_in server_addr = {
      .sin_family      = AF_INET,
      .sin_port        = htons(3333),
      .sin_addr.s_addr = htonl(INADDR_ANY),
   };

   int ret = bind(sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr));
   if (ret < 0) {
      ESP_LOGE(TAG, "Bind socket fail");
      goto exit;
   }

   // 调用 recvfrom 接口接收广播数据
   while (1) {
      ret = recvfrom(sockfd, udp_server_buf, sizeof(udp_server_buf) - 1, 0, (struct sockaddr *)&from_addr, (socklen_t *)&from_addr_len);
      if (ret > 0) {
         ESP_LOGI(TAG, "Receive udp broadcast from %s:%d, data is %s", inet_ntoa(((struct sockaddr_in *)&from_addr)->sin_addr), ntohs(((struct sockaddr_in *)&from_addr)->sin_port), udp_server_buf);
         // 如果收到广播请求数据，单播发送对端数据通信应用端口
         if (!strcmp(udp_server_buf, "123")) {
            cJSON* cjson_test = NULL;
            cjson_test = cJSON_CreateObject();
            cJSON_AddNumberToObject(cjson_test, "alarm_status", alarm_status);
            cJSON_AddNumberToObject(cjson_test, "alarm_hour", alarm_hour);
            cJSON_AddNumberToObject(cjson_test, "alarm_minute", alarm_min);
            cJSON_AddNumberToObject(cjson_test, "sleep_status", sleep_status);
            cJSON_AddNumberToObject(cjson_test, "sleep_hour", sleep_hour);
            cJSON_AddNumberToObject(cjson_test, "sleep_minute", sleep_min);
            char *udp_server_send_buf = cJSON_Print(cjson_test);
            ESP_LOGI(TAG,"State message:\n%s\n", udp_server_send_buf);
            //返回状态信息
            ret = sendto(sockfd, udp_server_send_buf, strlen(udp_server_send_buf), 0, (struct sockaddr *)&from_addr, from_addr_len);
            if (ret < 0) {
               ESP_LOGE(TAG, "Error occurred during sending");
            } else {
               ESP_LOGI(TAG, "Message sent successfully");
            }
            cJSON_Delete(cjson_test);
         }
      }
   }
exit:
   close(sockfd);
}

void notice(void *args)
{
    while(1)
    {
        time_t now;
        struct tm timeinfo;
        // Set timezone to China Standard Time
        setenv("TZ", "CST-8", 1);
        tzset();
        time(&now);
        localtime_r(&now, &timeinfo);
        if (timeinfo.tm_year < (2016 - 1900)) {
        }else{
            if(alarm_status)
            {
                if(alarm_hour == timeinfo.tm_hour && alarm_min == timeinfo.tm_min && timeinfo.tm_sec == 0)
                {
                    printf("alarm\n");
                    if(system_mode){
                        xEventGroupClearBits(offline_event_group, OFFLINE_RUN_BIT);
                        vTaskDelay(5000 / portTICK_PERIOD_MS);
                        offline_notice(1);
                        xEventGroupSetBits(offline_event_group, OFFLINE_RUN_BIT);
                        //tts_play("闹钟响了");
                        xTaskCreate(offline_control, "offline_control", 1024*4, NULL, 5, &offline_task_handle);
                    }else{
                        xEventGroupClearBits(task_event_group, TASK_RUN_BIT);
                        vTaskDelay(5000 / portTICK_PERIOD_MS);
                        memset(qianfan_message_content,'\0',sizeof(qianfan_message_content));
                        strcpy(qianfan_message_content, "时间到了，闹钟响啦");
                        xEventGroupSetBits(task_event_group, TASK_RUN_BIT);
                        xTaskCreate(baidu_tts, "baidu_tts", 1024*4, &music_url, 5, &baidu_tts_task_handle);
                    }
                }
            }
            if(sleep_status)
            {
                if(sleep_hour == timeinfo.tm_hour && sleep_min == timeinfo.tm_min && timeinfo.tm_sec == 0)
                {
                    printf("sleep\n");
                    if(system_mode){
                        xEventGroupClearBits(offline_event_group, OFFLINE_RUN_BIT);
                        vTaskDelay(5000 / portTICK_PERIOD_MS);
                        offline_notice(0);
                        xEventGroupSetBits(offline_event_group, OFFLINE_RUN_BIT);
                        //tts_play("该睡觉了");
                        xTaskCreate(offline_control, "offline_control", 1024*4, NULL, 5, &offline_task_handle);
                    }else{
                        xEventGroupClearBits(task_event_group, TASK_RUN_BIT);
                        vTaskDelay(5000 / portTICK_PERIOD_MS);
                        memset(qianfan_message_content,'\0',sizeof(qianfan_message_content));
                        strcpy(qianfan_message_content, "时间到了，该睡觉啦");
                        xEventGroupSetBits(task_event_group, TASK_RUN_BIT);
                        xTaskCreate(baidu_tts, "baidu_tts", 1024*4, &music_url, 5, &baidu_tts_task_handle);

                    }
                }
            }
        }
        vTaskDelay(100 / portTICK_PERIOD_MS);
    }
}