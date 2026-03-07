#include "menu.h"
#include "bsp/esp-bsp.h"
#include "lvgl.h"

#include "pages/qr_generate.h"
#include "pages/qr_scan.h"

static int current_index = 0;
static bool in_menu = true;

// ---- Menu array ----
static MenuItem main_menu[] = {
    {"QR Generate", page_qr_generate},
    {"QR Scan", page_qr_scan},
};

#define MENU_COUNT (sizeof(main_menu) / sizeof(main_menu[0]))

void draw_menu()
{
    bsp_display_lock(0);

    lv_obj_clean(lv_scr_act());

    for (int i = 0; i < MENU_COUNT; i++) {
        lv_obj_t *label = lv_label_create(lv_scr_act());

        char line[32];
        snprintf(line, sizeof(line), "%c %s",
                 (i == current_index ? '>' : ' '),
                 main_menu[i].name);

        lv_label_set_text(label, line);
        lv_obj_align(label, LV_ALIGN_TOP_LEFT, 10, i * 25);
    }

    bsp_display_unlock();
}

void handle_button(int button)
{
    if (in_menu) {
        if (button == 3) {
            current_index = (current_index - 1 + MENU_COUNT) % MENU_COUNT;
            draw_menu();
        }
        else if (button == 4) {
            current_index = (current_index + 1) % MENU_COUNT;
            draw_menu();
        }
        else if (button == 1) {
            in_menu = false;
            lv_obj_clean(lv_scr_act());
            main_menu[current_index].func();
        }
    }
    else {
        if (button == 2) {
            in_menu = true;
            draw_menu();
        }
    }
}