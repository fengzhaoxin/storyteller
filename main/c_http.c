#include "esp_http_client.h"
#include "cJSON.h"
#include "baidu_access_token.h"

#include "common.h"
#include "c_http.h"
#include "c_audio.h"

static const char *TAG = "C_HTTP";

EventGroupHandle_t task_event_group;

EXT_RAM_ATTR TaskHandle_t http_mp3_task_handle = NULL;
EXT_RAM_ATTR TaskHandle_t baidu_tts_task_handle = NULL;

#define MAX_HTTP_RECV_BUFFER 1024

enum _process_msg_id {
    SPEECH_GET = 1,
    QIANFAN_GET,
};
QueueHandle_t process_rec_q = NULL;

static char *qianfan_id = "yourid";
static char *speech_id = "yourid";

static char *qianfan_sec = "yoursecrect";
static char *speech_sec = "yoursecrect";

static char *qianfan_token = NULL;
char *speech_token = NULL;

EXT_RAM_ATTR static char speech_message_content[50]={0};
char qianfan_message_content[2048]={0};

#define cookie "NMTID=yourmusiccookie"
EXT_RAM_ATTR static __int64_t music_id = 0;//music id
EXT_RAM_ATTR char music_url[200] = {0};

int baidu_get_token(void)
{
    qianfan_token = baidu_get_access_token(qianfan_id, qianfan_sec);
    speech_token = baidu_get_access_token(speech_id, speech_sec);

    if (qianfan_token == NULL || speech_token == NULL) {
        ESP_LOGE(TAG, "Error issuing access token");
        return ESP_FAIL;
    }
    return ESP_OK;
}

static void parse_response (const char *data)
{
    cJSON* cjson_test = NULL;
    cJSON* cjson_result = NULL;
    cJSON* cjson_speech_message = NULL;
    cjson_test = cJSON_Parse(data);
    if(cjson_test == NULL)
    {
        printf("parse fail.\n");
        return;
    }
    printf("here\n");
 
    /* Parsing Chat GPT response*/
    cjson_result = cJSON_GetObjectItem(cjson_test, "result");
    if (cjson_result){
        cjson_speech_message = cJSON_GetArrayItem(cjson_result, 0);
        if(cjson_speech_message)
        {
            memset(speech_message_content,'\0',sizeof(speech_message_content));
            strcpy(speech_message_content,cjson_speech_message->valuestring);
            ESP_LOGI(TAG, "speech_message_content phased!\n");
            ESP_LOGI(TAG, "speech_message_content::: %s\n", speech_message_content);
            int msg = SPEECH_GET;
            if (xQueueSend(process_rec_q, &msg, 0) != pdPASS) {
                ESP_LOGE(TAG, "Speech get failed");
            }
        }else{
            memset(qianfan_message_content,'\0',sizeof(qianfan_message_content));
            strcpy(qianfan_message_content,cjson_result->valuestring);
            ESP_LOGI(TAG, "Qianfan_message_content phased!\n");
            ESP_LOGI(TAG, "speech_message_content::: %s\n", qianfan_message_content);
            int msg = QIANFAN_GET;
            if (xQueueSend(process_rec_q, &msg, 0) != pdPASS) {
                ESP_LOGE(TAG, "Qianfan get failed");
            }
        }
    }
    cJSON_Delete(cjson_test);
}

static esp_err_t response_handler(esp_http_client_event_t *evt)
{
    static char *data = NULL; // Initialize data to NULL
    static int data_len = 0; // Initialize data to NULL
 
    switch (evt->event_id) {
    case HTTP_EVENT_ERROR:
        ESP_LOGI(TAG, "HTTP_EVENT_ERROR");
        break;

    case HTTP_EVENT_HEADER_SENT:
        ESP_LOGI(TAG, "HTTP_EVENT_HEADER_SENT");
        break;
 
    case HTTP_EVENT_ON_DATA:
        ESP_LOGI(TAG, "HTTP_EVENT_ON_DATA (%d +)%d\n", data_len, evt->data_len);
        ESP_LOGI(TAG, "Raw Response: data length: (%d +)%d: %.*s\n", data_len, evt->data_len, evt->data_len, (char *)evt->data);
        
        // Allocate memory for the incoming data        
        data = heap_caps_realloc(data, data_len + evt->data_len + 1,  MALLOC_CAP_8BIT);
        if (data == NULL) {
                ESP_LOGE(TAG, "data realloc failed");
            free(data);
            data = NULL;
            break;
        }
        memcpy(data + data_len, (char *)evt->data, evt->data_len);
        data_len += evt->data_len;
        data[data_len] = '\0';
        break;
 
    case HTTP_EVENT_ON_FINISH:
        ESP_LOGI(TAG, "HTTP_EVENT_ON_FINISH");
        if (data != NULL) {
            // Process the raw data
            parse_response(data);
            // Free memory
            free(data); 
            data = NULL;
            data_len = 0;
        }else{
            ESP_LOGI(TAG, "data is NULL");
        }
        break;

    default:
        break;
    }
    return ESP_OK;
}


esp_err_t create_wenxin_request(const char *content)
{
    char url[184] = "https://aip.baidubce.com/rpc/2.0/ai_custom/v1/wenxinworkshop/chat/completions?access_token=";
    strcat(url, qianfan_token);
    printf(url);printf("\n");

    cJSON *pRoot = cJSON_CreateObject();                         // 创建JSON根部结构体
    cJSON * pArray = cJSON_CreateArray();                        // 创建数组类型结构体
    cJSON_AddItemToObject(pRoot,"messages",pArray);                  // 添加数组到根部结构体
    cJSON * pArray_relay = cJSON_CreateObject();                 // 创建JSON子叶结构体
    cJSON_AddItemToArray(pArray,pArray_relay);                   // 添加子叶结构体到数组结构体            
    cJSON_AddStringToObject(pArray_relay, "role", "user");        // 添加字符串类型数据到子叶结构体
    cJSON_AddStringToObject(pArray_relay, "content", content);        // 添加字符串类型数据到子叶结构体
    char *sendData = cJSON_Print(pRoot);                        // 从cJSON对象中获取有格式的JSON对象
    printf("data:%s\n", sendData);                            // 打印数据

    esp_http_client_config_t config = {
        .url = url,
        .method = HTTP_METHOD_POST,
        .event_handler = response_handler,
        .buffer_size = MAX_HTTP_RECV_BUFFER,
        .timeout_ms = 30000,
    };
 
    // Set the headers
    esp_http_client_handle_t client = esp_http_client_init(&config);
    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_header(client, "Accept", "application/json");
 
    esp_http_client_set_post_field(client, sendData, strlen(sendData));

    // Send the request
    esp_err_t err = esp_http_client_perform(client);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "HTTP POST request failed: %s\n", esp_err_to_name(err));
    }
 
    // Clean up client
    esp_http_client_cleanup(client);
 
    cJSON_free((void *) sendData);                             // 释放cJSON_Print ()分配出来的内存空间
    cJSON_Delete(pRoot);                                       // 释放cJSON_CreateObject ()分配出来的内存空间

    // Return error code
    return err;
}

esp_err_t create_speech_request(const char *content, const int len)
{
    const char* send_data = (const char*)(char*)content;
    char url[184] = "http://vop.baidu.com/server_api?dev_pid=1537&cuid=12345&token=";
    strcat(url, speech_token);
    printf(url);printf("\n");
    
    esp_http_client_config_t config = {
        .url = url,
        .method = HTTP_METHOD_POST,
        .event_handler = response_handler,
        .buffer_size = MAX_HTTP_RECV_BUFFER,
        .timeout_ms = 30000,
    };
 
    // Set the headers
    esp_http_client_handle_t client = esp_http_client_init(&config);
    esp_http_client_set_header(client, "Content-Type", "audio/pcm;rate=16000");
 
    esp_http_client_set_post_field(client, send_data, len);

    // Send the request
    esp_err_t err = esp_http_client_perform(client);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "HTTP POST request failed: %s\n", esp_err_to_name(err));
    }
 
    // Clean up client
    esp_http_client_cleanup(client);

    // Return error code
    return err;
}

static void parse_music_response (const char *data)
{
    cJSON* cjson_test = NULL;
    cJSON* cjson_music_data = NULL;
    cJSON* cjson_music_arr = NULL;
    cJSON* cjson_music_content = NULL;
    cjson_test = cJSON_Parse(data);
    if(cjson_test == NULL)
    {
        printf("parse fail.\n");
        return;
    }
    printf("here\n");
 
    /* Parsing Chat GPT response*/
    cjson_music_data = cJSON_GetObjectItem(cjson_test, "data");
    if (cjson_music_data){//获取音乐url
        cjson_music_arr = cJSON_GetArrayItem(cjson_music_data, 0);//得到数组0
        cjson_music_content = cJSON_GetObjectItem(cjson_music_arr, "url");//得到数组0中的url
        if(cjson_music_content)//获取url成功
        {
            //strcpy(speech_message_content,cjson_music_url->valuestring);
            ESP_LOGI(TAG, "cjson_music_url phased!\n");
            ESP_LOGI(TAG, "cjson_music_url::: %s\n", cjson_music_content->valuestring);
            memset(music_url,'\0',sizeof(music_url));
            strcpy(music_url,cjson_music_content->valuestring);
        }else{
            ESP_LOGI(TAG, "get cjson_music_url failed!\n");
        }
    }else{
        cjson_music_data = cJSON_GetObjectItem(cjson_test, "songs");//获取音乐列表
        if(cjson_music_data)
        {
            cjson_music_arr = cJSON_GetArrayItem(cjson_music_data, 0);//得到数组0
            cjson_music_content = cJSON_GetObjectItem(cjson_music_arr, "id");//得到数组0中的id
            if(cjson_music_content)
            {
                ESP_LOGI(TAG, "cjson_music_id phased!\n");
                ESP_LOGI(TAG, "cjson_music_id::: %d\n", cjson_music_content->valueint);
                music_id = cjson_music_content->valueint;
            }else{
                ESP_LOGI(TAG, "get cjson_music_id failed!\n");
            }
        }
    }
    cJSON_Delete(cjson_test);
}

static esp_err_t music_response_handler(esp_http_client_event_t *evt)
{
    static char *data = NULL; // Initialize data to NULL
    static int data_len = 0; // Initialize data to NULL
 
    switch (evt->event_id) {
    case HTTP_EVENT_ERROR:
        ESP_LOGI(TAG, "HTTP_EVENT_ERROR");
        break;
 
    case HTTP_EVENT_ON_DATA:
        ESP_LOGI(TAG, "HTTP_EVENT_ON_DATA (%d +)%d\n", data_len, evt->data_len);
        ESP_LOGI(TAG, "Raw Response: data length: (%d +)%d: %.*s\n", data_len, evt->data_len, evt->data_len, (char *)evt->data);
        
        // Allocate memory for the incoming data        
        data = heap_caps_realloc(data, data_len + evt->data_len + 1,  MALLOC_CAP_8BIT);
        if (data == NULL) {
                ESP_LOGE(TAG, "data realloc failed");
            free(data);
            data = NULL;
            break;
        }
        memcpy(data + data_len, (char *)evt->data, evt->data_len);
        data_len += evt->data_len;
        data[data_len] = '\0';
        break;
 
    case HTTP_EVENT_ON_FINISH:
        ESP_LOGI(TAG, "HTTP_EVENT_ON_FINISH");
        if (data != NULL) {
            // Process the raw data
            parse_music_response(data);
            // Free memory
            free(data); 
            data = NULL;
            data_len = 0;
        }else{
            ESP_LOGI(TAG, "data is NULL");
        }
        break;

    default:
        break;
    }
    return ESP_OK;
}

esp_err_t get_music_url(void)
{
    char url[85] = {0};
    sprintf(url, "http://music.zxfeng.asia/song/url?id=%lld", music_id);
    // strcat(url, music_id);
    printf(url);printf("\n");
    
    esp_http_client_config_t config = {
        .url = url,
        .method = HTTP_METHOD_GET,
        .event_handler = music_response_handler,
        .buffer_size = MAX_HTTP_RECV_BUFFER,
        .timeout_ms = 30000,
    };
 
    // Set the headers
    esp_http_client_handle_t client = esp_http_client_init(&config);
    esp_http_client_set_header(client, "cookie", cookie);

    // Send the request
    esp_err_t err = esp_http_client_perform(client);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "HTTP GET request failed: %s\n", esp_err_to_name(err));
    }
 
    // Clean up client
    esp_http_client_cleanup(client);

    // Return error code
    return err;  
}

esp_err_t get_music_list(void)
{
    char url[85] = {0};
    // strcpy(list_id,"2913404873");
    int min_number = 1;
    int max_number = 30;
    srand(time(NULL));
    sprintf(url, "http://music.zxfeng.asia/playlist/track/all?id=%s&limit=1&offset=%d", "2913404873", (rand() % (max_number - min_number + 1)) + min_number);
    printf(url);printf("\n");
    
    esp_http_client_config_t config = {
        .url = url,
        .method = HTTP_METHOD_GET,
        .event_handler = music_response_handler,
        .buffer_size = MAX_HTTP_RECV_BUFFER,
        .timeout_ms = 30000,
    };
 
    // Set the headers
    esp_http_client_handle_t client = esp_http_client_init(&config);
    esp_http_client_set_header(client, "cookie", cookie);

    // Send the request
    esp_err_t err = esp_http_client_perform(client);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "HTTP GET request failed: %s\n", esp_err_to_name(err));
    }
 
    // Clean up client
    esp_http_client_cleanup(client);

    // Return error code
    return err;
}

static void process_task(void *arg)
{
    int msg = 0;

    while (true) {
        ESP_LOGI(TAG, "Waiting for message");
        if (xQueueReceive(process_rec_q, &msg, portMAX_DELAY) == pdTRUE) {
            switch (msg) {
                case SPEECH_GET: {

                    ESP_LOGW(TAG, "Speech get");
                    ESP_LOGI(TAG, "speech_message_content %s\n", speech_message_content);
                    if (strlen(speech_message_content) == 0) {
                            ESP_LOGI(TAG, "speech_message_content is NULL");
                            break;
                    }else if(strcmp(speech_message_content,"播放音乐。") == 0)
                    {
                            get_music_list();
                            vTaskDelay(1000 / portTICK_PERIOD_MS);
                            get_music_url();
                            vTaskDelay(1000 / portTICK_PERIOD_MS);
                            if(baidu_tts_task_handle != NULL)
                            {
                                ESP_LOGI(TAG, "baidu_tts_task_handle is not NULL");
                                xEventGroupClearBits(task_event_group, TASK_RUN_BIT);
                            }
                            vTaskDelay(5000 / portTICK_PERIOD_MS);
                            xEventGroupSetBits(task_event_group, TASK_RUN_BIT);
                            xTaskCreate(http_mp3_play, "http_music", 1024*4, &music_url, 5, &http_mp3_task_handle);
                    }else{
                        if(http_mp3_task_handle != NULL)
                        {
                            ESP_LOGI(TAG, "http_mp3_task_handle is not NULL");
                            xEventGroupClearBits(task_event_group, TASK_RUN_BIT);
                        }
                        vTaskDelay(5000 / portTICK_PERIOD_MS);
                        create_wenxin_request((const char*)speech_message_content);
                    }
                    break;
                }
                case QIANFAN_GET: {
                    ESP_LOGW(TAG, "Qianfan get");
                    ESP_LOGI(TAG, "qianfan_message_content %s\n", qianfan_message_content);
                    xEventGroupSetBits(task_event_group, TASK_RUN_BIT);
                    xTaskCreate(baidu_tts, "baidu_tts", 1024*4, &music_url, 5, &baidu_tts_task_handle);
                    //tts_play((const char*)qianfan_message_content);
                    break;
                }
                default:
                    ESP_LOGE(TAG, "Unknown message type: %d", msg);
                    break;
            }
        }
    }
    vTaskDelete(NULL);    
}

void process_task_start(void)
{
    task_event_group = xEventGroupCreate();
    process_rec_q = xQueueCreate(2, sizeof(int));
    xTaskCreate(process_task, "process_task", 4096, NULL, 5, NULL);
}