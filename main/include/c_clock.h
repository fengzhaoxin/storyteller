#ifndef _C_CLOCK_H_
#define _C_CLOCK_H_

#include <time.h>
#include <sys/time.h>
#include "esp_sntp.h"

__uint8_t obtain_time(void);
void set_time(void);

#endif