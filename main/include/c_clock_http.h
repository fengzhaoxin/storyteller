#ifndef _C_CLOCK_HTTP_H_
#define _C_CLOCK_HTTP_H_

esp_err_t esp_start_webserver(void);
void esp_receive_broadcast(void *args);
void notice(void *args);

#endif