#include "qr_scan.h"
#include "bsp/esp-bsp.h"
#include "lvgl.h"
#include "esp_camera.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "qr_scan";
static lv_obj_t *camera_canvas = NULL;
static lv_timer_t *camera_timer = NULL;
static bool camera_initialized = false;

// Camera update callback
static void camera_update_cb(lv_timer_t *timer)
{
    (void)timer;
    
    if (!camera_initialized) {
        return;
    }

    // Get frame buffer from camera
    camera_fb_t *fb = esp_camera_fb_get();
    if (!fb) {
        ESP_LOGW(TAG, "Failed to get camera frame");
        return;
    }

    // Lock display for update
    bsp_display_lock(0);

    // Copy frame buffer to canvas
    if (camera_canvas && fb->format == PIXFORMAT_RGB565) {
        lv_canvas_set_buffer(camera_canvas, fb->buf, fb->width, fb->height, LV_COLOR_FORMAT_RGB565);
    }

    bsp_display_unlock();

    // Return frame buffer back to camera driver
    esp_camera_fb_return(fb);
}

// Initialize camera
static esp_err_t init_camera(void)
{
    const camera_config_t camera_config = BSP_CAMERA_DEFAULT_CONFIG;
    
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
    ESP_LOGI(TAG, "Camera initialized successfully");
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

    // Create canvas for camera display
    camera_canvas = lv_canvas_create(lv_scr_act());
    
    // Allocate buffer for canvas (240x240 RGB565 = 115200 bytes)
    static lv_color_t *camera_buf = NULL;
    if (!camera_buf) {
        camera_buf = heap_caps_malloc(240 * 240 * sizeof(lv_color_t), MALLOC_CAP_SPIRAM);
        if (!camera_buf) {
            ESP_LOGE(TAG, "Failed to allocate canvas buffer");
            bsp_display_unlock();
            return;
        }
    }
    
    lv_canvas_set_buffer(camera_canvas, camera_buf, 240, 240, LV_COLOR_FORMAT_RGB565);
    lv_obj_center(camera_canvas);

    // Create a timer to update camera feed
    if (camera_timer) {
        lv_timer_del(camera_timer);
    }
    camera_timer = lv_timer_create(camera_update_cb, 33, NULL);  // ~30 FPS

    bsp_display_unlock();

    ESP_LOGI(TAG, "Camera feed started");
}