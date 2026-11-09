/* The emulator window: the band with its screen and touch key, and the outside world. */
#define _GNU_SOURCE
#include <gtk/gtk.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "emu.h"

#define BEZEL     14
#define BODY_TOP  20
#define KEY_ZONE  84
#define KEY_R     24
#define MARGIN_V  48

static emu_opts_t opts;
static int scale;
static const char *script_path;

static GtkWidget *window, *area, *status_label, *log_view;
static GtkWidget *e_type, *e_time, *e_peer, *e_notify, *c_bt, *c_chg, *s_mv, *s_rssi, *s_speed;
static cairo_surface_t *panel_surface;
static int mouse_down, space_down;

/* ------------------------------------------------------------------ firmware thread */

static gpointer firmware_thread(gpointer data) {
    (void)data;
    emu_boot(&opts);
    for (;;) {
        emu_step();
        usleep(500);
    }
    return NULL;
}

/* ------------------------------------------------------------------ the band */

typedef struct { double body_x, body_y, body_w, body_h, key_x, key_y; int w, h; } layout_t;

static void get_layout(layout_t *l) {
    int pw, ph;
    emu_panel_size(&pw, &ph);
    l->w = pw * scale + 2 * BEZEL + 96;
    l->h = ph * scale + BODY_TOP + KEY_ZONE + 2 * MARGIN_V;
    l->body_w = pw * scale + 2 * BEZEL;
    l->body_h = ph * scale + BODY_TOP + KEY_ZONE;
    l->body_x = (l->w - l->body_w) / 2;
    l->body_y = MARGIN_V;
    l->key_x = l->w / 2.0;
    l->key_y = l->body_y + BODY_TOP + ph * scale + KEY_ZONE / 2.0;
}

static void round_rect(cairo_t *cr, double x, double y, double w, double h, double r) {
    cairo_new_sub_path(cr);
    cairo_arc(cr, x + w - r, y + r, r, -G_PI / 2, 0);
    cairo_arc(cr, x + w - r, y + h - r, r, 0, G_PI / 2);
    cairo_arc(cr, x + r, y + h - r, r, G_PI / 2, G_PI);
    cairo_arc(cr, x + r, y + r, r, G_PI, 1.5 * G_PI);
    cairo_close_path(cr);
}

static gboolean on_draw(GtkWidget *w, cairo_t *cr, gpointer data) {
    layout_t l;
    emu_status_t st;
    int pw, ph, i, buzzing, down = emu_key_level();
    double strap_w;
    (void)w; (void)data;

    get_layout(&l);
    emu_status(&st);
    emu_panel_size(&pw, &ph);
    buzzing = emu_now_us() < st.motor_until_us + 70000;

    cairo_set_source_rgb(cr, 0.13, 0.15, 0.17);
    cairo_paint(cr);

    if (buzzing) cairo_translate(cr, (emu_now_us() / 18000) % 2 ? 2 : -2, 0);

    /* strap */
    strap_w = l.body_w - 16;
    cairo_rectangle(cr, (l.w - strap_w) / 2, 0, strap_w, l.h);
    cairo_set_source_rgb(cr, 0.07, 0.08, 0.09);
    cairo_fill(cr);
    cairo_set_source_rgb(cr, 0.13, 0.15, 0.17);
    for (i = 0; i < 2; i++) {
        cairo_arc(cr, l.w / 2.0, l.h - 12 - 18 * i, 3.5, 0, 2 * G_PI);
        cairo_fill(cr);
    }

    /* body */
    round_rect(cr, l.body_x, l.body_y, l.body_w, l.body_h, 22);
    cairo_set_source_rgb(cr, 0.02, 0.02, 0.03);
    cairo_fill_preserve(cr);
    cairo_set_source_rgb(cr, 0.30, 0.33, 0.36);
    cairo_set_line_width(cr, 2);
    cairo_stroke(cr);

    /* screen: unlit it shows nothing, the image is kept in the panel */
    emu_panel_argb(cairo_image_surface_get_data(panel_surface), cairo_image_surface_get_stride(panel_surface));
    cairo_surface_mark_dirty(panel_surface);
    cairo_save(cr);
    cairo_translate(cr, l.body_x + BEZEL, l.body_y + BODY_TOP);
    if (st.backlight) {
        cairo_scale(cr, scale, scale);
        cairo_set_source_surface(cr, panel_surface, 0, 0);
        cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_NEAREST);
        cairo_rectangle(cr, 0, 0, pw, ph);
        cairo_fill(cr);
    } else {
        cairo_rectangle(cr, 0, 0, pw * scale, ph * scale);
        cairo_set_source_rgb(cr, 0.0, 0.0, 0.0);
        cairo_fill(cr);
    }
    cairo_restore(cr);

    /* touch key */
    if (down) {
        cairo_arc(cr, l.key_x, l.key_y, KEY_R + 7, 0, 2 * G_PI);
        cairo_set_source_rgba(cr, 1.0, 0.85, 0.1, 0.28);
        cairo_fill(cr);
    }
    cairo_arc(cr, l.key_x, l.key_y, KEY_R, 0, 2 * G_PI);
    if (down) cairo_set_source_rgb(cr, 1.0, 0.85, 0.1);
    else cairo_set_source_rgb(cr, 0.16, 0.18, 0.20);
    cairo_fill_preserve(cr);
    cairo_set_source_rgb(cr, 0.42, 0.46, 0.50);
    cairo_set_line_width(cr, 1.5);
    cairo_stroke(cr);

    return FALSE;
}

static gboolean refresh(gpointer data) {
    static char last[160];
    char text[160];
    emu_status_t st;
    (void)data;
    gtk_widget_queue_draw(area);
    emu_status(&st);
    if (st.booted) {
        snprintf(text, sizeof(text), "Schermo %s  ·  velocità %d  ·  punto sotto %u ms  ·  fine lettera %u ms",
                 st.screen_on ? "acceso" : "spento", st.speed, st.threshold_ms, 2 * st.threshold_ms);
        if (strcmp(text, last)) {
            strcpy(last, text);
            gtk_label_set_text(GTK_LABEL(status_label), text);
        }
    }
    return G_SOURCE_CONTINUE;
}

/* ------------------------------------------------------------------ the touch key */

static void key_update(void) { emu_key(mouse_down || space_down); }

static gboolean on_button_press(GtkWidget *w, GdkEventButton *e, gpointer data) {
    layout_t l;
    double dx, dy;
    (void)data;
    gtk_widget_grab_focus(w);
    get_layout(&l);
    dx = e->x - l.key_x;
    dy = e->y - l.key_y;
    if (e->button == 1 && dx * dx + dy * dy <= (KEY_R + 7) * (KEY_R + 7)) {
        mouse_down = 1;
        key_update();
    }
    return TRUE;
}

static gboolean on_button_release(GtkWidget *w, GdkEventButton *e, gpointer data) {
    (void)w; (void)data;
    if (e->button == 1 && mouse_down) {
        mouse_down = 0;
        key_update();
    }
    return TRUE;
}

/* before the focused widget, so that SPACE never clicks a button; a text box keeps it */
static gboolean on_key(GtkWidget *w, GdkEventKey *e, gpointer data) {
    GtkWidget *focus = gtk_window_get_focus(GTK_WINDOW(w));
    (void)data;
    if (e->keyval != GDK_KEY_space) return FALSE;
    if (e->type == GDK_KEY_PRESS && focus && GTK_IS_EDITABLE(focus)) return FALSE;
    if (e->type == GDK_KEY_RELEASE && !space_down) return FALSE;
    space_down = e->type == GDK_KEY_PRESS;
    key_update();
    return TRUE;
}

static gboolean on_focus_out(GtkWidget *w, GdkEvent *e, gpointer data) {
    (void)w; (void)e; (void)data;
    space_down = 0;
    key_update();
    return FALSE;
}

/* ------------------------------------------------------------------ controls */

static void on_speed(GtkRange *r, gpointer d) { (void)d; EMU_POST(EMU_SPEED, 0, (int)gtk_range_get_value(r)); }

static void type_text(const char *t) { EMU_POST(EMU_TYPE, t, 0); gtk_widget_grab_focus(area); }
static void on_type(GtkWidget *w, gpointer d) { (void)w; (void)d; type_text(gtk_entry_get_text(GTK_ENTRY(e_type))); }
static void on_tan(GtkWidget *w, gpointer d) { (void)w; (void)d; type_text("TAN"); }

static void on_battery(GtkWidget *w, gpointer d) {
    (void)w; (void)d;
    EMU_POST(EMU_BATTERY, 0, (int)gtk_range_get_value(GTK_RANGE(s_mv)),
             gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(c_chg)));
}

static void on_bt(GtkWidget *w, gpointer d) {
    (void)d;
    if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(w)))
        EMU_POST(EMU_CONNECT, gtk_entry_get_text(GTK_ENTRY(e_peer)), (int)gtk_range_get_value(GTK_RANGE(s_rssi)));
    else
        EMU_POST(EMU_DISCONNECT, 0, 0);
}

static void on_rssi(GtkRange *r, gpointer d) { (void)d; EMU_POST(EMU_RSSI, 0, (int)gtk_range_get_value(r)); }

static void on_notify(GtkWidget *w, gpointer d) {
    (void)w; (void)d;
    EMU_POST(EMU_NOTIFY, gtk_entry_get_text(GTK_ENTRY(e_notify)), 0);
    gtk_widget_grab_focus(area);
}

static void on_find(GtkWidget *w, gpointer d) { (void)w; (void)d; EMU_POST(EMU_FIND, 0, 0); }

static void fill_time_now(void) {
    char s[32];
    time_t now = time(NULL);
    struct tm t;
    localtime_r(&now, &t);
    strftime(s, sizeof(s), "%Y-%m-%d %H:%M:%S", &t);
    gtk_entry_set_text(GTK_ENTRY(e_time), s);
}

static void on_set_time(GtkWidget *w, gpointer d) {
    int y, mo, da, h, mi, s;
    (void)w; (void)d;
    if (sscanf(gtk_entry_get_text(GTK_ENTRY(e_time)), "%d-%d-%d %d:%d:%d", &y, &mo, &da, &h, &mi, &s) == 6)
        EMU_POST(EMU_SETTIME, 0, y, mo, da, h, mi, s);
    else
        emu_log("ora non valida: usa AAAA-MM-GG HH:MM:SS");
}

static void on_pc_time(GtkWidget *w, gpointer d) { fill_time_now(); on_set_time(w, d); }

/* ------------------------------------------------------------------ log */

static gboolean append_log(gpointer data) {
    GtkTextBuffer *b = gtk_text_view_get_buffer(GTK_TEXT_VIEW(log_view));
    GtkTextIter end;
    gtk_text_buffer_get_end_iter(b, &end);
    gtk_text_buffer_insert(b, &end, data, -1);
    gtk_text_buffer_insert(b, &end, "\n", -1);
    if (gtk_text_buffer_get_line_count(b) > 3000) {
        GtkTextIter a, z;
        gtk_text_buffer_get_start_iter(b, &a);
        gtk_text_buffer_get_iter_at_line(b, &z, 1000);
        gtk_text_buffer_delete(b, &a, &z);
        gtk_text_buffer_get_end_iter(b, &end);
    }
    gtk_text_view_scroll_to_mark(GTK_TEXT_VIEW(log_view), gtk_text_buffer_get_insert(b), 0, FALSE, 0, 0);
    g_free(data);
    return G_SOURCE_REMOVE;
}

static void log_to_gui(const char *line) {
    puts(line);
    g_idle_add(append_log, g_strdup(line));
}

/* ------------------------------------------------------------------ scripts */

typedef struct { const char *path; volatile int done, rc; } shot_t;

static gboolean do_shot(gpointer data) {
    shot_t *s = data;
    int w = gtk_widget_get_allocated_width(window), h = gtk_widget_get_allocated_height(window);
    cairo_surface_t *out = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h);
    cairo_t *cr = cairo_create(out);
    gtk_widget_draw(window, cr);
    cairo_destroy(cr);
    s->rc = cairo_surface_write_to_png(out, s->path) != CAIRO_STATUS_SUCCESS;
    cairo_surface_destroy(out);
    s->done = 1;
    return G_SOURCE_REMOVE;
}

static int gui_shot(const char *path) {
    shot_t s = {path, 0, 0};
    g_idle_add(do_shot, &s);
    while (!s.done) usleep(2000);
    return s.rc;
}

static void gui_wait(uint32_t ms) { usleep(ms * 1000); }

static gboolean quit_idle(gpointer d) { (void)d; gtk_main_quit(); return G_SOURCE_REMOVE; }
static void gui_quit(int st) { (void)st; g_idle_add(quit_idle, NULL); }

static gpointer script_thread(gpointer data) {
    static const emu_script_host_t host = {gui_wait, gui_shot, gui_quit};
    emu_status_t s;
    (void)data;
    do {
        usleep(10000);
        emu_status(&s);
    } while (!s.booted);
    if (emu_script_run(script_path, &host)) exit(1);
    return NULL;
}

/* ------------------------------------------------------------------ window */

static GtkWidget *add_frame(GtkWidget *parent, const char *title) {
    GtkWidget *f = gtk_frame_new(title), *b = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_container_set_border_width(GTK_CONTAINER(b), 6);
    gtk_container_add(GTK_CONTAINER(f), b);
    gtk_box_pack_start(GTK_BOX(parent), f, FALSE, FALSE, 0);
    return b;
}

static GtkWidget *add_scale(GtkWidget *box, double min, double max, double val, double step) {
    GtkWidget *s = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, min, max, step);
    gtk_scale_set_digits(GTK_SCALE(s), 0);
    gtk_range_set_value(GTK_RANGE(s), val);
    gtk_box_pack_start(GTK_BOX(box), s, FALSE, FALSE, 0);
    return s;
}

static GtkWidget *add_entry_button(GtkWidget *box, const char *text, const char *label, GCallback cb) {
    GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4), *e = gtk_entry_new(),
              *b = gtk_button_new_with_label(label);
    gtk_entry_set_text(GTK_ENTRY(e), text);
    gtk_box_pack_start(GTK_BOX(row), e, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(row), b, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), row, FALSE, FALSE, 0);
    g_signal_connect(b, "clicked", cb, NULL);
    g_signal_connect(e, "activate", cb, NULL);
    return e;
}

static GtkWidget *add_button(GtkWidget *box, const char *label, GCallback cb) {
    GtkWidget *b = gtk_button_new_with_label(label);
    gtk_box_pack_start(GTK_BOX(box), b, FALSE, FALSE, 0);
    g_signal_connect(b, "clicked", cb, NULL);
    return b;
}

static GtkWidget *build_controls(void) {
    GtkWidget *col = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6), *b;

    b = add_frame(col, "Morse");
    s_speed = add_scale(b, 1, 16, 9, 1);
    e_type = add_entry_button(b, "SOS", "Digita", G_CALLBACK(on_type));
    add_button(b, "Digita TAN (apri il menu)", G_CALLBACK(on_tan));

    b = add_frame(col, "Orologio");
    e_time = add_entry_button(b, "", "Imposta", G_CALLBACK(on_set_time));
    fill_time_now();
    add_button(b, "Usa l'ora del PC", G_CALLBACK(on_pc_time));

    b = add_frame(col, "Batteria (mV)");
    s_mv = add_scale(b, 3300, 4200, 3900, 10);
    c_chg = gtk_check_button_new_with_label("In carica");
    gtk_box_pack_start(GTK_BOX(b), c_chg, FALSE, FALSE, 0);

    b = add_frame(col, "Bluetooth");
    c_bt = gtk_check_button_new_with_label("Connesso");
    gtk_box_pack_start(GTK_BOX(b), c_bt, FALSE, FALSE, 0);
    e_peer = gtk_entry_new();
    gtk_entry_set_text(GTK_ENTRY(e_peer), "Pixel");
    gtk_box_pack_start(GTK_BOX(b), e_peer, FALSE, FALSE, 0);
    s_rssi = add_scale(b, -100, -40, -60, 1);

    b = add_frame(col, "Telefono");
    e_notify = add_entry_button(b, "CIAO DA MORSEOS", "Invia", G_CALLBACK(on_notify));
    add_button(b, "Trova la band", G_CALLBACK(on_find));

    g_signal_connect(s_speed, "value-changed", G_CALLBACK(on_speed), NULL);
    g_signal_connect(s_mv, "value-changed", G_CALLBACK(on_battery), NULL);
    g_signal_connect(c_chg, "toggled", G_CALLBACK(on_battery), NULL);
    g_signal_connect(c_bt, "toggled", G_CALLBACK(on_bt), NULL);
    g_signal_connect(s_rssi, "value-changed", G_CALLBACK(on_rssi), NULL);
    return col;
}

int gui_run(const emu_opts_t *o, const char *script, int sc, int argc, char **argv) {
    GtkWidget *root, *left, *hint, *scroll;
    layout_t l;
    int pw, ph;

    opts = *o;
    scale = sc;
    script_path = script;
    gtk_init(&argc, &argv);
    emu_panel_init(o->width, o->height);
    emu_panel_size(&pw, &ph);
    panel_surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, pw, ph);

    window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(window), "MorseOS Emulator");
    g_signal_connect(window, "destroy", G_CALLBACK(gtk_main_quit), NULL);
    g_signal_connect(window, "key-press-event", G_CALLBACK(on_key), NULL);
    g_signal_connect(window, "key-release-event", G_CALLBACK(on_key), NULL);
    g_signal_connect(window, "focus-out-event", G_CALLBACK(on_focus_out), NULL);

    root = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_container_set_border_width(GTK_CONTAINER(root), 10);
    gtk_container_add(GTK_CONTAINER(window), root);

    left = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    get_layout(&l);
    area = gtk_drawing_area_new();
    gtk_widget_set_size_request(area, l.w, l.h);
    gtk_widget_set_can_focus(area, TRUE);
    gtk_widget_add_events(area, GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK);
    g_signal_connect(area, "draw", G_CALLBACK(on_draw), NULL);
    g_signal_connect(area, "button-press-event", G_CALLBACK(on_button_press), NULL);
    g_signal_connect(area, "button-release-event", G_CALLBACK(on_button_release), NULL);
    gtk_box_pack_start(GTK_BOX(left), area, FALSE, FALSE, 0);
    hint = gtk_label_new("Tasto touch: tieni premuto SPAZIO\no clicca il cerchio sotto lo schermo");
    gtk_label_set_justify(GTK_LABEL(hint), GTK_JUSTIFY_CENTER);
    gtk_box_pack_start(GTK_BOX(left), hint, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(root), left, FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(root), build_controls(), FALSE, FALSE, 0);

    left = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    status_label = gtk_label_new("Avvio...");
    gtk_label_set_xalign(GTK_LABEL(status_label), 0);
    gtk_box_pack_start(GTK_BOX(left), status_label, FALSE, FALSE, 0);
    log_view = gtk_text_view_new();
    gtk_text_view_set_editable(GTK_TEXT_VIEW(log_view), FALSE);
    gtk_text_view_set_cursor_visible(GTK_TEXT_VIEW(log_view), FALSE);
    gtk_text_view_set_monospace(GTK_TEXT_VIEW(log_view), TRUE);
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(log_view), GTK_WRAP_WORD_CHAR);
    gtk_widget_set_can_focus(log_view, FALSE);
    scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_widget_set_size_request(scroll, 430, -1);
    gtk_container_add(GTK_CONTAINER(scroll), log_view);
    gtk_box_pack_start(GTK_BOX(left), scroll, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(root), left, TRUE, TRUE, 0);

    gtk_widget_show_all(window);
    gtk_widget_grab_focus(area);

    emu_log_sink(log_to_gui);
    g_timeout_add(16, refresh, NULL);
    g_thread_new("firmware", firmware_thread, NULL);
    if (script) g_thread_new("script", script_thread, NULL);
    gtk_main();
    return 0;
}
