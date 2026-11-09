/*
 * core/system for the emulator: the main loop of core/system/main.c without the radio.
 * Same order of calls, same stuck-key guard, same emergency sequence. What arrives over BLE
 * on the device (speed, FitPro time / notifications / find, link state) arrives as
 * emulator commands, processed in the same place of the loop.
 */
#include <ctype.h>

#include "mos.h"
#include "morse.h"
#include "st7735.h"
#include "drivers/5316/clock.h"
#include "emu.h"
#ifdef MOS_HAVE_BATTERY
#include "battery.h"
#endif
#ifdef MOS_HAVE_RTC
#include <time.h>
#include "rtc.h"
#endif
#ifdef MOS_HAVE_MESSAGE
#include "message.h"
#endif

#define EMERGENCY_WARN_MS  10000
#define EMERGENCY_TAP_MS   400
#define EMERGENCY_ARMED_MS 5000
#define STUCK_MS           30000
#define MAX_SEGS           640

static void buzz(int ms) {
    emu_motor(ms);
    sleep_us(ms * 1000);
}

/* the touchlog of lt716 dumper, from the DATA notifications */
static void notify(const u8 *p, int len) {
    char kind = p[4];
    (void)len;
    if (kind == 'T') {
        unsigned press = p[6] | p[7] << 8, gap = p[8] | p[9] << 8, th = p[10] | p[11] << 8;
        emu_log("T %c premuto %u ms, pausa %u ms, soglia %u ms, velocita %u", p[5], press, gap, th, p[12]);
    } else if (kind == 'L') {
        emu_log("L %c %.14s", p[5], (const char *)p + 6);
    } else if (kind == 'W') {
        emu_log("W %.15s", (const char *)p + 5);
    } else if (kind == 'S') {
        emu_log("S tasto bloccato");
    }
}

/* ------------------------------------------------------------------ automatic keyer */

typedef struct { u8 level; u16 ms; } seg_t;

static seg_t segs[MAX_SEGS];
static int nsegs, iseg, keyer_on;
static u32 t_seg;

static void add_seg(int level, u32 ms) {
    if (nsegs == MAX_SEGS) return;
    segs[nsegs].level = level;
    segs[nsegs].ms = ms;
    nsegs++;
}

static void keyer_start(const char *text) {
    u32 th = morse_threshold_ms();
    nsegs = iseg = 0;
    if (!mos_screen_on()) {        /* the touch that wakes the screen does not count */
        add_seg(1, 100);
        add_seg(0, 300);
    }
    for (; *text; text++) {
        const char *code = morse_code_of(toupper((unsigned char)*text));
        if (!code) continue;
        for (; *code; code++) {
            u32 press = *code == '.' ? th / 2 : th + th / 4;
            u32 gap = th / 2;
            /* stay out of the erase burst: 4 presses within 700 ms */
            if (press + gap < 240) gap = 240 - press;
            add_seg(1, press);
            add_seg(0, code[1] ? gap : 2 * th + 80);
        }
    }
    keyer_on = 1;
    t_seg = clock_time();
}

static int keyer_level(void) {
    if (!keyer_on) return 0;
    while (iseg < nsegs && mos_ms_since(t_seg) >= segs[iseg].ms) {
        t_seg += segs[iseg].ms * (sys_tick_per_us * 1000);
        iseg++;
    }
    if (iseg == nsegs) {
        keyer_on = 0;
        emu_typing_done();
        return 0;
    }
    return segs[iseg].level;
}

/* ------------------------------------------------------------------ link and commands */

static int connected, rssi_dbm = -60, name_pending;
static char peer_name[20];
static u32 t_conn, t_rssi;

static void copy_name(const char *s) {
    int i;
    for (i = 0; s[i] && i < (int)sizeof(peer_name) - 1; i++) peer_name[i] = s[i];
    peer_name[i] = 0;
}

#ifdef MOS_HAVE_FITPRO
static void find_band(void) {
    int i;
    mos_wake();
    for (i = 0; i < 3; i++) {
        mos_buzz(200);
        sleep_us(150 * 1000);
    }
}
#endif

static void do_commands(void) {
    int type, v[6];
    char s[200];
    while (emu_next_cmd(&type, s, sizeof(s), v)) {
        switch (type) {
        case EMU_SPEED:
            morse_set_speed(v[0]);
            emu_log("velocita %d, soglia punto %u ms", morse_speed(), (unsigned)morse_threshold_ms());
            break;
        case EMU_CONNECT:
            connected = 1;
            mos_set_connected(1);
            copy_name(s);
            rssi_dbm = v[0];
            name_pending = 1;                /* main.c reads the name 2 s after connecting */
            t_conn = t_rssi = clock_time();
            emu_log("BT connesso: %s, %d dBm", s, v[0]);
            break;
        case EMU_DISCONNECT:
            connected = 0;
            name_pending = 0;
            mos_set_connected(0);
            emu_log("BT disconnesso");
            break;
        case EMU_RSSI:
            rssi_dbm = v[0];
            break;
#ifdef MOS_HAVE_MESSAGE
        case EMU_NOTIFY:
            emu_log("notifica: %s", s);
            message_show(s);
            break;
#endif
#ifdef MOS_HAVE_FITPRO
        case EMU_FIND:
            emu_log("trova band");
            find_band();
            break;
#endif
#ifdef MOS_HAVE_RTC
        case EMU_SETTIME:
            rtc_set(v[0], v[1], v[2], v[3], v[4], v[5]);
            emu_log("ora impostata: %04d-%02d-%02d %02d:%02d:%02d", v[0], v[1], v[2], v[3], v[4], v[5]);
            break;
#endif
#ifdef MOS_HAVE_BATTERY
        case EMU_BATTERY:
            battery_emu_set(v[0], v[1]);
            battery_update();
            break;
#endif
        case EMU_TYPE:
            emu_log("digito: %s", s);
            keyer_start(s);
            if (!keyer_on) emu_typing_done();
            break;
        }
    }
}

/* ------------------------------------------------------------------ boot and main loop */

static int emergency_stage, key_prev, stuck, taps;
static u32 t_key, t_armed, key_press_ms;

void emu_boot(const emu_opts_t *o) {
    lcd.width = o->width;
    lcd.height = o->height;
    buzz(300);
    lcd_init();
    lcd_backlight(1);
#ifdef MOS_HAVE_BATTERY
    battery_update();
#endif
#ifdef MOS_HAVE_RTC
    if (o->set_time) {
        time_t now = time(NULL);
        struct tm t;
        localtime_r(&now, &t);
        rtc_set(t.tm_year + 1900, t.tm_mon + 1, t.tm_mday, t.tm_hour, t.tm_min, t.tm_sec);
    }
#endif
    mos_init(buzz, notify);
    t_key = clock_time();
    emu_log("MorseOS %s avviato", MOS_VERSION);
}

void emu_step(void) {
    int raw, t, key_released = 0;
    u32 held;
    emu_status_t st = {0};

    do_commands();

    raw = (emu_key_level() || keyer_level()) ? 1 : 0;
    if (raw != key_prev) {
        if (!raw) {
            key_released = !stuck;
            key_press_ms = mos_ms_since(t_key);
            stuck = 0;
        }
        key_prev = raw;
        t_key = clock_time();
    }
    held = raw ? mos_ms_since(t_key) : 0;
    if (raw && !stuck && held > STUCK_MS) {
        static const u8 ev[20] = {0xFD, 0xFF, 0xFF, 0xFF, 'S'};
        stuck = 1;
        notify(ev, sizeof(ev));
        if (emergency_stage) {
            emergency_stage = 0;
            mos_emergency_warning(0, 0);
        }
    }
    t = stuck ? 0 : raw;
#ifdef MOS_HAVE_RTC
    rtc_poll();
#endif
#ifdef MOS_HAVE_BATTERY
    battery_poll();
#endif
    mos_poll(emergency_stage == 0 ? t : 0);

    if (emergency_stage == 0 && t && held > EMERGENCY_WARN_MS) {
        emergency_stage = 1;
        buzz(400);
        mos_emergency_warning(1, 0);
    } else if (emergency_stage == 1 && !t) {
        emergency_stage = 2;
        taps = 0;
        t_armed = clock_time();
        mos_emergency_warning(2, 0);
    } else if (emergency_stage == 2) {
        if (key_released && key_press_ms < EMERGENCY_TAP_MS) {
            taps++;
            buzz(30);
            mos_emergency_warning(2, taps);
            t_armed = clock_time();
            if (taps == 3) {
                buzz(100);
                sleep_us(100 * 1000);
                buzz(100);
                sleep_us(100 * 1000);
                buzz(100);
                emu_log("AGGIORNATORE: sul dispositivo qui si riavvia nell'updater");
                emergency_stage = 0;
                mos_emergency_warning(0, 0);
            }
        } else if ((key_released && key_press_ms >= EMERGENCY_TAP_MS) ||
                   (!t && mos_ms_since(t_armed) > EMERGENCY_ARMED_MS) ||
                   (t && held >= EMERGENCY_TAP_MS)) {
            emergency_stage = 0;
            mos_emergency_warning(0, 0);
        }
    }

    if (connected) {
        if (name_pending && mos_ms_since(t_conn) > 2000) {
            name_pending = 0;
            mos_set_peer(peer_name);
        }
        if (mos_ms_since(t_rssi) > 1000) {
            t_rssi = clock_time();
            mos_set_rssi(rssi_dbm);
        }
    }

    st.booted = 1;
    st.screen_on = mos_screen_on();
    st.speed = morse_speed();
    st.threshold_ms = morse_threshold_ms();
    emu_status_publish(&st);
}
