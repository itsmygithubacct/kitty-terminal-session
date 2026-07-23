#include "kitty_terminal_session.h"

#include <assert.h>
#include <errno.h>
#include <pty.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

int main(void)
{
    static const char mixed_input[] =
        "\x1b[<0;12;8M\x1b[I\x1b[200~hello\x1b[201~";
    kittyts_session session;
    kittyts_options options;
    kittykb_event event;
    kittyin_event input_event;
    struct winsize window = {24, 80, 640, 480};
    int master = -1;
    int slave = -1;

    kittyts_session_init(&session);
    kittyts_options_init(&options);
    assert(!options.keyboard.make_raw);
    assert(!options.keyboard.make_nonblocking);
    assert(options.keyboard.flags == KITTYKB_FLAGS_KEY_STATE);
    assert(options.mouse_tracking == KITTYIN_MOUSE_TRACKING_OFF);
    assert(!options.focus_events);
    assert(!options.bracketed_paste);
    assert(&session.keyboard == &session.input.keyboard);
    assert(kittyts_start(NULL, 0, 1, &options) == -1 && errno == EINVAL);

    assert(openpty(&master, &slave, NULL, NULL, &window) == 0);
    assert(ioctl(slave, TIOCSWINSZ, &window) == 0);
    options.framebuffer.probe_graphics = false;
    options.framebuffer.install_winch_handler = false;
    options.framebuffer.min_width = options.framebuffer.max_width = 320;
    options.framebuffer.min_height = options.framebuffer.max_height = 240;
    options.keyboard.probe_timeout_ms = 0;
    options.mouse_tracking = KITTYIN_MOUSE_TRACKING_MOTION;
    options.focus_events = true;
    options.bracketed_paste = true;
    assert(kittyts_start(&session, slave, slave, &options) == 0);
    assert(kittyts_width(&session) == 320);
    assert(kittyts_height(&session) == 240);
    assert(kittyts_start(&session, slave, slave, &options) == -1 &&
           errno == EBUSY);

    assert(write(master, "\x1b[119;1:1u", 10) == 10);
    assert(kittyts_read_input(&session) >= 0);
    assert(kittyts_next_key_event(&session, &event));
    assert(event.key == 'w');
    assert(event.action == KITTYKB_ACTION_PRESS);

    assert(write(master, mixed_input, sizeof mixed_input - 1u) ==
           (ssize_t)(sizeof mixed_input - 1u));
    assert(kittyts_read_input(&session) >= 0);
    assert(kittyts_next_event(&session, &input_event));
    assert(input_event.kind == KITTYIN_EVENT_MOUSE);
    assert(input_event.data.mouse.x == 11);
    assert(input_event.data.mouse.y == 7);
    assert(kittyts_next_event(&session, &input_event));
    assert(input_event.kind == KITTYIN_EVENT_FOCUS);
    assert(input_event.data.focus.focused);
    assert(kittyts_next_event(&session, &input_event));
    assert(input_event.kind == KITTYIN_EVENT_PASTE);
    assert(input_event.data.paste.final);
    assert(input_event.data.paste.length == 5u);
    assert(memcmp(input_event.data.paste.bytes, "hello", 5u) == 0);
    assert(!kittyts_next_event(&session, &input_event));

    /* The compatibility API skips non-key events in the unified queue. */
    assert(write(master, "\x1b[Ox", sizeof("\x1b[Ox") - 1u) ==
           (ssize_t)(sizeof("\x1b[Ox") - 1u));
    assert(kittyts_read_input(&session) >= 0);
    assert(kittyts_next_key_event(&session, &event));
    assert(event.key == 'x');
    assert(!kittyts_next_key_event(&session, &event));

    kittyts_stop(&session);
    kittyts_stop(&session);
    assert(kittyts_width(&session) == 0);

    /* Emergency restoration must not suppress later resource reclamation or
     * permanently wedge this reusable aggregate object. */
    assert(kittyts_start(&session, slave, slave, &options) == 0);
    kittyts_emergency_restore(&session);
    kittyts_stop(&session);
    assert(!session.framebuffer_active);
    assert(!session.keyboard_active);
    assert(!session.framebuffer.presenter_started);
    assert(kittyts_start(&session, slave, slave, &options) == 0);
    kittyts_stop(&session);
    close(slave);
    close(master);
    puts("ok");
    return 0;
}
