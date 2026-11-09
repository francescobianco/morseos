/* Clock, log, command queue, panel memory and status shared by the emulator threads. */
#define _GNU_SOURCE
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "emu.h"
#include "drivers/5316/clock.h"

/* ------------------------------------------------------------------ clock */

static int virtual_clock;
static uint64_t vtime_us;
static uint64_t start_us;

static uint64_t wall_us(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000u + ts.tv_nsec / 1000;
}

void emu_clock_virtual(void) { virtual_clock = 1; }
void emu_advance_us(uint64_t us) { vtime_us += us; }

uint64_t emu_now_us(void) {
    if (virtual_clock) return vtime_us;
    if (!start_us) start_us = wall_us();
    return wall_us() - start_us;
}

u32 clock_time(void) {
    /* time passes while code runs: firmware loops that wait on the tick must still end */
    if (virtual_clock) vtime_us++;
    return (u32)(emu_now_us() * sys_tick_per_us);
}

int clock_time_exceed(u32 ref, u32 us) { return (u32)(clock_time() - ref) > us * sys_tick_per_us; }

void sleep_us(u32 us) {
    if (virtual_clock) vtime_us += us;
    else usleep(us);
}

/* ------------------------------------------------------------------ log */

#define LOG_SIZE (1 << 16)

static pthread_mutex_t log_lock = PTHREAD_MUTEX_INITIALIZER;
static char log_buf[LOG_SIZE];
static size_t log_len;
static void (*log_sink)(const char *);

void emu_log_sink(void (*sink)(const char *)) { log_sink = sink; }

void emu_log(const char *fmt, ...) {
    char line[320];
    int n;
    va_list ap;
    n = snprintf(line, sizeof(line), "[%8.3f] ", emu_now_us() / 1e6);
    va_start(ap, fmt);
    vsnprintf(line + n, sizeof(line) - n, fmt, ap);
    va_end(ap);

    pthread_mutex_lock(&log_lock);
    n = strlen(line);
    if (log_len + n + 2 > LOG_SIZE) {  /* keep the newest half */
        memmove(log_buf, log_buf + LOG_SIZE / 2, log_len - LOG_SIZE / 2);
        log_len -= LOG_SIZE / 2;
    }
    memcpy(log_buf + log_len, line, n);
    log_len += n;
    log_buf[log_len++] = '\n';
    log_buf[log_len] = 0;
    pthread_mutex_unlock(&log_lock);

    if (log_sink) log_sink(line);
}

int emu_log_expect(const char *text) {
    char *p;
    pthread_mutex_lock(&log_lock);
    p = strstr(log_buf, text);
    if (p) {
        size_t skip = (p - log_buf) + strlen(text);
        memmove(log_buf, log_buf + skip, log_len - skip + 1);
        log_len -= skip;
    }
    pthread_mutex_unlock(&log_lock);
    return p != NULL;
}

/* ------------------------------------------------------------------ commands */

#define CMD_MAX 32

typedef struct {
    int type, v[6];
    char s[200];
} cmd_t;

static pthread_mutex_t cmd_lock = PTHREAD_MUTEX_INITIALIZER;
static cmd_t cmds[CMD_MAX];
static int cmd_head, cmd_count, typing_pending;

void emu_post_v(int type, const char *s, const int v[6]) {
    pthread_mutex_lock(&cmd_lock);
    if (cmd_count < CMD_MAX) {
        cmd_t *c = &cmds[(cmd_head + cmd_count++) % CMD_MAX];
        c->type = type;
        memcpy(c->v, v, sizeof(c->v));
        snprintf(c->s, sizeof(c->s), "%s", s ? s : "");
        if (type == EMU_TYPE) typing_pending++;
    }
    pthread_mutex_unlock(&cmd_lock);
}

int emu_next_cmd(int *type, char *s, int size, int v[6]) {
    int got = 0;
    pthread_mutex_lock(&cmd_lock);
    if (cmd_count) {
        cmd_t *c = &cmds[cmd_head];
        *type = c->type;
        memcpy(v, c->v, sizeof(c->v));
        snprintf(s, size, "%s", c->s);
        cmd_head = (cmd_head + 1) % CMD_MAX;
        cmd_count--;
        got = 1;
    }
    pthread_mutex_unlock(&cmd_lock);
    return got;
}

void emu_typing_done(void) {
    pthread_mutex_lock(&cmd_lock);
    typing_pending--;
    pthread_mutex_unlock(&cmd_lock);
}

/* ------------------------------------------------------------------ key */

static volatile int key_level;

void emu_key(int down) { __atomic_store_n(&key_level, down, __ATOMIC_RELAXED); }
int emu_key_level(void) { return __atomic_load_n(&key_level, __ATOMIC_RELAXED); }

/* ------------------------------------------------------------------ panel */

static pthread_mutex_t panel_lock = PTHREAD_MUTEX_INITIALIZER;
static uint16_t panel[EMU_MAX_H][EMU_MAX_W];
static int panel_w = 80, panel_h = 160;

void emu_panel_init(int w, int h) {
    pthread_mutex_lock(&panel_lock);
    panel_w = w;
    panel_h = h;
    memset(panel, 0, sizeof(panel));
    pthread_mutex_unlock(&panel_lock);
}

void emu_panel_size(int *w, int *h) {
    *w = panel_w;
    *h = panel_h;
}

/* already clipped by lcd_fill() */
void emu_panel_fill(int x, int y, int w, int h, uint16_t color) {
    int i, j;
    pthread_mutex_lock(&panel_lock);
    for (j = y; j < y + h; j++)
        for (i = x; i < x + w; i++) panel[j][i] = color;
    pthread_mutex_unlock(&panel_lock);
}

void emu_panel_argb(uint8_t *dst, int stride) {
    int x, y;
    pthread_mutex_lock(&panel_lock);
    for (y = 0; y < panel_h; y++) {
        uint32_t *row = (uint32_t *)(dst + y * stride);
        for (x = 0; x < panel_w; x++) {
            uint16_t c = panel[y][x];
            uint32_t r = c >> 11, g = c >> 5 & 0x3F, b = c & 0x1F;
            row[x] = 0xFF000000u | (r << 3 | r >> 2) << 16 | (g << 2 | g >> 4) << 8 | (b << 3 | b >> 2);
        }
    }
    pthread_mutex_unlock(&panel_lock);
}

/* ------------------------------------------------------------------ status */

static pthread_mutex_t status_lock = PTHREAD_MUTEX_INITIALIZER;
static emu_status_t status;
static int backlight;
static uint64_t motor_until;

void emu_panel_backlight(int on) {
    pthread_mutex_lock(&status_lock);
    backlight = on;
    pthread_mutex_unlock(&status_lock);
}

void emu_motor(int ms) {
    pthread_mutex_lock(&status_lock);
    motor_until = emu_now_us() + (uint64_t)ms * 1000;
    pthread_mutex_unlock(&status_lock);
    emu_log("buzz %d ms", ms);
}

void emu_status_publish(const emu_status_t *s) {
    pthread_mutex_lock(&status_lock);
    status = *s;
    pthread_mutex_unlock(&status_lock);
}

void emu_status(emu_status_t *s) {
    pthread_mutex_lock(&status_lock);
    *s = status;
    s->backlight = backlight;
    s->motor_until_us = motor_until;
    pthread_mutex_unlock(&status_lock);
    pthread_mutex_lock(&cmd_lock);
    s->typing = typing_pending;
    pthread_mutex_unlock(&cmd_lock);
}
