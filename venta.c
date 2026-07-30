#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <termios.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <time.h>
#include <math.h>
#include <ctype.h>
#include <stdarg.h>
#include <sys/wait.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#ifndef VENTA_VERSION
#define VENTA_VERSION "dev"
#endif

static const char *bases = "ATCG";
static const char *comp  = "TAGC";
static const char *rungs[] = {"─","─","═","─","─","╌","─","═","─","╌"};
static const int nrung = sizeof(rungs)/sizeof(rungs[0]);
static const char *corrupt_chars[] = {"#","?","@","%","&","X","!","/","S","\\"};
static const int ncorr = sizeof(corrupt_chars)/sizeof(corrupt_chars[0]);
static const char *shards[] = {"·","*","+","x","~"};
static const int nsh = sizeof(shards)/sizeof(shards[0]);
static const char *crystal_chars[] = {"◆","◈","◇","◊","▪","▫","◉","○"};
static const int ncrys = sizeof(crystal_chars)/sizeof(crystal_chars[0]);

static const char default_config[] =
"{\n"
"  \"dna_color\": \"#00ffcc\",\n"
"  \"corrupt_color\": \"#ff0000\",\n"
"  \"recover_color\": \"#00ff50\",\n"
"  \"show_stats\": true\n"
"}\n";

static volatile sig_atomic_t running = 1;
static volatile sig_atomic_t resize_flag = 0;

static void handle_sigint(int sig) { (void)sig; running = 0; }
static void handle_sigwinch(int sig) { (void)sig; resize_flag = 1; }

static void hexcol(char *buf, size_t len, const char *hex, int pct) {
    unsigned int r, g, b;
    sscanf(hex, "%2x%2x%2x", &r, &g, &b);
    if (pct >= 0) {
        r = r * (unsigned)pct / 100;
        g = g * (unsigned)pct / 100;
        b = b * (unsigned)pct / 100;
        if (r > 255) r = 255;
        if (g > 255) g = 255;
        if (b > 255) b = 255;
    }
    snprintf(buf, len, "\033[38;2;%u;%u;%um", r, g, b);
}

#define COL_SZ 48

typedef struct {
    char dna_hex[7];
    char corrupt_hex[7];
    char recover_hex[7];
    int  show_stats;
} config_t;

static void config_defaults(config_t *cf) {
    strcpy(cf->dna_hex, "00ffcc");
    strcpy(cf->corrupt_hex, "ff0000");
    strcpy(cf->recover_hex, "00ff50");
    cf->show_stats = 1;
}

static void config_path(char *buf, size_t len) {
    const char *xdg = getenv("XDG_CONFIG_HOME");
    const char *home = getenv("HOME");
    if (xdg && *xdg)
        snprintf(buf, len, "%s/venta/config.json", xdg);
    else if (home)
        snprintf(buf, len, "%s/.config/venta/config.json", home);
    else
        snprintf(buf, len, "config.json");
}

static void ensure_config(const config_t *cf) {
    (void)cf;
    char path[1024];
    config_path(path, sizeof(path));
    if (access(path, F_OK) == 0) return;

    char dir[1024];
    snprintf(dir, sizeof(dir), "%s", path);
    char *slash = strrchr(dir, '/');
    if (slash) {
        *slash = '\0';
        char cmd[2048];
        snprintf(cmd, sizeof(cmd), "mkdir -p '%s'", dir);
        system(cmd);
    }

    FILE *f = fopen(path, "w");
    if (f) {
        fputs(default_config, f);
        fclose(f);
    }
}

static const char *json_str(const char *line, const char *key) {
    const char *k = strstr(line, key);
    if (!k) return NULL;
    k = strchr(k, ':');
    if (!k) return NULL;
    k++;
    while (*k && (*k == ' ' || *k == '\t')) k++;
    if (*k == '"') {
        k++;
        const char *end = strchr(k, '"');
        if (!end) return NULL;
        static char val[64];
        size_t n = (size_t)(end - k);
        if (n >= sizeof(val)) n = sizeof(val) - 1;
        memcpy(val, k, n);
        val[n] = '\0';
        return val;
    }
    return NULL;
}

static const char *json_num(const char *line, const char *key) {
    const char *k = strstr(line, key);
    if (!k) return NULL;
    k = strchr(k, ':');
    if (!k) return NULL;
    k++;
    while (*k && (*k == ' ' || *k == '\t')) k++;
    static char val[32];
    int i = 0;
    while (*k && *k != ',' && *k != '\n' && *k != '\r' && *k != '}' && i < 31) {
        val[i++] = *k++;
    }
    val[i] = '\0';
    return val;
}

static int json_bool(const char *line, const char *key) {
    const char *v = json_num(line, key);
    if (!v) return -1;
    if (strcmp(v, "true") == 0) return 1;
    if (strcmp(v, "false") == 0) return 0;
    return -1;
}

static void load_config(config_t *cf) {
    config_defaults(cf);
    char path[1024];
    config_path(path, sizeof(path));
    ensure_config(cf);

    FILE *f = fopen(path, "r");
    if (!f) return;

    char line[256];
    while (fgets(line, sizeof(line), f)) {
        const char *v;
        v = json_str(line, "dna_color");
        if (v && strlen(v) >= 6) {
            size_t n = strlen(v);
            char clean[7];
            int ci = 0;
            for (size_t i = 0; i < n && ci < 6; i++)
                if (isxdigit((unsigned char)v[i]))
                    clean[ci++] = v[i];
            clean[ci] = '\0';
            if (ci == 6) strcpy(cf->dna_hex, clean);
        }

        v = json_str(line, "corrupt_color");
        if (v && strlen(v) >= 6) {
            char clean[7];
            int ci = 0;
            for (size_t i = 0; v[i] && ci < 6; i++)
                if (isxdigit((unsigned char)v[i]))
                    clean[ci++] = v[i];
            clean[ci] = '\0';
            if (ci == 6) strcpy(cf->corrupt_hex, clean);
        }

        v = json_str(line, "recover_color");
        if (v && strlen(v) >= 6) {
            char clean[7];
            int ci = 0;
            for (size_t i = 0; v[i] && ci < 6; i++)
                if (isxdigit((unsigned char)v[i]))
                    clean[ci++] = v[i];
            clean[ci] = '\0';
            if (ci == 6) strcpy(cf->recover_hex, clean);
        }

        int b = json_bool(line, "show_stats");
        if (b >= 0) cf->show_stats = b;
    }
    fclose(f);
}

static struct termios orig_termios;
static int termios_saved = 0;

static void cleanup_term(void) {
    printf("\033[0m\033[?25h\033[?1049l");
    fflush(stdout);
    if (termios_saved) {
        tcsetattr(STDIN_FILENO, TCSANOW, &orig_termios);
        termios_saved = 0;
    }
}

static void setup_term(void) {
    struct termios raw;
    tcgetattr(STDIN_FILENO, &orig_termios);
    termios_saved = 1;
    raw = orig_termios;
    raw.c_lflag &= ~(ECHO | ICANON);
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 1;
    tcsetattr(STDIN_FILENO, TCSANOW, &raw);
    printf("\033[?1049h\033[?25l\033[2J");
    fflush(stdout);
}

static void termsize(int *rows, int *cols) {
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0) {
        *rows = ws.ws_row;
        *cols = ws.ws_col;
    } else {
        *rows = 24;
        *cols = 80;
    }
    if (*rows < 20) *rows = 20;
    if (*cols < 50) *cols = 50;
}

/* precomputed lookup so we don't call sin() 1000 times a frame */
static int sin_tab[360];
static int cos_tab[360];
static int trig_ready = 0;

static void init_trig(void) {
    if (trig_ready) return;
    for (int i = 0; i < 360; i++) {
        double rad = i * M_PI / 180.0;
        sin_tab[i] = (int)(sin(rad) * 1000);
        cos_tab[i] = (int)(cos(rad) * 1000);
    }
    trig_ready = 1;
}

static void check_updates(void) {
    const char *current = VENTA_VERSION;

    if (strcmp(current, "dev") == 0) {
        printf("use the flakes install u bum\n");
        return;
    }

    printf("Checking for updates\n");

    const char *gh_or_cb[] = {
        "https://api.github.com/repos/realnrxg/venta/releases/latest",
        "https://codeberg.org/api/v1/repos/nrxg/venta/releases/latest"
    };
    const char *tag = NULL;
    static char tag_buf[64];
    for (int attempt = 0; attempt < 2; attempt++) {
        char cmd[1024];
        snprintf(cmd, sizeof(cmd),
                 "curl -s --max-time 5 '%s' 2>/dev/null", gh_or_cb[attempt]);

        FILE *fp = popen(cmd, "r");
        if (!fp) continue;

        char resp[8192];
        size_t len = 0;
        int ch;
        while ((ch = fgetc(fp)) != EOF && len < sizeof(resp) - 1)
            resp[len++] = (char)ch;
        resp[len] = '\0';
        int status = pclose(fp);
        if (status != 0 || len == 0) {
            if (attempt == 0)
                printf("Github is unreachable trying codeberg\n");
            continue;
        }

        const char *tn = strstr(resp, "\"tag_name\"");
        if (!tn) continue;
        tn = strchr(tn, ':');
        if (!tn) continue;
        tn++;
        while (*tn && (*tn == ' ' || *tn == '\t')) tn++;
        if (*tn != '"') continue;
        tn++;
        const char *end = strchr(tn, '"');
        if (!end) continue;
        size_t n = (size_t)(end - tn);
        if (n >= sizeof(tag_buf)) n = sizeof(tag_buf) - 1;
        memcpy(tag_buf, tn, n);
        tag_buf[n] = '\0';
        tag = tag_buf;
        break;
    }

    if (!tag) {
        printf("Could not reach github or codeberg\n");
        return;
    }

    const char *tc = tag;
    if (*tc == 'v') tc++;
    const char *cc = current;
    if (*cc == 'v') cc++;

    if (strcmp(tc, cc) == 0) {
        printf("No new updates found (%s).\n", tag);
        return;
    }

    int cur_major = 0, cur_minor = 0, cur_patch = 0;
    int tag_major = 0, tag_minor = 0, tag_patch = 0;
    sscanf(cc, "%d.%d.%d", &cur_major, &cur_minor, &cur_patch);
    sscanf(tc, "%d.%d.%d", &tag_major, &tag_minor, &tag_patch);

    long cur_ver = cur_major * 10000L + cur_minor * 100L + cur_patch;
    long tag_ver = tag_major * 10000L + tag_minor * 100L + tag_patch;

    if (tag_ver > cur_ver)
        printf("Update available: %s -> %s\n", current, tag);
    else
        printf("You're on a newer version (ur not me u bum) (%s > %s).\n", current, tag);
}

typedef struct {
    const char *ch;
    unsigned int type;
} cell_t;

static cell_t *fb = NULL;
static int fb_h = 0;
static int fb_w = 0;

static void fb_resize(int h, int w) {
    if (fb && fb_h == h && fb_w == w) return;
    free(fb);
    fb = malloc((size_t)(h * w) * sizeof(cell_t));
    fb_h = h;
    fb_w = w;
}

static void fb_wipe(int h, int w) {
    fb_resize(h, w);
    int n = h * w;
    for (int i = 0; i < n; i++) {
        fb[i].ch = " ";
        fb[i].type = 1;
    }
}

static void set_cell(int y, int x, const char *ch, unsigned int typ, int h, int w) {
    if (y < 0 || y >= h || x < 0 || x >= w) return;
    int idx = y * w + x;
    fb[idx].ch = ch;
    fb[idx].type = typ;
}

int main(int argc, char **argv) {
    if (argc >= 2) {
        if (strcmp(argv[1], "-V") == 0 || strcmp(argv[1], "--version") == 0) {
            printf("%s\n", VENTA_VERSION);
            return 0;
        }
        if (strcmp(argv[1], "-U") == 0 || strcmp(argv[1], "--update") == 0) {
            check_updates();
            return 0;
        }
    }

    config_t cf;
    load_config(&cf);

    char c_bright[COL_SZ];
    char c_mid[COL_SZ];
    char c_dim[COL_SZ];
    char c_corrupt[COL_SZ];
    char c_recover[COL_SZ];
    char c_recover_mid[COL_SZ];
    char c_recover_dim[COL_SZ];
    char c_reform[COL_SZ];
    char c_reset[] = "\033[0m";

    hexcol(c_bright, sizeof(c_bright), cf.dna_hex, -1);
    hexcol(c_mid, sizeof(c_mid), cf.dna_hex, 62);
    hexcol(c_dim, sizeof(c_dim), cf.dna_hex, 28);
    hexcol(c_corrupt, sizeof(c_corrupt), cf.corrupt_hex, -1);
    hexcol(c_recover, sizeof(c_recover), cf.recover_hex, -1);
    hexcol(c_recover_mid, sizeof(c_recover_mid), cf.recover_hex, 55);
    hexcol(c_recover_dim, sizeof(c_recover_dim), cf.recover_hex, 25);

    {
        unsigned int dr, dg, db, cr, cg, cb;
        sscanf(cf.dna_hex, "%2x%2x%2x", &dr, &dg, &db);
        sscanf(cf.corrupt_hex, "%2x%2x%2x", &cr, &cg, &cb);
        unsigned int rr = (dr + cr) / 2;
        unsigned int rg = (dg + cg) / 2;
        unsigned int rb = (db + cb) / 2;
        snprintf(c_reform, sizeof(c_reform), "\033[38;2;%u;%u;%um", rr, rg, rb);
    }

    int PSTEP = 20;
    char p_bright[20][COL_SZ];
    char p_mid[20][COL_SZ];
    char p_dim[20][COL_SZ];

    {
        unsigned int dr, dg, db, cr, cg, cb;
        sscanf(cf.dna_hex, "%2x%2x%2x", &dr, &dg, &db);
        sscanf(cf.corrupt_hex, "%2x%2x%2x", &cr, &cg, &cb);
        for (int t = 0; t < PSTEP; t++) {
            int r = (int)(dr + ((int)cr - (int)dr) * t / (PSTEP - 1));
            int g = (int)(dg + ((int)cg - (int)dg) * t / (PSTEP - 1));
            int b = (int)(db + ((int)cb - (int)db) * t / (PSTEP - 1));
            snprintf(p_bright[t], COL_SZ, "\033[38;2;%d;%d;%dm", r, g, b);
        }
        unsigned int dmr = dr * 62 / 100, dmg = dg * 62 / 100, dmb = db * 62 / 100;
        unsigned int cmr = cr * 62 / 100, cmg = cg * 62 / 100, cmb = cb * 62 / 100;
        for (int t = 0; t < PSTEP; t++) {
            int r = (int)(dmr + ((int)cmr - (int)dmr) * t / (PSTEP - 1));
            int g = (int)(dmg + ((int)cmg - (int)dmg) * t / (PSTEP - 1));
            int b = (int)(dmb + ((int)cmb - (int)dmb) * t / (PSTEP - 1));
            snprintf(p_mid[t], COL_SZ, "\033[38;2;%d;%d;%dm", r, g, b);
        }
        unsigned int ddr = dr * 28 / 100, ddg = dg * 28 / 100, ddb = db * 28 / 100;
        unsigned int cdr = cr * 28 / 100, cdg = cg * 28 / 100, cdb = cb * 28 / 100;
        for (int t = 0; t < PSTEP; t++) {
            int r = (int)(ddr + ((int)cdr - (int)ddr) * t / (PSTEP - 1));
            int g = (int)(ddg + ((int)cdg - (int)ddg) * t / (PSTEP - 1));
            int b = (int)(ddb + ((int)cdb - (int)ddb) * t / (PSTEP - 1));
            snprintf(p_dim[t], COL_SZ, "\033[38;2;%d;%d;%dm", r, g, b);
        }
    }

    init_trig();

    atexit(cleanup_term);
    struct sigaction sa_int;
    memset(&sa_int, 0, sizeof(sa_int));
    sa_int.sa_handler = handle_sigint;
    sigaction(SIGINT, &sa_int, NULL);
    sigaction(SIGTERM, &sa_int, NULL);

    struct sigaction sa_winch;
    memset(&sa_winch, 0, sizeof(sa_winch));
    sa_winch.sa_handler = handle_sigwinch;
    sigaction(SIGWINCH, &sa_winch, NULL);

    setup_term();

    int rows = 0, cols = 0;
    termsize(&rows, &cols);
    int prow = rows, pcol = cols;
    int cyt = rows / 2;
    int amp = rows / 4;
    if (amp < 4) amp = 4;

    int *bi = calloc((size_t)cols, sizeof(int));
    int *rot = calloc((size_t)cols, sizeof(int));
    int *bt = calloc((size_t)cols, sizeof(int));

    /* ---- assembly ---- */
    {
        int ar = 0;
        while (running && ar <= cols / 2 + 1) {
            if (resize_flag) {
                resize_flag = 0;
                termsize(&rows, &cols);
                if (rows != prow || cols != pcol) {
                    prow = rows; pcol = cols;
                    cyt = rows / 2;
                    amp = rows / 4;
                    if (amp < 4) amp = 4;
                    int total = rows * cols;
                    fb_resize(rows, cols);
                    {
                        int oc = cols;
                        free(bi);
                        free(rot);
                        free(bt);
                        bi = calloc((size_t)oc, sizeof(int));
                        rot = calloc((size_t)oc, sizeof(int));
                        bt = calloc((size_t)oc, sizeof(int));
                        for (int x = 0; x < oc; x++) {
                            if (bi[x] == 0 && x < oc)
                                bi[x] = rand() % 4;
                        }
                    }
                    (void)total;
                }
            }

            size_t out_sz = (size_t)(rows * (cols * 24 + 20));
            char *out = malloc(out_sz);
            if (!out) break;
            char *p = out;
            char *end = out + out_sz;

            for (int i = 0; i < rows; i++) {
                if (end - p < 32) break;
                p += snprintf(p, (size_t)(end - p), "\033[%d;1H", i + 1);
                for (int j = 0; j < cols; j++) {
                    if (end - p < 32) break;
                    int dc = j - cols / 2;
                    if (dc < 0) dc = -dc;

                    if (dc > ar) {
                        *p++ = ' ';
                        continue;
                    }

                    int phase = (ar * 7 + j * 9) % 360;
                    int s = sin_tab[phase];
                    int cv = cos_tab[phase];
                    int y1 = cyt + (s * amp / 1000);
                    int y2 = cyt - (s * amp / 1000);
                    int drift = sin_tab[(phase + 90) % 360] * 2 / 1000;
                    y1 += drift;
                    y2 -= drift;

                    int top = y1, bot = y2;
                    if (top > bot) { int t = top; top = bot; bot = t; }
                    int m = (top + bot) / 2;
                    int ed = ar - dc;

                    const char *ac;
                    if (ed <= 2) ac = c_bright;
                    else if (ed <= 5) ac = c_mid;
                    else ac = c_dim;

                    int dep;
                    if (cv > 600) dep = 2;
                    else if (cv > -600) dep = 1;
                    else dep = 0;

                    if (i == y1 || i == y2) {
                        const char *bc = (bases + bi[j % cols]);
                        char tmp[2] = {bc[0], '\0'};
                        p += snprintf(p, (size_t)(end - p), "%s%s", ac, tmp);
                        if (dep >= 1 && i == y1 && i - 1 >= 0)
                            p += snprintf(p, (size_t)(end - p), "\033[%d;%dH%s·", i, j + 1, c_mid);
                        if (dep >= 1 && i == y2 && i + 1 < rows)
                            p += snprintf(p, (size_t)(end - p), "\033[%d;%dH%s·", i + 2, j + 1, c_mid);
                    } else if (i > top && i < bot) {
                        int dm = i - m;
                        if (dm < 0) dm = -dm;
                        int rp = (j + ar / 2) % 5;
                        const char *ch;
                        if (rp == 0) {
                            if (dm == 0) ch = "═";
                            else if (dm == 1 && (bot - top - 1) > 3) ch = "│";
                            else ch = "·";
                        } else {
                            if (dm <= 1) {
                                switch (((j + i + ar) % 4)) {
                                    case 0: ch = "·"; break;
                                    case 1: ch = ","; break;
                                    case 2: ch = "`"; break;
                                    default: ch = "."; break;
                                }
                            } else {
                                if (((j + i + ar) % 7) == 0) ch = "·";
                                else ch = " ";
                            }
                        }
                        if (ch[0] != ' ' || ch[1] != '\0')
                            p += snprintf(p, (size_t)(end - p), "%s%s", ac, ch);
                        else
                            *p++ = ' ';
                    } else {
                        *p++ = ' ';
                    }
                }
                p += snprintf(p, (size_t)(end - p), "%s", c_reset);
            }

            write(STDOUT_FILENO, out, (size_t)(p - out));
            free(out);
            ar += 3;

            nanosleep(&(struct timespec){0, 50000000}, NULL);
        }
    }

    /* ---- main animation state ---- */
    int tick = 0;
    int BRK_T = 18, BRK_S = 5;
    int gt = 0, gc = 0, gs = 0, gp = 0;
    int heal = 0, hx = 0, hw = 8, hcd = 0;
    int cst = 0, ct = 0;
    int CD = 45, CC = 55, CR = 90;
    int ca = amp;
    int rfp = 0;
    int PR_CD = 350, pr_cd = 0;

    for (int x = 0; x < cols; x++)
        bi[x] = rand() % 4;

    while (running) {
        if (resize_flag) {
            resize_flag = 0;
            termsize(&rows, &cols);
            if (rows != prow || cols != pcol) {
                int oc = pcol;
                prow = rows; pcol = cols;
                cyt = rows / 2;
                amp = rows / 4;
                if (amp < 4) amp = 4;

                int *nb = calloc((size_t)cols, sizeof(int));
                int *nr = calloc((size_t)cols, sizeof(int));
                int *nbt = calloc((size_t)cols, sizeof(int));
                int cn = cols < oc ? cols : oc;
                for (int x = 0; x < cn; x++) {
                    nb[x] = bi[x];
                    nr[x] = rot[x];
                    nbt[x] = bt[x];
                }
                for (int x = cn; x < cols; x++)
                    nb[x] = rand() % 4;
                free(bi); bi = nb;
                free(rot); rot = nr;
                free(bt); bt = nbt;

                fb_resize(rows, cols);
            }
        }

        /* r key (manual recovery) */
        char key = 0;
        {
            fd_set fds;
            struct timeval tv;
            FD_ZERO(&fds);
            FD_SET(STDIN_FILENO, &fds);
            tv.tv_sec = 0;
            tv.tv_usec = 10000;
            if (select(1, &fds, NULL, NULL, &tv) > 0) {
                if (read(STDIN_FILENO, &key, 1) != 1) key = 0;
            }
        }
        if (key == 'r' || key == 'R') {
            heal = 1;
            hx = 0;
            hcd = 0;
        }

        fb_wipe(rows, cols);

        /* ----- state transitions ----- */
        if (cst == 0) {
            if (tick > 1000 && pr_cd == 0) {
                int ccnt = 0;
                for (int x = 0; x < cols; x++)
                    if (rot[x] >= 27) ccnt++;
                if (ccnt * 100 / cols > 85) {
                    cst = 1;
                    ct = CD;
                    ca = amp;
                    heal = 0;
                    hcd = 9999;
                }
            }
        } else if (cst == 1) {
            ct--;
            ca = amp * ct / CD;
            if (ca < 0) ca = 0;
            for (int x = 0; x < cols; x++) {
                rot[x] += 3;
                if (rot[x] > 30) rot[x] = 30;
            }
            if (ct <= 0) {
                cst = 2;
                ct = CC;
                ca = 0;
            }
        } else if (cst == 2) {
            ct--;
            int cdns = 40 + rand() % 40;
            for (int ci = 0; ci < rows; ci++) {
                for (int cj = 0; cj < cols; cj++) {
                    if (rand() % 100 < cdns) {
                        const char *ch = corrupt_chars[rand() % ncorr];
                        set_cell(ci, cj, ch, 3, rows, cols);
                    }
                }
            }
            if (ct <= 0) {
                cst = 3;
                ct = CR;
                ca = 0;
                rfp = 0;
                for (int x = 0; x < cols; x++) {
                    rot[x] = 0;
                    bt[x] = 0;
                }
                hcd = 600;
            }
        } else if (cst == 3) {
            ct--;
            rfp = CR - ct;
            ca = amp * rfp / CR;
            if (ca > amp) ca = amp;
            if (ct <= 0) {
                cst = 0;
                ca = amp;
                pr_cd = PR_CD;
            }
        }

        /* random glitch bursts */
        if (cst == 0 && gt == 0 && tick > 40 && pr_cd == 0) {
            if (rand() % 1000 < 7 + tick / 160) {
                gt = 8 + rand() % 10;
                gc = rand() % cols;
                gs = 2 + rand() % 5;
                gp = PSTEP;
            }
        }
        if (gp > 0) gp--;

        /* auto-repair when too damaged */
        if (hcd > 0) hcd--;
        if (cst == 0 && heal == 0 && hcd == 0 && tick > 600) {
            int dirty = 0;
            for (int x = 0; x < cols; x++)
                if (rot[x] > 25) dirty++;
            if (dirty * 100 / cols > 35) {
                heal = 1;
                hx = 0;
            }
        }

        /* sweep a healing wave */
        if (heal) {
            hx += 2;
            if (hx > cols + hw * 3) {
                heal = 0;
                hcd = 300;
            }
            for (int h = hx - hw * 3; h <= hx; h++) {
                if (h >= 0 && h < cols) {
                    rot[h] = rot[h] > 3 ? rot[h] - 6 : 0;
                    bt[h] = 0;
                }
            }
        }

        if (cst != 2) {
            int ca_cur = ca;

            for (int x = 0; x < cols; x++) {
                int phase = (tick * 7 + x * 9) % 360;
                int s = sin_tab[phase];
                int cv = cos_tab[phase];

                int y1 = cyt + (s * ca_cur / 1000);
                int y2 = cyt - (s * ca_cur / 1000);
                int drift = sin_tab[(phase + 90) % 360] * 2 / 1000;
                y1 += drift;
                y2 -= drift;

                int dep;
                if (cv > 600) dep = 2;
                else if (cv > -600) dep = 1;
                else dep = 0;
                /* dep = (abs(cv) > 600) ? 2 : (abs(cv) > 200 ? 1 : 0); -- alternative */

                if (cst == 0) {
                    int br = sin_tab[(tick * 2) % 360] * (amp / 6) / 1000;
                    y1 -= br;
                    y2 += br;
                }

                if (cst == 1) {
                    int uz = (CD - ct) * (amp / 2) / CD;
                    y1 -= uz;
                    y2 += uz;
                }

                /* bases randomly swap */
                if (cst == 0 && rand() % 1000 < 8)
                    bi[x] = rand() % 4;

                /* slow decay */
                if (cst == 0) {
                    if (pr_cd == 0) {
                        if (tick > 100 && rand() % 1000 < (1 + tick / 500))
                            rot[x]++;
                        if (rot[x] > 24) {
                            if (x > 0 && rand() % 100 < 15) rot[x - 1]++;
                            if (x < cols - 1 && rand() % 100 < 15) rot[x + 1]++;
                        }
                    }
                    if (rot[x] > 0 && rand() % 1000 < 9)
                        rot[x]--;
                }

                int idx = bi[x];
                const char bc[2] = {bases[idx], '\0'};
                const char cc[2] = {comp[idx], '\0'};
                const char *b = bc;
                const char *c = cc;
                int bt_ = dep, ct_ = dep;

                /* glitch warp */
                if (gt > 0) {
                    int dist = x - gc;
                    if (dist < 0) dist = -dist;
                    if (dist < gs * 5) {
                        int push = (gs * 5 - dist) / 2;
                        int jit = (rand() % 3) - 1;
                        y1 += jit + push / 3;
                        y2 -= jit - push / 3;
                        if (rand() % 100 < 55) rot[x] += 2;
                    }
                }

                if (rot[x] > 10 && rand() % 100 < rot[x]) {
                    b = corrupt_chars[rand() % ncorr];
                    bt_ = 3;
                }
                if (rot[x] > 10 && rand() % 100 < rot[x]) {
                    c = corrupt_chars[rand() % ncorr];
                    ct_ = 3;
                }

                /* crystal formations during reform */
                if (cst == 3) {
                    int melt = rfp * 100 / CR;
                    if (bt_ != 3) {
                        bt_ = 5;
                        if (rand() % 100 >= melt)
                            b = crystal_chars[rand() % ncrys];
                    }
                    if (ct_ != 3) {
                        ct_ = 5;
                        if (rand() % 100 >= melt)
                            c = crystal_chars[rand() % ncrys];
                    }
                }

                /* healing wave colours */
                if (heal) {
                    int tr = hx - x;
                    if (tr >= 0 && tr < hw) {
                        bt_ = 4; ct_ = 4;
                        if (tr < 3) { b = "+"; c = "+"; }
                    } else if (tr >= hw && tr < hw * 3) {
                        if (tr < hw * 2) {
                            bt_ = 6; ct_ = 6;
                        } else {
                            bt_ = 7; ct_ = 7;
                        }
                    }
                }

                /* strand snaps when too much */
                if (bt[x] > 0) bt[x]--;
                if (cst == 0 && rot[x] > 22 && bt[x] == 0 && rand() % 100 < 10)
                    bt[x] = BRK_T;

                int top = y1, bot = y2;
                if (top > bot) { int t = top; top = bot; bot = t; }
                int mid_y = (top + bot) / 2;
                int bh = bot - top - 1;

                if (bt[x] == 0) {
                    int rp = (x + tick / 2) % 5;

                    for (int y = top + 1; y < bot; y++) {
                        int dm = y - mid_y;
                        if (dm < 0) dm = -dm;

                        const char *ch;
                        unsigned int fl;

                        if (rp == 0) {
                            if (dm == 0) { ch = "═"; fl = dep; }
                            else if (dm == 1 && bh > 3) { ch = "│"; fl = dep; }
                            else { ch = "·"; fl = dep; }
                        } else {
                            if (dm <= 1) {
                                switch (((x + y + tick) % 4)) {
                                    case 0: ch = "·"; break;
                                    case 1: ch = ","; break;
                                    case 2: ch = "`"; break;
                                    default: ch = "."; break;
                                }
                                fl = dep;
                            } else {
                                if (((x + y + tick) % 7) == 0) { ch = "·"; fl = dep; }
                                else { ch = " "; fl = 1; }
                            }
                        }

                        if (rot[x] > 14 && rand() % 100 < rot[x] / 2) {
                            ch = corrupt_chars[rand() % ncorr];
                            fl = 3;
                        }

                        if (gt > 0) {
                            int dist = x - gc;
                            if (dist < 0) dist = -dist;
                            if (dist < gs * 4 && rand() % 100 < 35) {
                                ch = corrupt_chars[rand() % ncorr];
                                fl = 3;
                            }
                        }

                        if (cst == 3) {
                            int melt = rfp * 100 / CR;
                            if (rand() % 100 >= melt) {
                                ch = crystal_chars[rand() % ncrys];
                                fl = 5;
                            }
                        }

                        if (heal) {
                            int wd = x - hx;
                            if (wd < 0) wd = -wd;
                            if (wd < hw) {
                                fl = 4;
                                if (wd < 3) ch = "|";
                            } else if (wd >= hw && wd < hw * 3) {
                                if (wd < hw * 2) fl = 6;
                                else fl = 7;
                            }
                        }

                        set_cell(y, x, ch, fl, rows, cols);
                    }

                    if (dep >= 1) {
                        if (y1 - 1 >= 0)
                            set_cell(y1 - 1, x, "·", dep > 0 ? dep - 1 : 0, rows, cols);
                        if (y2 + 1 < rows)
                            set_cell(y2 + 1, x, "·", dep > 0 ? dep - 1 : 0, rows, cols);
                    }
                    if (dep == 2) {
                        if (y1 - 2 >= 0)
                            set_cell(y1 - 2, x, ",", dep > 1 ? dep - 2 : 0, rows, cols);
                        if (y2 + 2 < rows)
                            set_cell(y2 + 2, x, ",", dep > 1 ? dep - 2 : 0, rows, cols);
                    }

                } else if (bt[x] > BRK_T - BRK_S) {
                    /* strand tearinfg */
                    int sf = BRK_T - bt[x];
                    int tr = sf * bh / (BRK_S * 2) + 1;
                    for (int y = top + 1; y < bot; y++) {
                        int dm = y - mid_y;
                        if (dm < 0) dm = -dm;
                        if (dm <= tr) {
                            if (dm == tr) {
                                const char *ch;
                                switch (((x + y + tick) % 4)) {
                                    case 0: ch = "\\"; break;
                                    case 1: ch = "/"; break;
                                    case 2: ch = "~"; break;
                                    default: ch = "-"; break;
                                }
                                set_cell(y, x, ch, 3, rows, cols);
                            }
                        } else {
                            const char *ch = rungs[((x + y + tick) % nrung)];
                            set_cell(y, x, ch, dep, rows, cols);
                        }
                    }
                    switch (tick % 3) {
                        case 0: bt_ = 3; b = "\\"; break;
                        case 1: bt_ = 3; b = "/"; break;
                        default: bt_ = 3; break;
                    }
                    switch ((tick + 1) % 3) {
                        case 0: ct_ = 3; c = "\\"; break;
                        case 1: ct_ = 3; c = "/"; break;
                        default: ct_ = 3; break;
                    }
                } else {
                    /* debre falling */
                    int fa = BRK_T - bt[x] - BRK_S;
                    int mf = cols / 4;
                    int ctf = dep > 0 ? dep - 1 : 0;
                    if (fa < mf) {
                        int dy1 = y1 + fa + ((x + tick) % 2);
                        if (dy1 >= 0 && dy1 < rows) {
                            const char *ch = shards[((x + tick) % nsh)];
                            set_cell(dy1, x, ch, ctf, rows, cols);
                        }
                        int dy2 = y2 + fa + ((x + tick + 1) % 2);
                        if (dy2 >= 0 && dy2 < rows) {
                            const char *ch = shards[((x + tick + 2) % nsh)];
                            set_cell(dy2, x, ch, ctf, rows, cols);
                        }
                        int dy3 = mid_y + fa + ((x + tick) % 3) - 1;
                        if (dy3 >= 0 && dy3 < rows) {
                            const char *ch = shards[((x + tick + 3) % nsh)];
                            set_cell(dy3, x, ch, 3, rows, cols);
                        }
                    }
                    for (int y = top; y <= bot; y++)
                        set_cell(y, x, " ", 1, rows, cols);
                    switch (tick % 4) {
                        case 0: bt_ = 3; b = "!"; break;
                        case 1: bt_ = 3; b = "|"; break;
                        default: break;
                    }
                    switch ((tick + 2) % 4) {
                        case 0: ct_ = 3; c = "!"; break;
                        case 1: ct_ = 3; c = "|"; break;
                        default: break;
                    }
                }

                set_cell(y1, x, b, bt_, rows, cols);
                set_cell(y2, x, c, ct_, rows, cols);

                if (cst == 0 && rot[x] > 18 && rand() % 100 < 10)
                    set_cell((y1 + y2) / 2, x, "¦", 3, rows, cols);
            }

            /* glitch lines */
            if (gt > 0) {
                int py = cyt + (sin_tab[(tick * 18) % 360] * (amp / 3) / 1000);
                for (int dx = -gs * 3; dx <= gs * 3; dx++) {
                    int gx = gc + dx;
                    if (gx < 0 || gx >= cols) continue;
                    const char *ch;
                    switch (((dx < 0 ? -dx : dx) % 5)) {
                        case 0: ch = "*"; break;
                        case 1: ch = "+"; break;
                        case 2: ch = "="; break;
                        case 3: ch = "-"; break;
                        default: ch = ":"; break;
                    }
                    if (rand() % 100 < 45)
                        ch = corrupt_chars[rand() % ncorr];
                    set_cell(py, gx, ch, 3, rows, cols);
                    set_cell(py - 1, gx, ".", 3, rows, cols);
                    set_cell(py + 1, gx, ".", 3, rows, cols);
                }
                gt--;
            }
        }

        if (pr_cd > 0) pr_cd--;

        /* glitch pulse animation steps */
        int half = PSTEP / 2;
        int fe = PSTEP - gp;
        int pi;
        if (fe < half)
            pi = fe * (PSTEP - 1) / half;
        else
            pi = (PSTEP - 1) - (fe - half) * (PSTEP - 1) / half;
        if (pi < 0) pi = 0;
        if (pi >= PSTEP) pi = PSTEP - 1;

        /* ----- frame ----- */
        size_t out_sz = (size_t)(rows * (cols * 24 + 20));
        char *out = malloc(out_sz);
        if (!out) break;
        char *p = out;
        char *end = out + out_sz;

        const char *cur_color = NULL;

        for (int i = 0; i < rows; i++) {
            if (end - p < 32) break;
            p += snprintf(p, (size_t)(end - p), "\033[%d;1H", i + 1);
            for (int j = 0; j < cols; j++) {
                if (end - p < 32) break;
                int cidx = i * cols + j;
                const char *ch = fb[cidx].ch;
                unsigned int typ = fb[cidx].type;

                if (ch[0] == ' ' && ch[1] == '\0') {
                    if (cur_color) {
                        p += snprintf(p, (size_t)(end - p), "%s", c_reset);
                        cur_color = NULL;
                    }
                    *p++ = ' ';
                } else {
                    const char *want = NULL;
                    if (typ == 3) want = c_corrupt;
                    else if (typ == 4) want = c_recover;
                    else if (typ == 6) want = c_recover_mid;
                    else if (typ == 7) want = c_recover_dim;
                    else if (typ == 5) want = c_reform;
                    else if (gp > 0) {
                        if (typ == 0) want = p_dim[pi];
                        else if (typ == 1) want = p_mid[pi];
                        else want = p_bright[pi];
                    } else {
                        if (typ == 0) want = c_dim;
                        else if (typ == 1) want = c_mid;
                        else want = c_bright;
                    }

                    if (want != cur_color) {
                        p += snprintf(p, (size_t)(end - p), "%s", want);
                        cur_color = want;
                    }
                    size_t chlen = strlen(ch);
                    if (end - p < (int)chlen + 1) break;
                    memcpy(p, ch, chlen);
                    p += chlen;
                }
            }
            if (end - p >= 4) {
                p += snprintf(p, (size_t)(end - p), "%s", c_reset);
                cur_color = NULL;
            }
        }

        /* ----- stats overlay ----- */
        if (cf.show_stats) {
            int crit_cols = 0;
            for (int x = 0; x < cols; x++)
                if (rot[x] >= 27) crit_cols++;
            int decay_pct = (crit_cols * 10000) / (cols * 85);
            if (decay_pct > 100) decay_pct = 100;

            const char *wstr;
            if (cst == 1) wstr = "COLLAPSE";
            else if (cst == 2) wstr = "CHAOS   ";
            else if (cst == 3) wstr = "REFORM  ";
            else if (heal) {
                int wp = hx * 100 / cols;
                if (wp > 100) wp = 100;
                static char rbuf[32];
                snprintf(rbuf, sizeof(rbuf), "REPAIR %d%%", wp);
                wstr = rbuf;
            } else if (pr_cd > 0) {
                int sec = pr_cd * 80 / 1000;
                if (sec < 1) sec = 1;
                static char sbuf[32];
                snprintf(sbuf, sizeof(sbuf), "STABLE %ds", sec);
                wstr = sbuf;
            } else if (hcd > 0) wstr = "COOLDOWN";
            else wstr = "STANDBY ";

            int bfill = decay_pct / 10;
            char dbar[32];
            dbar[0] = '\0';
            for (int b = 0; b < 10; b++)
                strcat(dbar, b < bfill ? "█" : "░");

            int ow = 24;
            int oc = cols - ow;
            if (oc < 1) oc = 1;

            char ov[3][56];
            snprintf(ov[0], sizeof(ov[0]), " DECAY : [%s] %3d%% ", dbar, decay_pct);
            snprintf(ov[1], sizeof(ov[1]), " STATE : %s     ", wstr);
            snprintf(ov[2], sizeof(ov[2]), " R = manual recovery  ");

            for (int li = 0; li < 3; li++) {
                char tmp[28];
                snprintf(tmp, sizeof(tmp), "%-24.24s", ov[li]);
                if (end - p < 64) break;
                p += snprintf(p, (size_t)(end - p), "\033[%d;%dH%s%s%s",
                              li + 2, oc, c_dim, tmp, c_reset);
            }
        }

        if (p > out)
            write(STDOUT_FILENO, out, (size_t)(p - out));
        free(out);

        tick++;
        nanosleep(&(struct timespec){0, 50000000}, NULL);
    }

    free(fb);
    free(bi);
    free(rot);
    free(bt);
    cleanup_term();
    return 0;
}
