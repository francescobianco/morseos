/* lib/i2cprobe for the emulator: reports the heart-rate sensor known from the stock firmware. */
#include "i2cprobe.h"
#include "drivers/5316/clock.h"

const char *i2cprobe_pin_name(int i) {
    static const char *const names[] = {"A3", "A4", "C4", "C7"};
    return i >= 0 && i < 4 ? names[i] : "?";
}

const char *i2cprobe_guess(const i2cprobe_hit_t *h) {
    if (h->id_reg != 0xFF && h->addr == 0x44 && h->id == 0x21) return "HRS3300";
    return "?";
}

void i2cprobe_run(i2cprobe_result_t *r) {
    i2cprobe_hit_t *h = &r->hit[0];
    sleep_us(200 * 1000);
    r->n = 1;
    r->busy_mask = 0;
    h->sda = 1;  /* PA4 */
    h->scl = 3;  /* PC7 */
    h->addr = 0x44;
    h->id_reg = 0x00;
    h->id = 0x21;
}
