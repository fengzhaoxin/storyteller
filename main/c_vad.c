#include "amrnb_encoder.h"
#include "amrwb_encoder.h"
#include "audio_element.h"
#include "audio_idf_version.h"
#include "audio_mem.h"
#include "audio_pipeline.h"
#include "audio_recorder.h"
#include "audio_thread.h"
#include "esp_audio.h"
#include "filter_resample.h"
#include "i2s_stream.h"
#include "raw_stream.h"
#include "recorder_encoder.h"
#include "recorder_sr.h"

#include "audio_event_iface.h"
#include "audio_common.h"
#include "tts_stream.h"

#include "board.h"
#include "common.h"
#include "c_http.h"
#include "c_vad.h"

#define NO_ENCODER  (0)
#define ENC_2_AMRNB (1)
#define ENC_2_AMRWB (2)

#define RECORDER_ENC_ENABLE (NO_ENCODER)
#define WAKENET_ENABLE      (true)
#define MULTINET_ENABLE     (false)

#ifndef CODEC_ADC_SAMPLE_RATE
#warning "Please define CODEC_ADC_SAMPLE_RATE first, default value is 48kHz may not correctly"
#define CODEC_ADC_SAMPLE_RATE    16000
#endif

#ifndef CODEC_ADC_BITS_PER_SAMPLE
#warning "Please define CODEC_ADC_BITS_PER_SAMPLE first, default value 16 bits may not correctly"
#define CODEC_ADC_BITS_PER_SAMPLE  I2S_BITS_PER_SAMPLE_16BIT
#endif

#ifndef RECORD_HARDWARE_AEC
#warning "The hardware AEC is disabled!"
#define RECORD_HARDWARE_AEC  (true)
#endif

#ifndef CODEC_ADC_I2S_PORT
#define CODEC_ADC_I2S_PORT  (0)
#endif

enum _rec_msg_id {
    REC_START = 1,
    REC_STOP,
    REC_CANCEL,
};

static char *TAG = "vad";

EXT_RAM_ATTR static audio_pipeline_handle_t pipeline_rec =NULL;
EXT_RAM_ATTR static audio_rec_handle_t     recorder      = NULL;
EXT_RAM_ATTR static audio_element_handle_t raw_read      = NULL;
EXT_RAM_ATTR static QueueHandle_t          rec_q         = NULL;
EXT_RAM_ATTR static bool                   voice_reading = false;

static void voice_read_task(void *args)
{
    int msg = 0;
    const int buf_len =8 * 1024;
    char *buffer = (char *)malloc(96 * 1024);
    if (NULL == buffer) 
    {
        ESP_LOGE(TAG, "Memory allocation failed!");
        return;
    }
    memset(buffer, 0, 96 * 1024);
    TickType_t delay = portMAX_DELAY;

    while (true) {
        //memset(buffer, 0, 96 * 1024);
        if (xQueueReceive(rec_q, &msg, delay) == pdTRUE) {
            switch (msg) {
                case REC_START: {
                    ESP_LOGW(TAG, "voice read begin");
                    delay = 0;
                    voice_reading = true;
                    break;
                }
                case REC_STOP: {
                    ESP_LOGW(TAG, "voice read stopped");
                        ESP_LOGI(TAG, "Speech start success");
                        create_speech_request(buffer, 96 * 1024);
                    delay = portMAX_DELAY;
                    voice_reading = false;
                    break;
                }
                case REC_CANCEL: {
                    ESP_LOGW(TAG, "voice read cancel");
                    delay = portMAX_DELAY;
                    voice_reading = false;
                    break;
                }
                default:
                    break;
            }
        }
        int ret = 0;
        if (voice_reading) {
            for(size_t i = 0; i < 12; i++)
            {
                ret = audio_recorder_data_read(recorder, (char *)buffer + i * 8 * 1024, buf_len, portMAX_DELAY);
                if (ret <= 0) {
                    ESP_LOGW(TAG, "audio recorder read finished %d", ret);
                    delay = portMAX_DELAY;
                    voice_reading = false;
                    break;
                }
            }
        }else{
            memset(buffer, 0, 96 * 1024);
        }
    }

    free(buffer);
    ESP_LOGI(TAG, "voice read task delete");
    vTaskDelete(NULL);
}

static esp_err_t rec_engine_cb(audio_rec_evt_t type, void *user_data)
{
    if (AUDIO_REC_WAKEUP_START == type) {
        ESP_LOGI(TAG, "rec_engine_cb - REC_EVENT_WAKEUP_START");
        gpio_set_level(GPIO_OUTPUT_IO_1, 1);
        if (voice_reading) {
            int msg = REC_CANCEL;
            if (xQueueSend(rec_q, &msg, 0) != pdPASS) {
                ESP_LOGE(TAG, "rec cancel send failed");
            }
        }
    } else if (AUDIO_REC_VAD_START == type) {
        ESP_LOGI(TAG, "rec_engine_cb - REC_EVENT_VAD_START");
        if (!voice_reading) {
            int msg = REC_START;
            if (xQueueSend(rec_q, &msg, 0) != pdPASS) {
                ESP_LOGE(TAG, "rec start send failed");
            }
        }
    } else if (AUDIO_REC_VAD_END == type) {
        ESP_LOGI(TAG, "rec_engine_cb - REC_EVENT_VAD_STOP");
        if (voice_reading) {
            int msg = REC_STOP;
            if (xQueueSend(rec_q, &msg, 0) != pdPASS) {
                ESP_LOGE(TAG, "rec stop send failed");
            }
        }

    } else if (AUDIO_REC_WAKEUP_END == type) {
        ESP_LOGI(TAG, "rec_engine_cb - REC_EVENT_WAKEUP_END");
        gpio_set_level(GPIO_OUTPUT_IO_1, 0);
    } else if (AUDIO_REC_COMMAND_DECT <= type) {
        ESP_LOGI(TAG, "rec_engine_cb - AUDIO_REC_COMMAND_DECT");
    } else {
        ESP_LOGE(TAG, "Unkown event");
    }
    return ESP_OK;
}

static int input_cb_for_afe(int16_t *buffer, int buf_sz, void *user_ctx, TickType_t ticks)
{
    return raw_stream_read(raw_read, (char *)buffer, buf_sz);
}

static void start_recorder()
{
    audio_element_handle_t i2s_stream_reader;
    audio_pipeline_cfg_t pipeline_cfg = DEFAULT_AUDIO_PIPELINE_CONFIG();
    pipeline_rec = audio_pipeline_init(&pipeline_cfg);
    if (NULL == pipeline_rec) {
        return;
    }

    i2s_stream_cfg_t i2s_cfg = I2S_STREAM_CFG_DEFAULT();
    i2s_cfg.i2s_port = CODEC_ADC_I2S_PORT;
    i2s_cfg.i2s_config.use_apll = 0;
    i2s_cfg.i2s_config.sample_rate = CODEC_ADC_SAMPLE_RATE;
    i2s_cfg.i2s_config.bits_per_sample = CODEC_ADC_BITS_PER_SAMPLE;
    i2s_cfg.type = AUDIO_STREAM_READER;
    i2s_stream_reader = i2s_stream_init(&i2s_cfg);

    audio_element_handle_t filter = NULL;
#if CODEC_ADC_SAMPLE_RATE != (16000)
    rsp_filter_cfg_t rsp_cfg = DEFAULT_RESAMPLE_FILTER_CONFIG();
    rsp_cfg.src_rate = CODEC_ADC_SAMPLE_RATE;
    rsp_cfg.dest_rate = 16000;
    filter = rsp_filter_init(&rsp_cfg);
#endif

    raw_stream_cfg_t raw_cfg = RAW_STREAM_CFG_DEFAULT();
    raw_cfg.type = AUDIO_STREAM_READER;
    raw_read = raw_stream_init(&raw_cfg);

    audio_pipeline_register(pipeline_rec, i2s_stream_reader, "i2s");
    audio_pipeline_register(pipeline_rec, raw_read, "raw");

    if (filter) {
        audio_pipeline_register(pipeline_rec, filter, "filter");
        const char *link_tag[3] = {"i2s", "filter", "raw"};
        audio_pipeline_link(pipeline_rec, &link_tag[0], 3);
    } else {
        const char *link_tag[2] = {"i2s", "raw"};
        audio_pipeline_link(pipeline_rec, &link_tag[0], 2);
    }

    audio_pipeline_run(pipeline_rec);
    ESP_LOGI(TAG, "Recorder has been created");

    recorder_sr_cfg_t recorder_sr_cfg = DEFAULT_RECORDER_SR_CFG();
    recorder_sr_cfg.afe_cfg.memory_alloc_mode = AFE_MEMORY_ALLOC_MORE_PSRAM;
    recorder_sr_cfg.afe_cfg.wakenet_init = WAKENET_ENABLE;
    // recorder_sr_cfg.feed_task_core=0;
    // recorder_sr_cfg.fetch_task_core=0;
    recorder_sr_cfg.multinet_init = false;
    recorder_sr_cfg.afe_cfg.aec_init = RECORD_HARDWARE_AEC;
    recorder_sr_cfg.afe_cfg.agc_mode = AFE_MN_PEAK_NO_AGC;

    audio_rec_cfg_t cfg = AUDIO_RECORDER_DEFAULT_CFG();
    cfg.read = (recorder_data_read_t)&input_cb_for_afe;
    cfg.sr_handle = recorder_sr_create(&recorder_sr_cfg, &cfg.sr_iface);
    cfg.event_cb = rec_engine_cb;
    cfg.vad_off = 500;
    recorder = audio_recorder_create(&cfg);
}

void vad_start(void)
{
    audio_board_handle_t board_handle = audio_board_init();
    audio_hal_ctrl_codec(board_handle->audio_hal, AUDIO_HAL_CODEC_MODE_DECODE, AUDIO_HAL_CTRL_START);
    start_recorder();
    rec_q = xQueueCreate(3, sizeof(int));
    audio_thread_create(NULL, "read_task", voice_read_task, NULL, 4 * 1024, 5, true, 0);
}