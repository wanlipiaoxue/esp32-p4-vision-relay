#include <math.h>
#include <stdint.h>
#include <stdio.h>

#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "driver/i2s_std.h"
#include "esp_check.h"
#include "esp_codec_dev.h"
#include "esp_codec_dev_defaults.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define TEST_I2C_PORT              I2C_NUM_0
#define TEST_I2C_SDA_GPIO          GPIO_NUM_7
#define TEST_I2C_SCL_GPIO          GPIO_NUM_8

#define TEST_I2S_PORT              I2S_NUM_0
#define TEST_I2S_MCLK_GPIO         GPIO_NUM_13
#define TEST_I2S_BCLK_GPIO         GPIO_NUM_12
#define TEST_I2S_WS_GPIO           GPIO_NUM_10
#define TEST_I2S_DOUT_GPIO         GPIO_NUM_9
#define TEST_I2S_DIN_GPIO          GPIO_NUM_11
#define TEST_PA_GPIO               GPIO_NUM_53

#define TEST_SAMPLE_RATE_HZ        16000
#define TEST_MCLK_MULTIPLE         384
#define TEST_CHANNELS              2
#define TEST_TONE_FREQUENCY_HZ     440.0f
#define TEST_TONE_MS               600
#define TEST_GAP_MS                400
#define TEST_FRAME_CHUNK           256
#define TEST_MAX_AMPLITUDE         26000.0f

static const char *TAG = "speaker_test";
static i2s_chan_handle_t tx_handle;
static i2s_chan_handle_t rx_handle;
static esp_codec_dev_handle_t codec_handle;

static esp_err_t init_i2s(void)
{
    i2s_chan_config_t channel_config =
        I2S_CHANNEL_DEFAULT_CONFIG(TEST_I2S_PORT, I2S_ROLE_MASTER);
    channel_config.auto_clear = true;
    ESP_RETURN_ON_ERROR(i2s_new_channel(&channel_config, &tx_handle, &rx_handle),
                        TAG, "create I2S channels failed");

    i2s_std_config_t standard_config = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(TEST_SAMPLE_RATE_HZ),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(
            I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = TEST_I2S_MCLK_GPIO,
            .bclk = TEST_I2S_BCLK_GPIO,
            .ws = TEST_I2S_WS_GPIO,
            .dout = TEST_I2S_DOUT_GPIO,
            .din = TEST_I2S_DIN_GPIO,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv = false,
            },
        },
    };
    standard_config.clk_cfg.mclk_multiple = TEST_MCLK_MULTIPLE;

    ESP_RETURN_ON_ERROR(i2s_channel_init_std_mode(tx_handle, &standard_config),
                        TAG, "init I2S TX failed");
    ESP_RETURN_ON_ERROR(i2s_channel_init_std_mode(rx_handle, &standard_config),
                        TAG, "init I2S RX failed");
    ESP_RETURN_ON_ERROR(i2s_channel_enable(tx_handle), TAG, "enable I2S TX failed");
    ESP_RETURN_ON_ERROR(i2s_channel_enable(rx_handle), TAG, "enable I2S RX failed");
    return ESP_OK;
}

static esp_err_t init_codec(void)
{
    i2c_master_bus_handle_t i2c_bus = NULL;
    const i2c_master_bus_config_t i2c_config = {
        .i2c_port = TEST_I2C_PORT,
        .sda_io_num = TEST_I2C_SDA_GPIO,
        .scl_io_num = TEST_I2C_SCL_GPIO,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    ESP_RETURN_ON_ERROR(i2c_new_master_bus(&i2c_config, &i2c_bus),
                        TAG, "create I2C bus failed");

    audio_codec_i2c_cfg_t i2c_codec_config = {
        .port = TEST_I2C_PORT,
        .addr = ES8311_CODEC_DEFAULT_ADDR,
        .bus_handle = i2c_bus,
    };
    const audio_codec_ctrl_if_t *ctrl_if =
        audio_codec_new_i2c_ctrl(&i2c_codec_config);
    ESP_RETURN_ON_FALSE(ctrl_if, ESP_FAIL, TAG, "create ES8311 I2C control failed");

    audio_codec_i2s_cfg_t i2s_codec_config = {
        .port = TEST_I2S_PORT,
        .rx_handle = rx_handle,
        .tx_handle = tx_handle,
    };
    const audio_codec_data_if_t *data_if =
        audio_codec_new_i2s_data(&i2s_codec_config);
    ESP_RETURN_ON_FALSE(data_if, ESP_FAIL, TAG, "create ES8311 I2S data interface failed");

    const audio_codec_gpio_if_t *gpio_if = audio_codec_new_gpio();
    ESP_RETURN_ON_FALSE(gpio_if, ESP_FAIL, TAG, "create codec GPIO interface failed");

    es8311_codec_cfg_t codec_config = {
        .ctrl_if = ctrl_if,
        .gpio_if = gpio_if,
        .codec_mode = ESP_CODEC_DEV_WORK_MODE_BOTH,
        .master_mode = false,
        .use_mclk = true,
        .pa_pin = TEST_PA_GPIO,
        .pa_reverted = false,
        .hw_gain = {
            .pa_voltage = 5.0f,
            .codec_dac_voltage = 3.3f,
        },
        .mclk_div = TEST_MCLK_MULTIPLE,
    };
    const audio_codec_if_t *codec_if = es8311_codec_new(&codec_config);
    ESP_RETURN_ON_FALSE(codec_if, ESP_FAIL, TAG, "create ES8311 codec failed");

    esp_codec_dev_cfg_t device_config = {
        .dev_type = ESP_CODEC_DEV_TYPE_IN_OUT,
        .codec_if = codec_if,
        .data_if = data_if,
    };
    codec_handle = esp_codec_dev_new(&device_config);
    ESP_RETURN_ON_FALSE(codec_handle, ESP_FAIL, TAG, "create codec device failed");

    esp_codec_dev_sample_info_t sample_info = {
        .bits_per_sample = I2S_DATA_BIT_WIDTH_16BIT,
        .channel = TEST_CHANNELS,
        .channel_mask = 0x03,
        .sample_rate = TEST_SAMPLE_RATE_HZ,
    };
    const int open_result = esp_codec_dev_open(codec_handle, &sample_info);
    ESP_RETURN_ON_FALSE(open_result == ESP_CODEC_DEV_OK, ESP_FAIL,
                        TAG, "open ES8311 codec failed: %d", open_result);
    return ESP_OK;
}

static esp_err_t set_volume(int volume)
{
    const int result = esp_codec_dev_set_out_vol(codec_handle, volume);
    ESP_RETURN_ON_FALSE(result == ESP_CODEC_DEV_OK, ESP_FAIL,
                        TAG, "set volume %d failed: %d", volume, result);
    return ESP_OK;
}

static esp_err_t write_pcm_chunk(const int16_t *samples, size_t frames)
{
    const int result = esp_codec_dev_write(
        codec_handle, (void *)samples, frames * TEST_CHANNELS * sizeof(int16_t));
    ESP_RETURN_ON_FALSE(result == ESP_CODEC_DEV_OK, ESP_FAIL,
                        TAG, "audio write failed: %d", result);
    return ESP_OK;
}

static void play_tone(uint32_t duration_ms)
{
    int16_t samples[TEST_FRAME_CHUNK * TEST_CHANNELS];
    const size_t total_frames =
        (size_t)TEST_SAMPLE_RATE_HZ * duration_ms / 1000U;
    const float phase_step =
        2.0f * 3.14159265358979323846f * TEST_TONE_FREQUENCY_HZ /
        TEST_SAMPLE_RATE_HZ;
    float phase = 0.0f;

    for (size_t offset = 0; offset < total_frames; offset += TEST_FRAME_CHUNK) {
        size_t frames = total_frames - offset;
        if (frames > TEST_FRAME_CHUNK) {
            frames = TEST_FRAME_CHUNK;
        }

        for (size_t i = 0; i < frames; ++i) {
            const int16_t value = (int16_t)(sinf(phase) * TEST_MAX_AMPLITUDE);
            samples[i * TEST_CHANNELS] = value;
            samples[i * TEST_CHANNELS + 1] = value;
            phase += phase_step;
            if (phase >= 2.0f * 3.14159265358979323846f) {
                phase -= 2.0f * 3.14159265358979323846f;
            }
        }
        ESP_ERROR_CHECK(write_pcm_chunk(samples, frames));
    }
}

static void play_silence(uint32_t duration_ms)
{
    int16_t samples[TEST_FRAME_CHUNK * TEST_CHANNELS] = {0};
    const size_t total_frames =
        (size_t)TEST_SAMPLE_RATE_HZ * duration_ms / 1000U;

    for (size_t offset = 0; offset < total_frames; offset += TEST_FRAME_CHUNK) {
        size_t frames = total_frames - offset;
        if (frames > TEST_FRAME_CHUNK) {
            frames = TEST_FRAME_CHUNK;
        }
        ESP_ERROR_CHECK(write_pcm_chunk(samples, frames));
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "Initializing ES8311 speaker path");
    ESP_ERROR_CHECK(init_i2s());
    ESP_ERROR_CHECK(init_codec());
    ESP_LOGI(TAG, "MAX_VOLUME_TEST level=100 percent duration=10000 ms frequency=440 Hz");
    ESP_ERROR_CHECK(set_volume(100));
    play_tone(10000);
    play_silence(TEST_GAP_MS);
    ESP_ERROR_CHECK(set_volume(0));
    ESP_LOGI(TAG, "MAX_VOLUME_TEST_COMPLETE: final volume muted");
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
