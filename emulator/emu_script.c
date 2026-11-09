/* Script runner: one command per line, see tests/smoke.emu. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "emu.h"

static void wait_typing(const emu_script_host_t *h) {
    emu_status_t s;
    do {
        h->wait_ms(5);
        emu_status(&s);
    } while (s.typing);
}

static int fail(const char *path, int n, const char *what) {
    fprintf(stderr, "%s:%d: %s\n", path, n, what);
    return 1;
}

int emu_script_run(const char *path, const emu_script_host_t *h) {
    char line[256];
    int n = 0;
    FILE *f = fopen(path, "r");
    if (!f) {
        perror(path);
        return 1;
    }
    while (fgets(line, sizeof(line), f)) {
        char *cmd, *arg;
        int a, b, y, mo, d, ho, mi, se;
        n++;
        line[strcspn(line, "\r\n")] = 0;
        cmd = line + strspn(line, " \t");
        if (!*cmd || *cmd == '#') continue;
        arg = cmd + strcspn(cmd, " \t");
        if (*arg) *arg++ = 0;
        arg += strspn(arg, " \t");

        if (!strcmp(cmd, "wait")) h->wait_ms(atoi(arg));
        else if (!strcmp(cmd, "down")) emu_key(1);
        else if (!strcmp(cmd, "up")) emu_key(0);
        else if (!strcmp(cmd, "press")) {
            emu_key(1);
            h->wait_ms(atoi(arg));
            emu_key(0);
            h->wait_ms(5);
        } else if (!strcmp(cmd, "type")) {
            EMU_POST(EMU_TYPE, arg, 0);
            wait_typing(h);
        } else if (!strcmp(cmd, "speed")) EMU_POST(EMU_SPEED, 0, atoi(arg));
        else if (!strcmp(cmd, "notify")) EMU_POST(EMU_NOTIFY, arg, 0);
        else if (!strcmp(cmd, "find")) EMU_POST(EMU_FIND, 0, 0);
        else if (!strcmp(cmd, "disconnect")) EMU_POST(EMU_DISCONNECT, 0, 0);
        else if (!strcmp(cmd, "connect")) {
            char name[20];
            if (sscanf(arg, "%19s %d", name, &a) != 2) return fail(path, n, "connect NOME DBM");
            EMU_POST(EMU_CONNECT, name, a);
        } else if (!strcmp(cmd, "battery")) {
            if (sscanf(arg, "%d %d", &a, &b) != 2) return fail(path, n, "battery MV 0|1");
            EMU_POST(EMU_BATTERY, 0, a, b);
        } else if (!strcmp(cmd, "time")) {
            if (sscanf(arg, "%d-%d-%d %d:%d:%d", &y, &mo, &d, &ho, &mi, &se) != 6)
                return fail(path, n, "time AAAA-MM-GG HH:MM:SS");
            EMU_POST(EMU_SETTIME, 0, y, mo, d, ho, mi, se);
        } else if (!strcmp(cmd, "shot")) {
            h->wait_ms(1);
            if (h->shot(arg)) return fail(path, n, "cannot write the screenshot");
        } else if (!strcmp(cmd, "expect")) {
            if (!emu_log_expect(arg)) return fail(path, n, "not in the log");
        } else if (!strcmp(cmd, "expect-screen")) {
            emu_status_t s;
            h->wait_ms(1);
            emu_status(&s);
            if (s.screen_on != !strcmp(arg, "on")) return fail(path, n, "wrong screen state");
        } else if (!strcmp(cmd, "quit")) {
            fclose(f);
            h->quit(0);
            return 0;
        } else {
            return fail(path, n, "unknown command");
        }
    }
    fclose(f);
    return 0;
}
