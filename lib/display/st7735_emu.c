/* lib/display for the emulator: same interface as st7735.c, the panel is a framebuffer. */
#include "st7735.h"
#include "drivers/5316/clock.h"
#include "emu.h"

lcd_cfg_t lcd = {26, 1, 80, 160, MADCTL_MY | MADCTL_MX | MADCTL_BGR, 1};

void lcd_init(void) {
    sleep_us(645 * 1000);  /* the real init sequence takes this long */
    emu_panel_init(lcd.width, lcd.height);
}

void lcd_apply(void) {}
void lcd_backlight(int on) { emu_panel_backlight(on); }

void lcd_sleep(int sleep) {
    static int asleep;
    if (sleep == asleep) return;
    asleep = sleep;
    sleep_us((sleep ? 5 : 10) * 1000);
}

void lcd_fill(int x, int y, int w, int h, u16 color) {
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > lcd.width) w = lcd.width - x;
    if (y + h > lcd.height) h = lcd.height - y;
    if (w <= 0 || h <= 0) return;
    emu_panel_fill(x, y, w, h, color);
}
