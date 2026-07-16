/* Terminal glue over the shared Kitty framebuffer and keyboard libraries. */
#include "joustix.h"
#include "kitty_framebuffer.h"
#include "kitty_keyboard_posix.h"

#include <errno.h>
#include <stdlib.h>
#include <unistd.h>

static kittyfb_session framebuffer;
static kittykb_terminal keyboard;
static bool framebuffer_active;
static bool keyboard_active;
static volatile int shutdown_claimed;

bool term_init(int *out_w, int *out_h)
{
    kittyfb_options framebuffer_options;
    kittykb_terminal_options keyboard_options;

    kittyfb_session_init(&framebuffer);
    kittyfb_options_init(&framebuffer_options);
    framebuffer_options.min_width = 480;
    framebuffer_options.min_height = 270;
    framebuffer_options.max_width = 1440;
    framebuffer_options.max_height = 900;
    if (getenv("JOUSTIX_SKIP_PROBE"))
        framebuffer_options.probe_graphics = false;

    if (kittyfb_start(&framebuffer, STDIN_FILENO, STDOUT_FILENO,
                      &framebuffer_options) != 0)
        return false;
    framebuffer_active = true;
    shutdown_claimed = 0;

    kittykb_terminal_init(&keyboard);
    kittykb_terminal_options_init(&keyboard_options);
    keyboard_options.flags = KITTYKB_FLAGS_KEY_STATE;
    keyboard_options.make_raw = false;
    keyboard_options.make_nonblocking = false;
    if (kittykb_terminal_start(&keyboard, STDIN_FILENO, STDOUT_FILENO,
                               &keyboard_options) != 0) {
        int error = errno;
        kittyfb_stop(&framebuffer);
        framebuffer_active = false;
        errno = error;
        return false;
    }
    keyboard_active = true;
    *out_w = kittyfb_width(&framebuffer);
    *out_h = kittyfb_height(&framebuffer);
    return true;
}

bool term_check_resize(int *out_w, int *out_h)
{
    return framebuffer_active &&
           kittyfb_check_resize(&framebuffer, out_w, out_h);
}

void term_present(const uint8_t *rgba, int w, int h)
{
    if (framebuffer_active)
        (void)kittyfb_present(&framebuffer, rgba, w, h);
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

static bool claim_shutdown(void)
{
    if (!framebuffer_active) return false;
    return !__sync_lock_test_and_set(&shutdown_claimed, 1);
}

void term_shutdown(void)
{
    if (!claim_shutdown()) return;
    if (keyboard_active) {
        (void)kittykb_terminal_stop(&keyboard);
        keyboard_active = false;
    }
    kittyfb_stop(&framebuffer);
    framebuffer_active = false;
}

void term_emergency_restore(void)
{
    static const char keyboard_pop[] = "\x1b\\\x1b[<u";

    if (!claim_shutdown()) return;
    if (keyboard_active)
        (void)write(STDOUT_FILENO, keyboard_pop, sizeof keyboard_pop - 1);
    kittyfb_emergency_restore(&framebuffer);
}
