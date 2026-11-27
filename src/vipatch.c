/* VI-Patch Payload -- applies VI-Patch patches to Grand Theft Auto VI (PPSA38500 v01.002.001) without PS Patch System.
 *
 * Patches: 60 FPS, First person (R3 + D-pad Right), No Vignette, No depth of field, Skip intro logos, Weather fix.
 *
 * Send it BEFORE starting the game (needed for Skip intro logos). It waits up to 10 minutes for GTA VI, checks the
 * original 01.002.001 bytes of every line, writes each patch only if all its lines match (cave code first, the jump
 * into it last), reads it back and exits. A patch already in place is left alone; anything else is skipped and logged.
 * Log: /data/vi-patch/payload.log
 *
 * 60 FPS here does not need 120 Hz forced: the game's own frame-rate setters are set to 60 (RVA 0x3f73e51, 0x40d2be1),
 * plus the frame-time floor fix. Research: HSgod's ps5-fpsunlocker-gta6 (offsets and how the cap works).
 * Code writes go through the game's page tables and the kernel direct map (code pages are not writable otherwise).
 */
#include <errno.h>
#include <signal.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/sysctl.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <sys/user.h>
#include <ps5/kernel.h>
#include <ps5/klog.h>
#ifdef PRIVATE
#include "patches_private.h"
#else
#include "patches.h"
#endif

#define VERSION   "1.2"
#define DIR_      "/data/vi-patch"
#define TITLE     "PPSA38500"
#define LOCK_PORT 47991
#define FPS       60

typedef struct { char pad[45]; char msg[3075]; } notify_t;
int sceKernelSendNotificationRequest(int, notify_t *, size_t, int);
int sceKernelGetAppInfo(pid_t pid, void *info);

static FILE *g_log;
static void lg(const char *fmt, ...) {
    char b[1024]; va_list ap; va_start(ap, fmt); vsnprintf(b, sizeof b, fmt, ap); va_end(ap);
    if (g_log) { fprintf(g_log, "%s\n", b); fflush(g_log); }
    klog_printf("[vi-patch] %s\n", b);
}
static void notify(const char *fmt, ...) {
    notify_t n; va_list ap; memset(&n, 0, sizeof n);
    strcpy(n.msg, "VI-Patch: ");
    va_start(ap, fmt); vsnprintf(n.msg + strlen(n.msg), 2000, fmt, ap); va_end(ap);
    lg("NOTE %s", n.msg);
    sceKernelSendNotificationRequest(0, &n, sizeof n, 0);
}

/* ------------------------------------------------------------ game process */
static int title_of(pid_t pid, char out[16]) {
    uint8_t info[0x100]; size_t i, k;
    memset(info, 0, sizeof info);
    if (sceKernelGetAppInfo(pid, info)) return -1;
    for (i = 0; i + 9 <= sizeof info; i++) {
        for (k = 0; k < 9; k++) {
            uint8_t c = info[i + k];
            if (k < 4 ? (c < 'A' || c > 'Z') : (c < '0' || c > '9')) break;
        }
        if (k == 9) { memcpy(out, info + i, 9); out[9] = 0; return 0; }
    }
    return -1;
}
static pid_t find_gta(void) {
    int mib[4] = {CTL_KERN, KERN_PROC, KERN_PROC_PROC, 0};
    size_t sz = 0; uint8_t *buf, *p; pid_t found = -1; char tid[16];
    if (sysctl(mib, 4, NULL, &sz, NULL, 0) || !(buf = malloc(sz))) return -1;
    if (sysctl(mib, 4, buf, &sz, NULL, 0)) { free(buf); return -1; }
    for (p = buf; p < buf + sz;) {
        struct kinfo_proc *ki = (struct kinfo_proc *)p;
        if (ki->ki_structsize <= 0) break;
        p += ki->ki_structsize;
        if (ki->ki_pid <= 0 || strncmp(ki->ki_comm, "eboot.bin", 9)) continue;
        if (!title_of(ki->ki_pid, tid) && !strcmp(tid, TITLE)) { found = ki->ki_pid; break; }
    }
    free(buf);
    return found;
}
static uint64_t eboot_base(pid_t pid) {
    uint32_t h;
    if (kernel_dynlib_handle(pid, "eboot.bin", &h)) return 0;
    intptr_t b = kernel_dynlib_mapbase_addr(pid, h);
    return b > 0 ? (uint64_t)b : 0;
}

/* ------------------------------------------------------------ physical memory access (page walk + direct map) */
static uint64_t g_dmap, g_cr3;
static uint64_t proc_cr3(pid_t pid) {
    intptr_t proc = kernel_get_proc(pid);
    if (!proc || !KERNEL_OFFSET_VMSPACE_VM_PMAP) return 0;
    uint64_t vms = (uint64_t)kernel_getlong(proc + KERNEL_OFFSET_PROC_P_VMSPACE);
    uint64_t d[2];
    if (!vms || kernel_copyout((intptr_t)(vms + KERNEL_OFFSET_VMSPACE_VM_PMAP + 32), d, sizeof d)) return 0;
    g_dmap = d[0] - d[1];
    return d[1];
}
static uint64_t v2p(uint64_t va, uint64_t *lim) {
    uint64_t e = g_cr3 & 0x000ffffffffff000ull;
    for (int sh = 39; sh >= 12; sh -= 9) {
        e = (uint64_t)kernel_getlong((intptr_t)(g_dmap + e + ((va >> sh) & 0x1ff) * 8));
        if (!(e & 1)) return (uint64_t)-1;
        if ((e & 0x80) || sh == 12) {
            e &= (1ull << 52) - (1ull << sh);
            e |= va & ((1ull << sh) - 1);
            if (lim) *lim = (e | ((1ull << sh) - 1)) + 1;
            return e;
        }
        e &= 0x000ffffffffff000ull;
    }
    return (uint64_t)-1;
}
static int phys_rw(uint64_t va, void *buf, size_t len, int wr) {
    uint8_t *p = buf;
    while (len) {
        uint64_t lim = 0, pa = v2p(va, &lim);
        if (pa == (uint64_t)-1) return -1;
        size_t c = (size_t)(lim - pa); if (c > len) c = len;
        int r = wr ? kernel_copyin(p, (intptr_t)(g_dmap + pa), c) : kernel_copyout((intptr_t)(g_dmap + pa), p, c);
        if (r) return -1;
        va += c; p += c; len -= c;
    }
    return 0;
}

/* ------------------------------------------------------------ config: /data/vi-patch/vipatch.txt */
#define CFG DIR_ "/vipatch.txt"
static int g_on[32];
static void load_config(int n) {
    for (int i = 0; i < n; i++) g_on[i] = GROUPS[i].def;
    FILE *f = fopen(CFG, "r");
    if (!f) {
        f = fopen(CFG, "w");
        if (f) {
            fprintf(f, "# VI-Patch Payload " VERSION ": 1 = on, 0 = off. Edit over FTP, then send the payload again\n"
                       "# before you start the game.\n"
                       "# 1080p = render at 1080p instead of 1440p (use with 60 FPS for a steadier frame rate).\n"
                       "# First person: R3 + D-pad Right. No clothing: R3 + D-pad Down.\n\n");
            for (int i = 0; i < n; i++) fprintf(f, "%s=%d\n", GROUPS[i].name, GROUPS[i].def);
            fclose(f);
        }
        lg("no config: wrote defaults to " CFG);
        return;
    }
    char b[256]; int seen[32] = {0}, lastnl = 1;
    while (fgets(b, sizeof b, f)) {
        size_t bl = strlen(b); lastnl = bl && b[bl - 1] == '\n';
        char *eq = strchr(b, '=');
        if (b[0] == '#' || !eq) continue;
        *eq = 0;
        char *k = b, *e = eq - 1;
        while (*k == ' ' || *k == '\t') k++;
        while (e >= k && (*e == ' ' || *e == '\t')) *e-- = 0;
        int i;
        for (i = 0; i < n; i++) if (!strcasecmp(k, GROUPS[i].name)) { g_on[i] = atoi(eq + 1) != 0; seen[i] = 1; break; }
        if (i == n) lg("config: unknown patch '%s'", k);
    }
    fclose(f);
    /* a newer payload has patches the file does not list yet: add them with their default */
    for (int i = 0; i < n; i++) {
        if (seen[i]) continue;
        if ((f = fopen(CFG, "a"))) {
            if (!lastnl) { fputc('\n', f); lastnl = 1; }
            fprintf(f, "%s=%d\n", GROUPS[i].name, GROUPS[i].def);
            fclose(f);
        }
        lg("config: added new patch line '%s=%d'", GROUPS[i].name, GROUPS[i].def);
    }
}

/* ------------------------------------------------------------ patching */
static int hexb(const char *h, uint8_t *o, int max) {
    int n = 0; unsigned v;
    while (h[0] && h[1] && n < max && sscanf(h, "%2x", &v) == 1) { o[n++] = (uint8_t)v; h += 2; }
    return n;
}
enum { S_ORIG, S_DONE, S_MIXED, S_FAIL };
static int group_state(uint64_t base, const struct pgroup *g) {
    int orig = 0, done = 0;
    for (int i = 0; i < g->n; i++) {
        static uint8_t cur[2048], o[2048], nw[2048];
        const struct pline *l = &g->l[i];
        hexb(l->orig, o, sizeof o); hexb(l->new_, nw, sizeof nw);
        if (phys_rw(base + l->rva, cur, l->n, 0)) return S_FAIL;
        if (!memcmp(cur, o, l->n)) orig++;
        else if (!memcmp(cur, nw, l->n)) done++;
    }
    return orig == g->n ? S_ORIG : done == g->n ? S_DONE : S_MIXED;
}
static int apply_group(uint64_t base, const struct pgroup *g) {
    for (int i = 0; i < g->n; i++) {           /* table order: caves first, then the jumps into them */
        static uint8_t nw[2048], chk[2048];
        const struct pline *l = &g->l[i];
        hexb(l->new_, nw, sizeof nw);
        if (phys_rw(base + l->rva, nw, l->n, 1) || phys_rw(base + l->rva, chk, l->n, 0) || memcmp(chk, nw, l->n)) {
            lg("  %s: write at rva %x FAILED", g->name, l->rva);
            return -1;
        }
    }
    return 0;
}

int main(void) {
    struct sockaddr_in sa; int lock = socket(AF_INET, SOCK_DGRAM, 0);
    memset(&sa, 0, sizeof sa); sa.sin_family = AF_INET; sa.sin_port = htons(LOCK_PORT);
    sa.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (lock < 0 || bind(lock, (struct sockaddr *)&sa, sizeof sa)) { notify("already running (waiting for GTA VI). It reads vipatch.txt again when the game starts"); return 1; }
    syscall(SYS_thr_set_name, -1, "vi-patch.elf");
    mkdir(DIR_, 0777);
    g_log = fopen(DIR_ "/payload.log", "w");
    lg("VI-Patch Payload v%s fw %08x", VERSION, kernel_get_fw_version());

    int ng = sizeof GROUPS / sizeof GROUPS[0];
    load_config(ng);
    for (int i = 0; i < ng; i++) lg("%s=%d", GROUPS[i].name, g_on[i]);

    pid_t pid = find_gta();
    int running_at_start = pid > 0;
    if (pid < 0) notify("ready. Start GTA VI now");
    for (int i = 0; i < 30000 && pid < 0; i++) { pid = find_gta(); if (pid < 0) usleep(20000); }   /* 10 min */
    if (pid < 0) { notify("GTA VI was not started in 10 minutes"); return 2; }
    if (!running_at_start) {                 /* the game just started: read vipatch.txt again (edited while we waited) */
        load_config(ng);
        for (int i = 0; i < ng; i++) lg("now %s=%d", GROUPS[i].name, g_on[i]);
    }
    uint64_t base = 0;
    for (int i = 0; i < 4000 && !(base = eboot_base(pid)); i++) usleep(5000);
    for (int i = 0; i < 1000 && !(g_cr3 = proc_cr3(pid)); i++) usleep(10000);
    lg("GTA pid %d base 0x%llx cr3 %llx", pid, (unsigned long long)base, (unsigned long long)g_cr3);
    if (!base || !g_cr3) { notify("could not reach the game"); return 3; }

    int n = sizeof GROUPS / sizeof GROUPS[0], ok = 0, already = 0, skipped = 0;
    int st[32];
    /* wait until the code is loaded: every group either original or already patched (max 60 s) */
    for (int t = 0; t < 3000; t++) {
        int ready = 1;
        for (int i = 0; i < n; i++) { if (!g_on[i]) continue; st[i] = group_state(base, &GROUPS[i]); if (st[i] == S_FAIL || st[i] == S_MIXED) ready = 0; }
        if (ready) break;
        usleep(20000);
    }
    for (int i = 0; i < n; i++) {
        const struct pgroup *g = &GROUPS[i];
        if (!g_on[i]) continue;
        if (st[i] == S_ORIG) st[i] = group_state(base, g);   /* re-check: an earlier patch may share these bytes */
        if (st[i] == S_DONE) { lg("%s: already in place", g->name); already++; continue; }
        if (st[i] != S_ORIG) { lg("%s: SKIPPED (bytes are not 01.002.001 originals, state %d)", g->name, st[i]); skipped++; continue; }
        if (apply_group(base, g)) { skipped++; continue; }
        lg("%s: applied", g->name); ok++;
        if (!strcmp(g->name, "60 FPS")) {        /* game already running: refresh the frame time it computed at 30 */
            uint32_t us = 1000000 / FPS;
            phys_rw(base + 0xe3be220, &us, 4, 1);
        }
    }
    if (skipped) notify("%d patches applied, %d skipped (see /data/vi-patch/payload.log)", ok + already, skipped);
    else notify("%d patches applied", ok + already);
    lg("done: %d applied, %d already, %d skipped", ok, already, skipped);
    return 0;
}
