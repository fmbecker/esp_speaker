#include <inttypes.h>
#include <math.h>
#include <stdbool.h>

#include "driver/i2s_std.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "usb_device_uac.h"

// MAX98357A braucht nur die drei I2S-Leitungen: kein MCLK, kein I2C.
// Der SD-Pin bestimmt den Kanal: offen oder ueber 100k nach VDD -> (L+R)/2,
// nach GND -> Verstaerker aus.
#define PIN_BCLK GPIO_NUM_5
#define PIN_LRC  GPIO_NUM_6
#define PIN_DIN  GPIO_NUM_7

static const char *TAG = "esp_speaker";

static i2s_chan_handle_t s_tx_chan;

// Vom Kontroll-Task geschrieben, vom SPK-Task gelesen.
static volatile float s_gain  = 1.0f;
static volatile bool  s_muted = false;

static void speaker_init(void)
{
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_AUTO, I2S_ROLE_MASTER);
    ESP_ERROR_CHECK(i2s_new_channel(&chan_cfg, &s_tx_chan, NULL));

    i2s_std_config_t std_cfg = {
        .clk_cfg  = I2S_STD_CLK_DEFAULT_CONFIG(CONFIG_UAC_SAMPLE_RATE),
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

// Der Host schickt 16-Bit-Stereo-Frames, die direkt dem I2S-Format
// entsprechen. Daempfung wird im Puffer selbst angewandt, bevor er
// weitergereicht wird.
static esp_err_t uac_output(uint8_t *buf, size_t len, void *cb_ctx)
{
    if (s_muted) {
        return ESP_OK;
    }

    float gain = s_gain;
    if (gain < 0.999f) {
        int16_t *samples = (int16_t *)buf;
        size_t count = len / sizeof(int16_t);
        for (size_t i = 0; i < count; i++) {
            samples[i] = (int16_t)(samples[i] * gain);
        }
    }

    size_t written = 0;
    return i2s_channel_write(s_tx_chan, buf, len, &written, pdMS_TO_TICKS(100));
}

// Die Komponente rechnet die dB-Angabe des Hosts in 0 bis 100 um.
// Zurueck auf dB und daraus den Amplitudenfaktor, damit der Regler
// sich nicht in der unteren Haelfte stauchen anfuehlt.
static void uac_set_volume(uint32_t volume, void *cb_ctx)
{
    float db = (float)volume / 2.0f - 50.0f;
    s_gain = (volume == 0) ? 0.0f : powf(10.0f, db / 20.0f);
    ESP_LOGI(TAG, "Lautstaerke %"PRIu32" (%.1f dB)", volume, db);
}

static void uac_set_mute(uint32_t mute, void *cb_ctx)
{
    s_muted = (mute != 0);
    ESP_LOGI(TAG, "Stumm: %s", s_muted ? "ja" : "nein");
}

void app_main(void)
{
    speaker_init();

    uac_device_config_t uac_cfg = {
        .output_cb        = uac_output,
        .input_cb         = NULL,
        .set_mute_cb      = uac_set_mute,
        .set_volume_cb    = uac_set_volume,
        .cb_ctx           = NULL,
        .skip_tinyusb_init = false,
    };
    ESP_ERROR_CHECK(uac_device_init(&uac_cfg));

    ESP_LOGI(TAG, "USB-Soundkarte bereit: %d Hz, 16 Bit, %d Kanaele",
             CONFIG_UAC_SAMPLE_RATE, CONFIG_UAC_SPEAKER_CHANNEL_NUM);
}
