#include "buttons.h"
#include "../menu/menu.h"

#include "iot_button.h"
#include "esp_log.h"
#include "driver/adc.h"

#define BSP_BUTTON_NUM 5
#define BSP_BUTTON_MENU 0
#define BSP_BUTTON_PLAY 1
#define BSP_BUTTON_DOWN 2
#define BSP_BUTTON_UP   3
#define BSP_BUTTON_BOOT_IO 0

static const char *TAG = "ssi_esp";

static const button_config_t bsp_button_config[BSP_BUTTON_NUM] = {
    {
        .type = BUTTON_TYPE_ADC,
        .adc_button_config.adc_channel = ADC_CHANNEL_0,
        .adc_button_config.button_index = BSP_BUTTON_MENU,
        .adc_button_config.min = 2310,
        .adc_button_config.max = 2510
    },
    {
        .type = BUTTON_TYPE_ADC,
        .adc_button_config.adc_channel = ADC_CHANNEL_0,
        .adc_button_config.button_index = BSP_BUTTON_PLAY,
        .adc_button_config.min = 1880,
        .adc_button_config.max = 2080
    },
    {
        .type = BUTTON_TYPE_ADC,
        .adc_button_config.adc_channel = ADC_CHANNEL_0,
        .adc_button_config.button_index = BSP_BUTTON_DOWN,
        .adc_button_config.min = 720,
        .adc_button_config.max = 920
    },
    {
        .type = BUTTON_TYPE_ADC,
        .adc_button_config.adc_channel = ADC_CHANNEL_0,
        .adc_button_config.button_index = BSP_BUTTON_UP,
        .adc_button_config.min = 280,
        .adc_button_config.max = 480
    },
    {
        .type = BUTTON_TYPE_GPIO,
        .gpio_button_config.active_level = 0,
        .gpio_button_config.gpio_num = BSP_BUTTON_BOOT_IO
    }
};

// Callbacks
static void button_cb_menu(void *arg, void *data)
{
    handle_button(1);
    ESP_LOGI(TAG, "Menu pressed");
}

static void button_cb_play(void *arg, void *data)
{
    handle_button(2);
    ESP_LOGI(TAG, "Play pressed");
}

static void button_cb_up(void *arg, void *data)
{
    handle_button(3);
    ESP_LOGI(TAG, "Up pressed");
}

static void button_cb_down(void *arg, void *data)
{
    handle_button(4);
    ESP_LOGI(TAG, "Down pressed");
}

void buttons_init(void)
{
    button_handle_t btn_menu = iot_button_create(&bsp_button_config[0]);
    button_handle_t btn_play = iot_button_create(&bsp_button_config[1]);
    button_handle_t btn_down = iot_button_create(&bsp_button_config[2]);
    button_handle_t btn_up   = iot_button_create(&bsp_button_config[3]);

    iot_button_register_cb(btn_menu, BUTTON_SINGLE_CLICK, button_cb_menu, NULL);
    iot_button_register_cb(btn_play, BUTTON_SINGLE_CLICK, button_cb_play, NULL);
    iot_button_register_cb(btn_down, BUTTON_SINGLE_CLICK, button_cb_down, NULL);
    iot_button_register_cb(btn_up,   BUTTON_SINGLE_CLICK, button_cb_up, NULL);
}