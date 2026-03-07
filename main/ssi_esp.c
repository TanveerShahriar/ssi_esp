#include "bsp/esp-bsp.h"
#include "menu/menu.h"
#include "buttons/buttons.h"

void app_main(void)
{
    bsp_i2c_init();

    bsp_display_start();
    bsp_display_backlight_on();

    draw_menu();

    buttons_init();
}