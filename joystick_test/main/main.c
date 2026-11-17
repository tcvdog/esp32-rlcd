#include <stdio.h>
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_log.h"

// Waveshare ESP32-S3-RLCD-4.2 P1 2x8 header, direct-wired X1K4 joystick.
// X/Y need ADC. The five switches use GPIOs that are otherwise free on the header.
#define JOY_X_GPIO 1
#define JOY_Y_GPIO 2
#define JOY_SW_GPIO 0     // header pin 5 (also BOOT, do not hold during reset)
#define K1_GPIO 13        // header pin 14 (SDA pad, used as plain GPIO here)
#define K2_GPIO 14        // header pin 16 (SCL pad, used as plain GPIO here)
#define K3_GPIO 17        // header pin 13
#define K4_GPIO 18        // header pin 15 (also onboard KEY)

static const char *TAG = "JOYTEST";

static void config_input(gpio_num_t pin) {
    gpio_config_t io = {
        .pin_bit_mask = 1ULL << pin,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&io));
}

void app_main(void) {
    ESP_LOGI(TAG, "X1K4 joystick test on ESP32-S3-RLCD-4.2");
    ESP_LOGI(TAG, "X=GPIO%d Y=GPIO%d SW=GPIO%d K1=%d K2=%d K3=%d K4=%d",
             JOY_X_GPIO, JOY_Y_GPIO, JOY_SW_GPIO, K1_GPIO, K2_GPIO, K3_GPIO, K4_GPIO);

    config_input(JOY_SW_GPIO);
    config_input(K1_GPIO);
    config_input(K2_GPIO);
    config_input(K3_GPIO);
    config_input(K4_GPIO);

    adc_oneshot_unit_handle_t adc1;
    adc_oneshot_unit_init_cfg_t init_cfg = { .unit_id = ADC_UNIT_1 };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&init_cfg, &adc1));

    adc_oneshot_chan_cfg_t chan_cfg = {
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_12,
    };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc1, ADC_CHANNEL_0, &chan_cfg));
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc1, ADC_CHANNEL_1, &chan_cfg));

    while (true) {
        int x = 0, y = 0;
        ESP_ERROR_CHECK(adc_oneshot_read(adc1, ADC_CHANNEL_0, &x));
        ESP_ERROR_CHECK(adc_oneshot_read(adc1, ADC_CHANNEL_1, &y));

        // Switches are assumed active-low to GND. If your module is active-high,
        // invert these levels after checking it with a multimeter.
        int sw = !gpio_get_level(JOY_SW_GPIO);
        int k1 = !gpio_get_level(K1_GPIO);
        int k2 = !gpio_get_level(K2_GPIO);
        int k3 = !gpio_get_level(K3_GPIO);
        int k4 = !gpio_get_level(K4_GPIO);

        const char *dir = "CENTER";
        if (x < 1200) dir = "LEFT";
        else if (x > 2800) dir = "RIGHT";

        const char *vert = "MID";
        if (y < 1200) vert = "UP";
        else if (y > 2800) vert = "DOWN";

        printf("X:%4d(%s) Y:%4d(%s) SW:%d K1:%d K2:%d K3:%d K4:%d\n",
               x, dir, y, vert, sw, k1, k2, k3, k4);
        fflush(stdout);
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}
