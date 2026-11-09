/* MorseOS emulator: GTK window, or headless with a virtual clock for scripted runs. */
#include <cairo.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "emu.h"

static void usage(FILE *f) {
    fputs("uso: morseos-emu [opzioni]\n"
          "  --script FILE   esegue uno script (vedi tests/smoke.emu)\n"
          "  --headless      senza finestra, orologio virtuale (richiede --script)\n"
          "  --scale N       zoom del display, default 3\n"
          "  --lcd LxA       dimensioni del pannello, default 80x160\n"
          "  --no-time       parte con l'ora non impostata, come il dispositivo appena acceso\n", f);
}

static void print_line(const char *s) { puts(s); }

static void headless_wait(uint32_t ms) {
    while (ms--) {
        emu_step();
        emu_advance_us(1000);
    }
}

static int headless_shot(const char *path) {
    enum { ZOOM = 4 };
    int w, h, st;
    cairo_surface_t *panel, *out;
    cairo_t *cr;
    cairo_status_t rc;
    emu_panel_size(&w, &h);
    panel = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h);
    emu_panel_argb(cairo_image_surface_get_data(panel), cairo_image_surface_get_stride(panel));
    cairo_surface_mark_dirty(panel);
    out = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w * ZOOM, h * ZOOM);
    cr = cairo_create(out);
    cairo_scale(cr, ZOOM, ZOOM);
    cairo_set_source_surface(cr, panel, 0, 0);
    cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_NEAREST);
    cairo_paint(cr);
    rc = cairo_surface_write_to_png(out, path);
    cairo_destroy(cr);
    cairo_surface_destroy(out);
    cairo_surface_destroy(panel);
    st = rc != CAIRO_STATUS_SUCCESS;
    return st;
}

static void headless_quit(int status) { exit(status); }

int main(int argc, char **argv) {
    emu_opts_t opts = {80, 160, 1};
    const char *script = NULL;
    int headless = 0, scale = 3, i;

    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--headless")) headless = 1;
        else if (!strcmp(argv[i], "--no-time")) opts.set_time = 0;
        else if (!strcmp(argv[i], "--script") && i + 1 < argc) script = argv[++i];
        else if (!strcmp(argv[i], "--scale") && i + 1 < argc) scale = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--lcd") && i + 1 < argc) {
            if (sscanf(argv[++i], "%dx%d", &opts.width, &opts.height) != 2 || opts.width < 1 ||
                opts.height < 1 || opts.width > EMU_MAX_W || opts.height > EMU_MAX_H) {
                fprintf(stderr, "--lcd: massimo %dx%d\n", EMU_MAX_W, EMU_MAX_H);
                return 2;
            }
        } else {
            usage(strcmp(argv[i], "--help") ? stderr : stdout);
            return strcmp(argv[i], "--help") ? 2 : 0;
        }
    }
    if (scale < 1) scale = 1;

    if (headless) {
        static const emu_script_host_t host = {headless_wait, headless_shot, headless_quit};
        if (!script) {
            usage(stderr);
            return 2;
        }
        emu_clock_virtual();
        emu_log_sink(print_line);
        emu_boot(&opts);
        return emu_script_run(script, &host);
    }
    return gui_run(&opts, script, scale, argc, argv);
}
