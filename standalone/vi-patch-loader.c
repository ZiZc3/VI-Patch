/* SPDX-License-Identifier: GPL-3.0-only
 *
 * vi-patch-loader: applies the VI-Patch patches (ZiZc3) to a running
 * Grand Theft Auto VI (PPSA38500 01.002.001) without PS Patch System.
 *
 * Send it to the console, then start the game within 10 minutes. It finds the
 * game, checks every line of every enabled patch against the game's original
 * bytes, writes the patches whose bytes all match, reads them back and exits.
 * A patch whose bytes differ (another game version, or memory already changed
 * by another payload) is left untouched.
 *
 * Config: /data/vi-patch/config.ini (written with defaults on first run).
 * Log:    /data/vi-patch/log.txt
 *
 * Game lookup and the DMAP write (ptwalk.c) come from HSgod's
 * ps5-fpsunlocker-gta6; ptwalk.c is a port of ps5debug-NG by OpenSourcereR.
 */
#include <ctype.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/sysctl.h>
#include <sys/types.h>
#include <sys/user.h>
#include <ps5/kernel.h>
#include <ps5/mdbg.h>
#include "ptwalk.h"
#include "patches.h"

#define LOADER_VERSION "1.0"
#define DIR_PATH    "/data/vi-patch"
#define CONFIG_PATH DIR_PATH "/config.ini"
#define LOG_PATH    DIR_PATH "/log.txt"
#define WAIT_SECONDS 600
#define IMAGE_BASE  0x400000ULL /* eboot.bin link address the XML uses */
#define MAX_LINE    0x1000

typedef struct notify_request {
    char useless1[45];
    char message[3075];
} notify_request_t;
int sceKernelSendNotificationRequest(int, notify_request_t *, size_t, int);
int sceKernelGetAppInfo(pid_t pid, void *info);

static FILE *g_log;
static int g_enabled[NUM_PATCHES];
static int g_dump;

static void logmsg(const char *fmt, ...) {
    va_list ap;
    if (!g_log)
        return;
    fprintf(g_log, "%ld ", (long)time(NULL));
    va_start(ap, fmt);
    vfprintf(g_log, fmt, ap);
    va_end(ap);
    fputc('\n', g_log);
    fflush(g_log);
}

static void notify(const char *fmt, ...) {
    notify_request_t req;
    va_list ap;
    memset(&req, 0, sizeof(req));
    strcpy(req.message, "VI-Patch: ");
    va_start(ap, fmt);
    vsnprintf(req.message + strlen(req.message),
              sizeof(req.message) - strlen(req.message), fmt, ap);
    va_end(ap);
    logmsg("NOTE %s", req.message);
    sceKernelSendNotificationRequest(0, &req, sizeof(req), 0);
}

/* ---- config ---------------------------------------------------------- */

static char *trim(char *s) {
    char *e;
    while (isspace((unsigned char)*s))
        s++;
    e = s + strlen(s);
    while (e > s && isspace((unsigned char)e[-1]))
        *--e = 0;
    return s;
}

static void write_default_config(void) {
    FILE *f = fopen(CONFIG_PATH, "w");
    if (!f)
        return;
    fprintf(f, "# VI-Patch standalone loader %s. 1 = on, 0 = off.\n"
               "# Change a value, then send the loader again before starting the game.\n\n",
            LOADER_VERSION);
    for (size_t i = 0; i < NUM_PATCHES; i++)
        fprintf(f, "%s=%d\n", patches[i].name, patches[i].default_on);
    fprintf(f, "\n# 1 = only log the bytes at every patch address, change nothing.\n"
               "dump=0\n");
    fclose(f);
}

static void read_config(void) {
    char buf[256];
    FILE *f;
    for (size_t i = 0; i < NUM_PATCHES; i++)
        g_enabled[i] = patches[i].default_on;
    if (!(f = fopen(CONFIG_PATH, "r"))) {
        write_default_config();
        logmsg("no config, wrote defaults to %s", CONFIG_PATH);
        return;
    }
    while (fgets(buf, sizeof(buf), f)) {
        char *eq, *key, *val;
        if (buf[0] == '#' || !(eq = strchr(buf, '=')))
            continue;
        *eq = 0;
        key = trim(buf);
        val = trim(eq + 1);
        if (!strcasecmp(key, "dump")) {
            g_dump = atoi(val);
            continue;
        }
        size_t i;
        for (i = 0; i < NUM_PATCHES; i++)
            if (!strcasecmp(key, patches[i].name)) {
                g_enabled[i] = atoi(val) != 0;
                break;
            }
        if (i == NUM_PATCHES)
            logmsg("config: unknown patch \"%s\", ignored", key);
    }
    fclose(f);
}

/* ---- game lookup ----------------------------------------------------- */

/* Returns the pid of a running eboot.bin whose title id is GAME_TITLE_ID. */
static pid_t find_game(void) {
    int mib[4] = {CTL_KERN, KERN_PROC, KERN_PROC_PROC, 0};
    size_t size = 0;
    uint8_t *buf, *p;
    pid_t found = -1;
    if (sysctl(mib, 4, NULL, &size, NULL, 0) || !(buf = malloc(size)))
        return -1;
    if (sysctl(mib, 4, buf, &size, NULL, 0)) {
        free(buf);
        return -1;
    }
    for (p = buf; p < buf + size && found < 0;) {
        struct kinfo_proc *ki = (struct kinfo_proc *)p;
        uint8_t info[0x100];
        if (ki->ki_structsize <= 0)
            break;
        p += ki->ki_structsize;
        if (strncmp(ki->ki_comm, "eboot.bin", 9) || ki->ki_pid <= 0)
            continue;
        memset(info, 0, sizeof(info));
        if (sceKernelGetAppInfo(ki->ki_pid, info))
            continue;
        for (size_t i = 0; i + 9 <= sizeof(info); i++)
            if (!memcmp(info + i, GAME_TITLE_ID, 9)) {
                found = ki->ki_pid;
                break;
            }
    }
    free(buf);
    return found;
}

static int eboot_base(pid_t pid, uint64_t *base) {
    uint32_t handle;
    intptr_t b;
    if (kernel_dynlib_handle(pid, "eboot.bin", &handle))
        return -1;
    b = kernel_dynlib_mapbase_addr(pid, handle);
    if (b <= 0)
        return -1;
    *base = (uint64_t)b;
    return 0;
}

/* ---- patching -------------------------------------------------------- */

enum { ST_ORIG, ST_DONE, ST_OTHER, ST_READFAIL };

static uint64_t line_va(uint64_t base, const struct line *l) {
    return base + (l->addr - IMAGE_BASE);
}

static void log_hex(const char *what, const struct patch *p, const struct line *l,
                    const uint8_t *b) {
    char hex[2 * 32 + 3];
    uint32_t n = l->len < 32 ? l->len : 32;
    for (uint32_t i = 0; i < n; i++)
        snprintf(hex + 2 * i, 3, "%02x", b[i]);
    if (l->len > n)
        strcat(hex, "..");
    logmsg("  %s %s 0x%08lx %u: %s", what, p->name, (long)l->addr, l->len, hex);
}

static int patch_state(pid_t pid, uint64_t base, const struct patch *p, int verbose) {
    static uint8_t cur[MAX_LINE];
    int orig = 0, done = 0;
    for (int i = 0; i < p->n; i++) {
        const struct line *l = &p->lines[i];
        if (l->len > sizeof(cur) ||
            mdbg_copyout(pid, (intptr_t)line_va(base, l), cur, l->len))
            return ST_READFAIL;
        if (!memcmp(cur, l->orig, l->len)) {
            orig++;
            if (verbose)
                logmsg("  original  %s 0x%08lx", p->name, (long)l->addr);
        } else if (!memcmp(cur, l->new_, l->len)) {
            done++;
            if (verbose)
                logmsg("  patched   %s 0x%08lx", p->name, (long)l->addr);
        } else if (verbose)
            log_hex("other    ", p, l, cur);
    }
    if (orig == p->n)
        return ST_ORIG;
    if (done == p->n)
        return ST_DONE;
    return ST_OTHER;
}

/* The game may not have every page readable the moment it appears. */
static int patch_state_retry(pid_t pid, uint64_t base, const struct patch *p) {
    int st = ST_READFAIL;
    for (int t = 0; t < 50 && (st = patch_state(pid, base, p, 0)) == ST_READFAIL; t++)
        usleep(200000);
    return st;
}

/* Logs the current bytes in originals.txt format, ready to paste. */
static void dump_patch(pid_t pid, uint64_t base, const struct patch *p) {
    static uint8_t cur[MAX_LINE];
    if (!g_log)
        return;
    logmsg("# %s", p->name);
    for (int i = 0; i < p->n; i++) {
        const struct line *l = &p->lines[i];
        uint32_t zero = 0;
        if (l->len > sizeof(cur) ||
            mdbg_copyout(pid, (intptr_t)line_va(base, l), cur, l->len)) {
            logmsg("# read failed at 0x%08lx", (long)l->addr);
            continue;
        }
        while (zero < l->len && !cur[zero])
            zero++;
        fprintf(g_log, "0x%08lx %4u  ", (long)l->addr, l->len);
        if (zero == l->len)
            fputs("zero", g_log);
        else
            for (uint32_t j = 0; j < l->len; j++)
                fprintf(g_log, "%02x", cur[j]);
        fputc('\n', g_log);
        fflush(g_log);
    }
}

/* Code pages refuse the debug write on recent firmware, so write through the
 * direct map first and fall back to the debug write. Returns 1 on success. */
static int write_line(pid_t pid, uint64_t base, const struct line *l, const uint8_t *bytes) {
    uint64_t va = line_va(base, l);
    if (ptwalk_write((uint32_t)pid, va, bytes, l->len) == PTWALK_OK)
        return 1;
    return mdbg_copyin(pid, bytes, (intptr_t)va, l->len) == 0;
}

/* Lines are already in write order (see gen_patches.py): caves first, then
 * the hooks and code edits last to first. A failed write undoes the lines
 * written so far, in reverse. Returns 1 when applied, 2 when it was
 * already in place, 0 when skipped or failed. */
static int apply_patch(pid_t pid, uint64_t base, const struct patch *p) {
    int st = patch_state_retry(pid, base, p);
    if (st == ST_DONE) {
        logmsg("%s: already applied", p->name);
        return 2;
    }
    if (st == ST_READFAIL) {
        logmsg("%s: could not read the game's memory, skipped", p->name);
        return 0;
    }
    if (st == ST_OTHER) {
        logmsg("%s: not every line holds the original bytes, skipped:", p->name);
        patch_state(pid, base, p, 1);
        return 0;
    }
    for (int i = 0; i < p->n; i++) {
        if (write_line(pid, base, &p->lines[i], p->lines[i].new_))
            continue;
        logmsg("%s: write failed at 0x%08lx, restoring the lines written so far",
               p->name, (long)p->lines[i].addr);
        for (; i >= 0; i--) /* line i too: it may be half written */
            if (!write_line(pid, base, &p->lines[i], p->lines[i].orig))
                logmsg("%s: could not restore 0x%08lx", p->name, (long)p->lines[i].addr);
        return 0;
    }
    st = patch_state(pid, base, p, 0);
    logmsg("%s: %s", p->name, st == ST_DONE ? "applied and verified" : "verify FAILED:");
    if (st != ST_DONE)
        patch_state(pid, base, p, 1);
    return st == ST_DONE;
}

int main(void) {
    pid_t pid = -1;
    uint64_t base = 0;
    int on = 0, ok = 0, already = 0;

    mkdir(DIR_PATH, 0777);
    g_log = fopen(LOG_PATH, "w");
    logmsg("vi-patch-loader %s, firmware 0x%x, game %s %s", LOADER_VERSION,
           kernel_get_fw_version(), GAME_TITLE_ID, patches[0].app_ver);
    read_config();
    for (size_t i = 0; i < NUM_PATCHES; i++) {
        logmsg("  %-22s %s", patches[i].name, g_enabled[i] ? "on" : "off");
        on += g_enabled[i];
    }
    if (!on && !g_dump) {
        notify("every patch is off in %s", CONFIG_PATH);
        return 0;
    }
    notify("ready, start GTA VI within 10 minutes");

    /* Poll often: Skip intro logos only helps if it lands before the logos. */
    for (int t = 0; t < WAIT_SECONDS * 5 && pid < 0; t++) {
        pid = find_game();
        if (pid < 0)
            usleep(200000);
    }
    if (pid < 0) {
        notify("GTA VI did not start within 10 minutes, nothing changed");
        return 0;
    }
    for (int t = 0; t < 100 && eboot_base(pid, &base); t++)
        usleep(100000);
    if (!base) {
        notify("could not find the game in memory, nothing changed");
        return 0;
    }
    logmsg("game pid %d, eboot.bin at 0x%lx", pid, (long)base);

    if (g_dump) {
        for (size_t i = 0; i < NUM_PATCHES; i++)
            dump_patch(pid, base, &patches[i]);
        notify("dump written to %s, nothing changed", LOG_PATH);
        return 0;
    }

    char failed[512] = "";
    for (size_t i = 0; i < NUM_PATCHES; i++) {
        if (!g_enabled[i])
            continue;
        int r = apply_patch(pid, base, &patches[i]);
        if (r == 1)
            ok++;
        else if (r == 2)
            already++;
        else
            snprintf(failed + strlen(failed), sizeof(failed) - strlen(failed),
                     "%s%s", failed[0] ? ", " : "", patches[i].name);
    }
    if (ok + already == on)
        notify("%d applied, %d already in place", ok, already);
    else
        notify("%d applied, %d already in place, skipped: %s (see %s)",
               ok, already, failed, LOG_PATH);
    if (g_log)
        fclose(g_log);
    return 0;
}
