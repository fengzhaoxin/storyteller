#ifndef _C_HTTP_H_
#define _C_HTTP_H_

#define TASK_RUN_BIT BIT0
extern EventGroupHandle_t task_event_group;

extern TaskHandle_t http_mp3_task_handle;
extern TaskHandle_t baidu_tts_task_handle;

extern char music_url[200];
extern char *speech_token;
extern char qianfan_message_content[2048];

int baidu_get_token(void);
esp_err_t create_wenxin_request(const char *content);
esp_err_t create_speech_request(const char *content, const int len);

esp_err_t get_music_url(void);
esp_err_t get_music_list(void);

void process_task_start(void);
#endif // _C_HTTP_H_