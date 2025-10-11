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

#include "iot_button.h"   // Needed for button driver

static const char *TAG = "ssi_esp";

// Button callback
static void button_cb_menu(void *arg, void *data)
{
    ESP_LOGI(TAG, "Menu pressed");
}

static void button_cb_play(void *arg, void *data)
{
    ESP_LOGI(TAG, "Play pressed");
}

static void button_cb_up(void *arg, void *data)
{
    ESP_LOGI(TAG, "Up pressed");
}

static void button_cb_down(void *arg, void *data)
{
    ESP_LOGI(TAG, "Down pressed");
}


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

void app_main(void)
{
    bsp_i2c_init();
    bsp_display_start();
    bsp_display_backlight_on(); // Set display brightness to 100%

    bsp_display_brightness_set(50);

    // Hello World
    bsp_display_lock(0);

    lv_obj_t *label = lv_label_create(lv_scr_act());
    lv_label_set_text(label, "Hello World!");
    lv_obj_center(label);

    bsp_display_unlock();
    
    vTaskDelay(pdMS_TO_TICKS(3000));

    lv_obj_del(label);

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


    ESP_LOGI(TAG, "Button test ready! Press buttons...");



    // Initialize the camera
    const camera_config_t camera_config = BSP_CAMERA_DEFAULT_CONFIG;
    esp_err_t err = esp_camera_init(&camera_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Camera Init Failed");
        return;
    }
    sensor_t *s = esp_camera_sensor_get();
    s->set_vflip(s, BSP_CAMERA_VFLIP);
    s->set_hmirror(s, BSP_CAMERA_HMIRROR);
    ESP_LOGI(TAG, "Camera Init done");

    uint32_t cam_buff_size = BSP_LCD_H_RES * BSP_LCD_V_RES * 2;
    uint8_t *cam_buff = heap_caps_malloc(cam_buff_size, MALLOC_CAP_SPIRAM);
    assert(cam_buff);

    // Create LVGL canvas for camera image
    bsp_display_lock(0);
    lv_obj_t *camera_canvas = lv_canvas_create(lv_scr_act());
    lv_canvas_set_buffer(camera_canvas, cam_buff, BSP_LCD_H_RES, BSP_LCD_V_RES, LV_COLOR_FORMAT_RGB565);
    assert(camera_canvas);
    lv_obj_center(camera_canvas);
    bsp_display_unlock();

    camera_fb_t *pic;
    while (1) {
        pic = esp_camera_fb_get();
        if (pic) {
            bsp_display_lock(0);
            memcpy(cam_buff, pic->buf, cam_buff_size);
            esp_camera_fb_return(pic);
            if (BSP_LCD_BIGENDIAN) {
                /* Swap bytes in RGB565 */
                lv_draw_sw_rgb565_swap(cam_buff, cam_buff_size);
            }
            lv_obj_invalidate(camera_canvas);
            bsp_display_unlock();
        } else {
            ESP_LOGE(TAG, "Get frame failed");
        }
        vTaskDelay(1);
    }
}
