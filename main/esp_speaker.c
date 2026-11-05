#include <math.h>

#include "driver/i2s_std.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// MAX98357A braucht nur die drei I2S-Leitungen: kein MCLK, kein I2C.
// Der SD-Pin bestimmt den Kanal: offen oder ueber 100k nach VDD -> (L+R)/2,
// nach GND -> Verstaerker aus.
#define PIN_BCLK GPIO_NUM_5
#define PIN_LRC  GPIO_NUM_6
#define PIN_DIN  GPIO_NUM_7

#define SAMPLE_RATE_HZ 48000
#define TONE_HZ        1000
#define TONE_AMPLITUDE 8000

// 48000 / 1000 ist ganzzahlig, damit passt genau eine Periode in den Puffer
// und er laesst sich bruchlos wiederholen.
#define PERIOD_SAMPLES (SAMPLE_RATE_HZ / TONE_HZ)

static const char *TAG = "esp_speaker";

static i2s_chan_handle_t s_tx_chan;

static void speaker_init(void)
{
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_AUTO, I2S_ROLE_MASTER);
    ESP_ERROR_CHECK(i2s_new_channel(&chan_cfg, &s_tx_chan, NULL));

    i2s_std_config_t std_cfg = {
        .clk_cfg  = I2S_STD_CLK_DEFAULT_CONFIG(SAMPLE_RATE_HZ),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT,
                                                        I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = PIN_BCLK,
            .ws   = PIN_LRC,
            .dout = PIN_DIN,
            .din  = I2S_GPIO_UNUSED,
        },
    };
    ESP_ERROR_CHECK(i2s_channel_init_std_mode(s_tx_chan, &std_cfg));
    ESP_ERROR_CHECK(i2s_channel_enable(s_tx_chan));
}

// Eine Periode Sinus, auf beide Slots gelegt. Welchen der MAX98357A nimmt,
// haengt am SD-Pin, deshalb liegt das Signal auf links und rechts.
static void fill_tone(int16_t buf[PERIOD_SAMPLES][2])
{
    for (int i = 0; i < PERIOD_SAMPLES; i++) {
        float phase = 2.0f * (float)M_PI * (float)i / (float)PERIOD_SAMPLES;
        int16_t sample = (int16_t)(TONE_AMPLITUDE * sinf(phase));
        buf[i][0] = sample;
        buf[i][1] = sample;
    }
}

void app_main(void)
{
    static int16_t tone[PERIOD_SAMPLES][2];

    speaker_init();
    fill_tone(tone);
    ESP_LOGI(TAG, "I2S laeuft: %d Hz, Testton %d Hz", SAMPLE_RATE_HZ, TONE_HZ);

    while (true) {
        size_t written = 0;
        ESP_ERROR_CHECK(i2s_channel_write(s_tx_chan, tone, sizeof(tone),
                                          &written, portMAX_DELAY));
    }
}
