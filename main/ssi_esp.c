#include <stdio.h>
#include <string.h>
#include "sdkconfig.h"
#include "bsp/esp-bsp.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "driver/spi_master.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_panel_ops.h"
#include "esp_camera.h"

#include "iot_button.h"

static const char *TAG = "ssi_esp";

static const button_config_t bsp_button_config[BSP_BUTTON_NUM] = {
    {
        .type = BUTTON_TYPE_ADC,
        .adc_button_config.adc_channel = ADC_CHANNEL_0, // ADC1 channel 0 is GPIO1
        .adc_button_config.button_index = BSP_BUTTON_MENU,
        .adc_button_config.min = 2310, // middle is 2410mV
        .adc_button_config.max = 2510
    },
    {
        .type = BUTTON_TYPE_ADC,
        .adc_button_config.adc_channel = ADC_CHANNEL_0, // ADC1 channel 0 is GPIO1
        .adc_button_config.button_index = BSP_BUTTON_PLAY,
        .adc_button_config.min = 1880, // middle is 1980mV
        .adc_button_config.max = 2080
    },
    {
        .type = BUTTON_TYPE_ADC,
        .adc_button_config.adc_channel = ADC_CHANNEL_0, // ADC1 channel 0 is GPIO1
        .adc_button_config.button_index = BSP_BUTTON_DOWN,
        .adc_button_config.min = 720, // middle is 820mV
        .adc_button_config.max = 920
    },
    {
        .type = BUTTON_TYPE_ADC,
        .adc_button_config.adc_channel = ADC_CHANNEL_0, // ADC1 channel 0 is GPIO1
        .adc_button_config.button_index = BSP_BUTTON_UP,
        .adc_button_config.min = 280, // middle is 380mV
        .adc_button_config.max = 480
    },
    {
        .type = BUTTON_TYPE_GPIO,
        .gpio_button_config.active_level = 0,
        .gpio_button_config.gpio_num = BSP_BUTTON_BOOT_IO
    }
};

// ---- Menu structure ----
typedef void (*PageFunction)(void);

typedef struct {
    const char *name;
    PageFunction func;
} MenuItem;

// ---- Page functions ----
void page_qr_generate(void);
void page_qr_scan(void);

// ---- Menu array ----
MenuItem main_menu[] = {
    {"QR Generate", page_qr_generate},
    {"QR Scan", page_qr_scan},
};

#define MENU_COUNT (sizeof(main_menu) / sizeof(main_menu[0]))

// ---- State ----
int current_index = 0;
bool in_menu = true;

void draw_menu(){
    bsp_display_lock(0);

    lv_obj_clean(lv_scr_act()); 

    for (int i = 0; i < MENU_COUNT; i++) {
        lv_obj_t *label = lv_label_create(lv_scr_act());
        char line[32];
        snprintf(line, sizeof(line), "%c %s", (i == current_index ? '>' : ' '), main_menu[i].name);
        lv_label_set_text(label, line);
        lv_obj_align(label, LV_ALIGN_TOP_LEFT, 10, i * 25);
    }

    bsp_display_unlock();
}

// ---- Button handler ----
void handle_button(int button)
{
    if (in_menu) {
        if (button == 3) {
            current_index = (current_index - 1 + MENU_COUNT) % MENU_COUNT;
            draw_menu();
        } else if (button == 4) {
            current_index = (current_index + 1) % MENU_COUNT;
            draw_menu();
        } else if (button == 1) {
            in_menu = false;
            lv_obj_clean(lv_scr_act()); 
            main_menu[current_index].func();
        }
    } else {
        if (button == 2) {
            in_menu = true;
            draw_menu();
        }
    }
}

// Button callback
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

void app_main(void)
{
    bsp_i2c_init();
    bsp_display_start();
    bsp_display_backlight_on(); // Set display brightness to 100%

    draw_menu();
    

    // --- Init buttons manually ---
    button_handle_t btn_menu = iot_button_create(&bsp_button_config[0]);
    button_handle_t btn_play = iot_button_create(&bsp_button_config[1]);
    button_handle_t btn_down = iot_button_create(&bsp_button_config[2]);
    button_handle_t btn_up = iot_button_create(&bsp_button_config[3]);

    // Register callbacks
    iot_button_register_cb(btn_menu, BUTTON_SINGLE_CLICK, button_cb_menu, NULL);
    iot_button_register_cb(btn_play, BUTTON_SINGLE_CLICK, button_cb_play, NULL);
    iot_button_register_cb(btn_down, BUTTON_SINGLE_CLICK, button_cb_down, NULL);
    iot_button_register_cb(btn_up,   BUTTON_SINGLE_CLICK, button_cb_up, NULL);
}

// ---- Page Implementations ----
void page_qr_generate(void)
{
    bsp_display_lock(0);

    lv_obj_t *label = lv_label_create(lv_scr_act());
    lv_label_set_text(label, "qr generate");
    lv_obj_center(label);

    bsp_display_unlock();
}

void page_qr_scan(void)
{
    bsp_display_lock(0);

    lv_obj_t *label = lv_label_create(lv_scr_act());
    lv_label_set_text(label, "qr scan");
    lv_obj_center(label);

    bsp_display_unlock();
}
