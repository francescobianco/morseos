/*
 * MorseOS emulator core: what the GUI and the script runner use to drive the firmware.
 * The firmware runs on one thread only (emu_boot/emu_step); everything else talks to it
 * through emu_key(), emu_post() and the snapshots below, which are thread safe.
 */
#ifndef EMU_H
#define EMU_H

#include <stdint.h>

#define EMU_MAX_W 255
#define EMU_MAX_H 255

typedef struct {
    int width, height;  /* panel size, 80 x 160 on the LT716 */
    int set_time;       /* start with the clock set from the PC (else as after a real boot) */
} emu_opts_t;

typedef struct {
    int booted, screen_on, backlight, speed, typing;
    uint32_t threshold_ms;
    uint64_t motor_until_us;
} emu_status_t;

/* commands, the equivalent of what reaches the device over BLE */
enum {
    EMU_SPEED,       /* v0 = level 1..16 */
    EMU_CONNECT,     /* s = central's name, v0 = RSSI dBm */
    EMU_DISCONNECT,
    EMU_RSSI,        /* v0 = dBm */
    EMU_NOTIFY,      /* s = text */
    EMU_FIND,
    EMU_SETTIME,     /* v0..v5 = year, month, day, hour, min, sec */
    EMU_BATTERY,     /* v0 = mV, v1 = charging */
    EMU_TYPE,        /* s = text, keyed in Morse at the current speed */
};

void emu_post_v(int type, const char *s, const int v[6]);
#define EMU_POST(type, s, ...) emu_post_v(type, s, (const int[6]){__VA_ARGS__})

/* firmware thread */
void emu_boot(const emu_opts_t *opts);
void emu_step(void);

/* the touch key, level as on the GPIO */
void emu_key(int down);

/* clock: wall clock by default, or a virtual one that only emu_advance_us() moves */
void     emu_clock_virtual(void);
void     emu_advance_us(uint64_t us);
uint64_t emu_now_us(void);

/* log: every line goes to the sink and stays searchable for emu_log_expect() */
void emu_log(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
void emu_log_sink(void (*sink)(const char *line));
int  emu_log_expect(const char *text);  /* found since the last match: consumes up to it */

/* snapshots */
void emu_status(emu_status_t *s);
void emu_panel_size(int *w, int *h);
void emu_panel_argb(uint8_t *dst, int stride);  /* cairo ARGB32 layout */

/* used by the emulated libraries and the system module */
void emu_panel_init(int w, int h);
void emu_panel_fill(int x, int y, int w, int h, uint16_t color);
void emu_panel_backlight(int on);
void emu_motor(int ms);
void emu_status_publish(const emu_status_t *s);
int  emu_next_cmd(int *type, char *s, int size, int v[6]);
void emu_typing_done(void);
int  emu_key_level(void);
void battery_emu_set(uint32_t mv, int charging);

/* scripts: see tests/smoke.emu for the commands */
typedef struct {
    void (*wait_ms)(uint32_t ms);
    int  (*shot)(const char *path);
    void (*quit)(int status);
} emu_script_host_t;

int emu_script_run(const char *path, const emu_script_host_t *host);

int gui_run(const emu_opts_t *opts, const char *script, int scale, int argc, char **argv);

#endif
