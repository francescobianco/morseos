/* lib/battery for the emulator: same gauge as battery.c, the ADC is the GUI slider. */
#include "battery.h"
#include "drivers/5316/clock.h"
#include "emu.h"

static u32 pin_vbat_mv = 3900, pin_charger_mv;
static u32 vbat_mv = 4000, charger_mv;

void battery_emu_set(uint32_t mv, int charging) {
    pin_vbat_mv = mv;
    pin_charger_mv = charging ? 5000 : 0;
}

void battery_update(void) {
    vbat_mv = pin_vbat_mv;
    charger_mv = pin_charger_mv;
}

void battery_poll(void) {
    static u32 t_last;
    static int started;
    if (!started || clock_time_exceed(t_last, 30 * 1000 * 1000)) {
        started = 1;
        t_last = clock_time();
        battery_update();
    }
}

u32 battery_mv(void) { return vbat_mv; }

int battery_level(void) {
    if (vbat_mv >= 3800) return 100;
    if (vbat_mv >= 3700) return 75;
    if (vbat_mv >= 3600) return 50;
    if (vbat_mv >= 3500) return 25;
    return 0;
}

int battery_charging(void) { return charger_mv >= 4400 && charger_mv <= 6500; }
