#include "qr_generate.h"
#include "bsp/esp-bsp.h"
#include "lvgl.h"

void page_qr_generate(void)
{
    bsp_display_lock(0);

    lv_obj_t *label = lv_label_create(lv_scr_act());
    lv_label_set_text(label, "qr generate");
    lv_obj_center(label);

    bsp_display_unlock();
}