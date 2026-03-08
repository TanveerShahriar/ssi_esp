#include "qr_scan.h"
#include "bsp/esp-bsp.h"
#include "lvgl.h"
#include "esp_camera.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "quirc.h"
#include <string.h>
#include <inttypes.h>

static const char *TAG = "qr_scan";
static lv_obj_t *camera_canvas = NULL;
static uint8_t *cam_buff = NULL;
static bool camera_initialized = false;
static TaskHandle_t camera_task_handle = NULL;
static volatile bool camera_running = false;
static struct quirc *qr_decoder = NULL;

#define QR_FRAME_WIDTH   240  // Match display width
#define QR_FRAME_HEIGHT  240  // Match display height
#define QR_FRAME_PIXELS  (QR_FRAME_WIDTH * QR_FRAME_HEIGHT)
#define QR_FRAME_BYTES   (QR_FRAME_PIXELS * 2)
#define QR_TEXT_MAX_LEN  512
#define LCD_WIDTH        BSP_LCD_H_RES  // Display canvas width (240)
#define LCD_HEIGHT       BSP_LCD_V_RES  // Display canvas height (240)

static void stop_camera(void)
{
    if (camera_initialized) {
        esp_err_t err = esp_camera_deinit();
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "Camera deinit failed: 0x%x", err);
        }
        camera_initialized = false;
    }
}

static esp_err_t init_qr_decoder(void)
{
    if (qr_decoder) {
        return ESP_OK;
    }

    qr_decoder = quirc_new();
    if (!qr_decoder) {
        ESP_LOGE(TAG, "Failed to allocate quirc decoder");
        return ESP_ERR_NO_MEM;
    }

    if (quirc_resize(qr_decoder, QR_FRAME_WIDTH, QR_FRAME_HEIGHT) < 0) {
        ESP_LOGE(TAG, "Failed to resize quirc decoder");
        quirc_destroy(qr_decoder);
        qr_decoder = NULL;
        return ESP_FAIL;
    }

    return ESP_OK;
}

static void deinit_qr_decoder(void)
{
    if (qr_decoder) {
        quirc_destroy(qr_decoder);
        qr_decoder = NULL;
    }
}

static void rgb565_to_grayscale(const uint8_t *rgb565, uint8_t *gray, uint32_t pixels)
{
    for (uint32_t i = 0, offset = 0; i < pixels; i++, offset += 2) {
        uint16_t pixel = ((uint16_t)rgb565[offset] << 8) | rgb565[offset + 1];
        uint8_t r = (uint8_t)((((pixel >> 11) & 0x1F) * 527 + 23) >> 6);
        uint8_t g = (uint8_t)((((pixel >> 5) & 0x3F) * 259 + 33) >> 6);
        uint8_t b = (uint8_t)(((pixel & 0x1F) * 527 + 23) >> 6);
        gray[i] = (uint8_t)((r * 30 + g * 59 + b * 11) / 100);
    }
}

static bool decode_qr_payload(const uint8_t *rgb565_buf, char *decoded, size_t decoded_size)
{
    static uint32_t decode_call_count = 0;
    decode_call_count++;
    
    if (!rgb565_buf || !decoded || decoded_size == 0 || !qr_decoder) {
        return false;
    }

    int width = 0;
    int height = 0;
    uint8_t *image = quirc_begin(qr_decoder, &width, &height);
    if (!image) {
        if (decode_call_count % 50 == 0) {
            ESP_LOGW(TAG, "quirc_begin failed at call %u", (unsigned)decode_call_count);
        }
        return false;
    }

    if (width <= 0 || height <= 0) {
        ESP_LOGW(TAG, "Invalid quirc dims: %dx%d", width, height);
        quirc_end(qr_decoder);
        return false;
    }

    uint32_t pixels_to_convert = (width * height);
    if (pixels_to_convert > QR_FRAME_PIXELS) {
        pixels_to_convert = QR_FRAME_PIXELS;
    }

    // Convert RGB565 to grayscale
    rgb565_to_grayscale(rgb565_buf, image, pixels_to_convert);
    quirc_end(qr_decoder);

    int code_count = quirc_count(qr_decoder);
    
    // Log every 100 calls even if no codes found
    if (decode_call_count % 100 == 0) {
        ESP_LOGI(TAG, "Decode call %u: found %d codes", (unsigned)decode_call_count, code_count);
    }
    
    if (code_count <= 0) {
        return false;
    }

    ESP_LOGI(TAG, "Found %d QR codes", code_count);

    // Try to decode each code found
    for (int i = 0; i < code_count; i++) {
        struct quirc_code code;
        struct quirc_data data;

        quirc_extract(qr_decoder, i, &code);
        quirc_decode_error_t err = quirc_decode(&code, &data);
        
        if (err != QUIRC_SUCCESS) {
            struct quirc_code flipped = code;
            quirc_flip(&flipped);
            err = quirc_decode(&flipped, &data);
        }

        if (err == QUIRC_SUCCESS) {
            if (data.payload_len > 0) {
                size_t payload_len = data.payload_len;
                if (payload_len >= decoded_size) {
                    payload_len = decoded_size - 1;
                }
                memcpy(decoded, data.payload, payload_len);
                decoded[payload_len] = '\0';
                ESP_LOGI(TAG, "SUCCESS! Payload: %s", decoded);
                return true;
            }
        } else {
            ESP_LOGI(TAG, "Code %d decode failed: %d", i, err);
        }
    }

    return false;
}

static void show_decoded_data(const char *decoded_data)
{
    bsp_display_lock(0);

    lv_obj_clean(lv_scr_act());
    camera_canvas = NULL;

    lv_obj_t *label = lv_label_create(lv_scr_act());
    lv_obj_set_width(label, BSP_LCD_H_RES - 16);
    lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
    lv_label_set_text(label, decoded_data);
    lv_obj_center(label);

    bsp_display_unlock();
}

// Camera streaming task
static void camera_stream_task(void *arg)
{
    camera_fb_t *pic;
    char decoded_data[QR_TEXT_MAX_LEN] = {0};
    uint8_t *temp_frame = NULL;
    bool decode_success = false;
    uint32_t frame_count = 0;
    uint32_t decode_attempts = 0;
    
    ESP_LOGI(TAG, "Camera streaming task started");
    
    // Allocate temporary buffer for frame processing
    temp_frame = heap_caps_malloc(QR_FRAME_BYTES, MALLOC_CAP_8BIT);
    if (!temp_frame) {
        ESP_LOGE(TAG, "Failed to allocate temp frame buffer");
        camera_running = false;
        goto task_exit;
    }
    
    while (camera_running) {
        pic = esp_camera_fb_get();
        if (!pic) {
            ESP_LOGW(TAG, "Failed to get camera frame");
            vTaskDelay(pdMS_TO_TICKS(50));
            continue;
        }
        
        // Log frame info every 30 frames for diagnostics
        if (frame_count % 30 == 0) {
            ESP_LOGI(TAG, "Frame %lu: len=%u", frame_count, (unsigned)pic->len);
        }
        
        // Display the frame on canvas
        if (camera_canvas && pic->len > 0) {
            bsp_display_lock(0);
            uint32_t copy_len = (pic->len < QR_FRAME_BYTES) ? pic->len : QR_FRAME_BYTES;
            memcpy(cam_buff, pic->buf, copy_len);
            
            if (BSP_LCD_BIGENDIAN) {
                lv_draw_sw_rgb565_swap(cam_buff, copy_len);
            }
            
            lv_obj_invalidate(camera_canvas);
            bsp_display_unlock();
        }
        
        // Copy frame to temp buffer for QR decoding (outside display lock)
        uint32_t frame_len = pic->len;
        if (frame_len > 0 && frame_len <= QR_FRAME_BYTES) {
            memcpy(temp_frame, pic->buf, frame_len);
        }
        
        esp_camera_fb_return(pic);
        
        // Try to decode every 3rd frame (reduce CPU load)
        if (frame_count % 3 == 0 && frame_len > 0 && frame_len == QR_FRAME_BYTES) {
            decode_attempts++;
            decode_success = decode_qr_payload(temp_frame, decoded_data, sizeof(decoded_data));
            
            if (decode_success) {
                ESP_LOGI(TAG, "QR decoded after %u attempts!", (unsigned)decode_attempts);
                // Clear canvas reference immediately to prevent race condition
                camera_canvas = NULL;
                break;
            }
        }
        
        frame_count++;
        vTaskDelay(pdMS_TO_TICKS(10));  // Longer delay to reduce CPU and prevent watchdog
    }

task_exit:
    // Stop camera before any UI operations
    camera_running = false;
    stop_camera();
    deinit_qr_decoder();
    
    if (temp_frame) {
        heap_caps_free(temp_frame);
    }
    
    // If decode succeeded, wait a bit for any pending display ops to complete
    if (decode_success) {
        vTaskDelay(pdMS_TO_TICKS(50));
        show_decoded_data(decoded_data);
    }
    
    camera_task_handle = NULL;
    
    ESP_LOGI(TAG, "Camera task stopped: %lu frames, %lu decode attempts", 
             frame_count, decode_attempts);
    vTaskDelete(NULL);
}

// Initialize camera
static esp_err_t init_camera(void)
{
    camera_config_t camera_config = BSP_CAMERA_DEFAULT_CONFIG;
    
    // Use RGB565 with 240x240 resolution to match display
    camera_config.pixel_format = PIXFORMAT_RGB565;
    camera_config.frame_size = FRAMESIZE_240X240;  // Match display resolution
    
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
    ESP_LOGI(TAG, "Camera initialized successfully (240x240 RGB565)");
    return ESP_OK;
}

void page_qr_scan_stop(void)
{
    camera_canvas = NULL;
    camera_running = false;

    if (camera_task_handle && camera_task_handle != xTaskGetCurrentTaskHandle()) {
        while (camera_task_handle) {
            vTaskDelay(pdMS_TO_TICKS(10));
        }
    }

    if (!camera_task_handle) {
        stop_camera();
        deinit_qr_decoder();
    }
}

void page_qr_scan(void)
{
    page_qr_scan_stop();

    if (!camera_initialized) {
        esp_err_t err = init_camera();
        if (err != ESP_OK) {
            bsp_display_lock(0);
            lv_obj_clean(lv_scr_act());
            lv_obj_t *label = lv_label_create(lv_scr_act());
            lv_label_set_text(label, "Camera Init Failed!");
            lv_obj_center(label);
            bsp_display_unlock();
            return;
        }
    }

    if (init_qr_decoder() != ESP_OK) {
        stop_camera();
        bsp_display_lock(0);
        lv_obj_clean(lv_scr_act());
        lv_obj_t *label = lv_label_create(lv_scr_act());
        lv_label_set_text(label, "QR Decoder Init Failed!");
        lv_obj_center(label);
        bsp_display_unlock();
        return;
    }

    bsp_display_lock(0);

    if (!cam_buff) {
        cam_buff = heap_caps_malloc(QR_FRAME_BYTES, MALLOC_CAP_SPIRAM);
        if (!cam_buff) {
            cam_buff = heap_caps_malloc(QR_FRAME_BYTES, MALLOC_CAP_8BIT);
        }
        if (!cam_buff) {
            ESP_LOGE(TAG, "Failed to allocate frame buffer (%zu bytes)", QR_FRAME_BYTES);
            bsp_display_unlock();
            stop_camera();
            deinit_qr_decoder();
            return;
        }
        ESP_LOGI(TAG, "Allocated %zu byte frame buffer", QR_FRAME_BYTES);
    }

    lv_obj_clean(lv_scr_act());
    
    // Create canvas to display camera feed
    camera_canvas = lv_canvas_create(lv_scr_act());
    if (!camera_canvas) {
        ESP_LOGE(TAG, "Failed to create canvas");
        bsp_display_unlock();
        stop_camera();
        deinit_qr_decoder();
        return;
    }
    
    // Set canvas buffer - display as RGB565
    lv_canvas_set_buffer(camera_canvas, cam_buff, LCD_WIDTH, LCD_HEIGHT, LV_COLOR_FORMAT_RGB565);
    lv_obj_center(camera_canvas);
    
    // Add status label on top
    lv_obj_t *status_label = lv_label_create(lv_scr_act());
    lv_label_set_text(status_label, "Scanning...");
    lv_obj_align(status_label, LV_ALIGN_TOP_MID, 0, 5);

    bsp_display_unlock();

    if (!camera_running) {
        camera_running = true;
        xTaskCreatePinnedToCore(
            camera_stream_task,
            "camera_stream",
            8192,  // Increased from 4096 to prevent stack overflow with quirc
            NULL,
            5,
            &camera_task_handle,
            1
        );
    }

    ESP_LOGI(TAG, "Camera feed started, scanning QR continuously");
}