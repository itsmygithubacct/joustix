/* Raw terminal input and asynchronous Kitty graphics presentation. */
#include "joustix.h"
#include "kitty_keyboard_posix.h"

#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <termios.h>
#include <unistd.h>
#include <zlib.h>

static volatile sig_atomic_t winch_flag;
static struct termios original_termios;
static bool raw_active;
static bool keyboard_active;
static kittykb_terminal keyboard;
static volatile int shutdown_claimed;
static pthread_mutex_t frame_lock = PTHREAD_MUTEX_INITIALIZER;
static bool clear_pending;
static char origin_seq[32] = "\x1b[H";

static void on_winch(int sig) { (void)sig; winch_flag = 1; }

static int read_byte_timeout(unsigned char *c, int timeout_ms)
{
    fd_set readfds;
    FD_ZERO(&readfds);
    FD_SET(STDIN_FILENO, &readfds);
    struct timeval tv = { timeout_ms / 1000, (timeout_ms % 1000) * 1000 };
    if (select(STDIN_FILENO + 1, &readfds, NULL, NULL, &tv) <= 0) return 0;
    return read(STDIN_FILENO, c, 1) == 1;
}

static void write_all(const char *p, size_t n)
{
    while (n) {
        ssize_t wrote = write(STDOUT_FILENO, p, n);
        if (wrote <= 0) return;
        p += wrote;
        n -= (size_t)wrote;
    }
}

static void write_str(const char *s) { write_all(s, strlen(s)); }

static bool measure_geometry(int *out_w, int *out_h)
{
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) != 0) return false;
    int cols = ws.ws_col > 0 ? ws.ws_col : 80;
    int rows = ws.ws_row > 0 ? ws.ws_row : 24;
    int cell_w = ws.ws_xpixel > 0 ? ws.ws_xpixel / cols : 9;
    int cell_h = ws.ws_ypixel > 0 ? ws.ws_ypixel / rows : 18;
    if (cell_w <= 0) cell_w = 9;
    if (cell_h <= 0) cell_h = 18;
    int grid_rows = rows > 2 ? rows - 1 : rows;
    int px = cols * cell_w, py = grid_rows * cell_h;
    if (px < 480) px = 480;
    if (py < 270) py = 270;
    if (px > 1440) px = 1440;
    if (py > 900) py = 900;
    px -= px % cell_w;
    py -= py % cell_h;
    *out_w = px & ~1;
    *out_h = py & ~1;

    int image_cols = (*out_w + cell_w - 1) / cell_w;
    int image_rows = (*out_h + cell_h - 1) / cell_h;
    int col = 1 + (cols - image_cols) / 2;
    int row = 1 + (grid_rows - image_rows) / 2;
    if (col < 1) col = 1;
    if (row < 1) row = 1;
    snprintf(origin_seq, sizeof origin_seq, "\x1b[%d;%dH", row, col);
    return true;
}

static bool kitty_probe(void)
{
    write_str("\x1b_Gi=71,a=q,t=d,f=24,s=1,v=1;AAAA\x1b\\\x1b[c");
    char reply[512];
    size_t n = 0;
    bool graphics = false;
    for (size_t i = 0; i + 1 < sizeof reply; i++) {
        unsigned char c;
        if (!read_byte_timeout(&c, 400)) break;
        reply[n++] = (char)c;
        reply[n] = '\0';
        if (strstr(reply, "\x1b_Gi=71")) graphics = true;
        if (c == 'c' && strstr(reply, "\x1b[?")) break;
    }
    return graphics;
}

bool term_init(int *out_w, int *out_h)
{
    kittykb_terminal_options keyboard_options;

    if (!isatty(STDIN_FILENO) || !isatty(STDOUT_FILENO)) return false;
    if (!measure_geometry(out_w, out_h)) return false;
    if (tcgetattr(STDIN_FILENO, &original_termios) != 0) return false;
    struct termios raw = original_termios;
    raw.c_lflag &= ~(ECHO | ICANON | ISIG | IEXTEN);
    raw.c_iflag &= ~(IXON | ICRNL | BRKINT | INPCK | ISTRIP);
    raw.c_oflag &= ~OPOST;
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 0;
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) != 0) return false;
    raw_active = true;

    if (!getenv("JOUSTIX_SKIP_PROBE") && !kitty_probe()) {
        tcsetattr(STDIN_FILENO, TCSAFLUSH, &original_termios);
        raw_active = false;
        fprintf(stderr, "joustix: terminal did not answer the Kitty graphics query\n"
                        "try Kilix, Kitty, Ghostty, WezTerm, or recent Konsole\n"
                        "(JOUSTIX_SKIP_PROBE=1 bypasses this check)\n");
        return false;
    }
    signal(SIGWINCH, on_winch);
    write_str("\x1b[?1049h\x1b[?25l\x1b[2J\x1b[H");

    kittykb_terminal_init(&keyboard);
    kittykb_terminal_options_init(&keyboard_options);
    keyboard_options.flags = KITTYKB_FLAGS_KEY_STATE;
    keyboard_options.make_raw = false;
    keyboard_options.make_nonblocking = false;
    if (kittykb_terminal_start(&keyboard, STDIN_FILENO, STDOUT_FILENO,
                               &keyboard_options) != 0) {
        int error = errno;
        write_str("\x1b[?25h\x1b[?1049l");
        tcsetattr(STDIN_FILENO, TCSAFLUSH, &original_termios);
        raw_active = false;
        fprintf(stderr, "joustix: keyboard setup failed: %s\n", strerror(error));
        errno = error;
        return false;
    }
    keyboard_active = true;
    return true;
}

bool term_check_resize(int *out_w, int *out_h)
{
    if (!winch_flag) return false;
    winch_flag = 0;
    pthread_mutex_lock(&frame_lock);
    bool ok = measure_geometry(out_w, out_h);
    if (ok) clear_pending = true;
    pthread_mutex_unlock(&frame_lock);
    return ok;
}

static pthread_t presenter;
static pthread_cond_t frame_cond = PTHREAD_COND_INITIALIZER;
static uint8_t *pending_buf, *encode_buf;
static size_t pending_cap, encode_cap;
static int frame_w, frame_h;
static bool frame_pending, presenter_running;

static void presenter_stop(void);

static bool claim_shutdown(void)
{
    if (!raw_active) return false;
    return !__sync_lock_test_and_set(&shutdown_claimed, 1);
}

static void restore_terminal(void)
{
    if (keyboard_active) {
        (void)kittykb_terminal_stop(&keyboard);
        keyboard_active = false;
    }
    /* The first ST also closes an interrupted APC payload. */
    write_str("\x1b\\\x1b_Ga=d,d=A,q=2\x1b\\\x1b[?25h\x1b[?1049l");
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &original_termios);
    raw_active = false;
}

void term_shutdown(void)
{
    if (!claim_shutdown()) return;
    presenter_stop();
    restore_terminal();
    free(pending_buf); pending_buf = NULL; pending_cap = 0;
    free(encode_buf); encode_buf = NULL; encode_cap = 0;
}

void term_emergency_restore(void)
{
    static const char emergency[] =
        "\x1b\\\x1b_Ga=d,d=A,q=2\x1b\\\x1b[<u\x1b[?25h\x1b[?1049l";

    if (!claim_shutdown()) return;
    /* write() is used directly because this path runs from a signal handler. */
    (void)write(STDOUT_FILENO, emergency, sizeof emergency - 1);
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &original_termios);
    raw_active = false;
}

static const char base64_chars[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static size_t base64_encode(const uint8_t *in, size_t len, char *out)
{
    size_t i = 0, o = 0;
    while (i + 2 < len) {
        uint32_t v = (uint32_t)in[i] << 16 | (uint32_t)in[i + 1] << 8 | in[i + 2];
        out[o++] = base64_chars[(v >> 18) & 63];
        out[o++] = base64_chars[(v >> 12) & 63];
        out[o++] = base64_chars[(v >> 6) & 63];
        out[o++] = base64_chars[v & 63];
        i += 3;
    }
    if (i < len) {
        uint32_t v = (uint32_t)in[i] << 16;
        if (i + 1 < len) v |= (uint32_t)in[i + 1] << 8;
        out[o++] = base64_chars[(v >> 18) & 63];
        out[o++] = base64_chars[(v >> 12) & 63];
        out[o++] = i + 1 < len ? base64_chars[(v >> 6) & 63] : '=';
        out[o++] = '=';
    }
    return o;
}

static void encode_and_write(const uint8_t *rgba, int w, int h,
                             const char *origin, bool clear_first)
{
    static uint8_t *rgb, *compressed;
    static char *b64, *output;
    static size_t rgb_cap, compressed_cap, b64_cap, output_cap;
    size_t pixels = (size_t)w * h;
    size_t raw_len = pixels * 3;
    if (raw_len > rgb_cap) {
        uint8_t *next = realloc(rgb, raw_len);
        if (!next) return;
        rgb = next; rgb_cap = raw_len;
    }
    for (size_t i = 0; i < pixels; i++) {
        rgb[i * 3] = rgba[i * 4];
        rgb[i * 3 + 1] = rgba[i * 4 + 1];
        rgb[i * 3 + 2] = rgba[i * 4 + 2];
    }
    size_t bound = compressBound(raw_len);
    if (bound > compressed_cap) {
        uint8_t *next_c = realloc(compressed, bound);
        if (!next_c) return;
        compressed = next_c;
        compressed_cap = bound;
    }
    size_t b64_need = ((bound + 2) / 3) * 4 + 8;
    if (b64_need > b64_cap) {
        char *next_b = realloc(b64, b64_need);
        if (!next_b) return;
        b64 = next_b; b64_cap = b64_need;
    }
    uLongf compressed_len = (uLongf)compressed_cap;
    if (compress2(compressed, &compressed_len, rgb, raw_len, 1) != Z_OK) return;
    size_t b64_len = base64_encode(compressed, compressed_len, b64);
    size_t needed = b64_len + (b64_len / 4096 + 2) * 80 + 256;
    if (needed > output_cap) {
        char *next = realloc(output, needed);
        if (!next) return;
        output = next; output_cap = needed;
    }

    static int shown_id = 2;
    int new_id = shown_id == 1 ? 2 : 1;
    char *o = output;
    o += sprintf(o, "\x1b[?2026h%s%s", clear_first ? "\x1b[2J" : "", origin);
    size_t off = 0;
    bool first = true;
    while (off < b64_len) {
        size_t n = b64_len - off > 4096 ? 4096 : b64_len - off;
        int more = off + n < b64_len;
        if (first) {
            o += sprintf(o, "\x1b_Ga=T,f=24,i=%d,q=2,o=z,s=%d,v=%d,m=%d;",
                         new_id, w, h, more);
            first = false;
        } else {
            o += sprintf(o, "\x1b_Gm=%d;", more);
        }
        memcpy(o, b64 + off, n); o += n;
        *o++ = '\x1b'; *o++ = '\\';
        off += n;
    }
    o += sprintf(o, "\x1b_Ga=d,d=I,i=%d,q=2\x1b\\\x1b[?2026l", shown_id);
    shown_id = new_id;
    write_all(output, (size_t)(o - output));
}

static void *presenter_main(void *unused)
{
    (void)unused;
    for (;;) {
        pthread_mutex_lock(&frame_lock);
        while (!frame_pending && presenter_running)
            pthread_cond_wait(&frame_cond, &frame_lock);
        if (!presenter_running) { pthread_mutex_unlock(&frame_lock); break; }
        uint8_t *tb = pending_buf; pending_buf = encode_buf; encode_buf = tb;
        size_t tc = pending_cap; pending_cap = encode_cap; encode_cap = tc;
        int w = frame_w, h = frame_h;
        char origin[sizeof origin_seq];
        snprintf(origin, sizeof origin, "%s", origin_seq);
        bool clear = clear_pending;
        clear_pending = false;
        frame_pending = false;
        pthread_mutex_unlock(&frame_lock);
        encode_and_write(encode_buf, w, h, origin, clear);
    }
    return NULL;
}

void term_present(const uint8_t *rgba, int w, int h)
{
    size_t need = (size_t)w * h * 4;
    pthread_mutex_lock(&frame_lock);
    if (!presenter_running) {
        presenter_running = true;
        if (pthread_create(&presenter, NULL, presenter_main, NULL) != 0) {
            presenter_running = false;
            pthread_mutex_unlock(&frame_lock);
            encode_and_write(rgba, w, h, origin_seq, false);
            return;
        }
    }
    if (need > pending_cap) {
        uint8_t *next = realloc(pending_buf, need);
        if (!next) { pthread_mutex_unlock(&frame_lock); return; }
        pending_buf = next; pending_cap = need;
    }
    memcpy(pending_buf, rgba, need);
    frame_w = w; frame_h = h; frame_pending = true;
    pthread_cond_signal(&frame_cond);
    pthread_mutex_unlock(&frame_lock);
}

static void presenter_stop(void)
{
    pthread_mutex_lock(&frame_lock);
    if (!presenter_running) { pthread_mutex_unlock(&frame_lock); return; }
    presenter_running = false;
    frame_pending = false;
    pthread_cond_signal(&frame_cond);
    pthread_mutex_unlock(&frame_lock);
    pthread_join(presenter, NULL);
}

int term_read_input(void)
{
    if (!keyboard_active) {
        errno = EINVAL;
        return -1;
    }
    return kittykb_terminal_read(&keyboard);
}

bool term_next_key_event(kittykb_event *event)
{
    return keyboard_active && kittykb_input_next(&keyboard.input, event);
}

bool term_key_down(uint32_t key)
{
    return keyboard_active && kittykb_input_key_down(&keyboard.input, key);
}

bool term_has_release_events(void)
{
    return keyboard_active &&
           kittykb_input_has_release_events(&keyboard.input);
}
