#include "qr_scan.h"
#include "bsp/esp-bsp.h"
#include "lvgl.h"
#include "esp_camera.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

static const char *TAG = "qr_scan";
static lv_obj_t *camera_canvas = NULL;
static uint8_t *cam_buff = NULL;
static bool camera_initialized = false;
static TaskHandle_t camera_task_handle = NULL;
static bool camera_running = false;

// Camera streaming task
static void camera_stream_task(void *arg)
{
    uint32_t cam_buff_size = BSP_LCD_H_RES * BSP_LCD_V_RES * 2;
    camera_fb_t *pic;
    
    ESP_LOGI(TAG, "Camera streaming task started");
    
    while (camera_running) {
        pic = esp_camera_fb_get();
        if (pic) {
            bsp_display_lock(0);
            
            // Copy frame buffer to canvas buffer
            memcpy(cam_buff, pic->buf, cam_buff_size);
            esp_camera_fb_return(pic);
            
            if (BSP_LCD_BIGENDIAN) {
                /* Swap bytes in RGB565 for big-endian displays */
                lv_draw_sw_rgb565_swap(cam_buff, cam_buff_size);
            }
            
            // Trigger LVGL redraw
            lv_obj_invalidate(camera_canvas);
            
            bsp_display_unlock();
        } else {
            ESP_LOGW(TAG, "Failed to get camera frame");
        }
        
        vTaskDelay(1);  // Minimal delay for maximum FPS
    }
    
    ESP_LOGI(TAG, "Camera streaming task stopped");
    vTaskDelete(NULL);
}

// Initialize camera
static esp_err_t init_camera(void)
{
    camera_config_t camera_config = BSP_CAMERA_DEFAULT_CONFIG;
    
    // Use RGB565 for clear color display
    camera_config.pixel_format = PIXFORMAT_RGB565;
    camera_config.frame_size = FRAMESIZE_240X240;
    
    esp_err_t err = esp_camera_init(&camera_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Camera init failed with error 0x%x", err);
        return err;
    }

    // Get sensor handle and apply settings
    sensor_t *s = esp_camera_sensor_get();
    if (s) {
        s->set_vflip(s, BSP_CAMERA_VFLIP);
        s->set_hmirror(s, BSP_CAMERA_HMIRROR);
    }

    camera_initialized = true;
    ESP_LOGI(TAG, "Camera initialized successfully (RGB565 mode)");
    return ESP_OK;
}

void page_qr_scan(void)
{
    // Initialize camera if not already done
    if (!camera_initialized) {
        esp_err_t err = init_camera();
        if (err != ESP_OK) {
            bsp_display_lock(0);
            lv_obj_t *label = lv_label_create(lv_scr_act());
            lv_label_set_text(label, "Camera Init Failed!");
            lv_obj_center(label);
            bsp_display_unlock();
            return;
        }
    }

    bsp_display_lock(0);

    // Allocate buffer for canvas (240x240 RGB565 = 115200 bytes)
    if (!cam_buff) {
        uint32_t cam_buff_size = BSP_LCD_H_RES * BSP_LCD_V_RES * 2;
        cam_buff = heap_caps_malloc(cam_buff_size, MALLOC_CAP_SPIRAM);
        if (!cam_buff) {
            ESP_LOGE(TAG, "Failed to allocate canvas buffer");
            bsp_display_unlock();
            return;
        }
    }

    // Create canvas for camera display
    camera_canvas = lv_canvas_create(lv_scr_act());
    lv_canvas_set_buffer(camera_canvas, cam_buff, BSP_LCD_H_RES, BSP_LCD_V_RES, LV_COLOR_FORMAT_RGB565);
    lv_obj_center(camera_canvas);

    bsp_display_unlock();

    // Start camera streaming task
    if (!camera_running) {
        camera_running = true;
        xTaskCreatePinnedToCore(
            camera_stream_task,
            "camera_stream",
            4096,
            NULL,
            5,
            &camera_task_handle,
            1
        );
    }

    ESP_LOGI(TAG, "Camera feed started - streaming at maximum FPS");
}