#include "kitty_terminal_session.h"

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <pty.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#define CHECK(condition)                                                      \
    do {                                                                      \
        if (!(condition)) {                                                   \
            (void)fprintf(stderr, "%s:%d: check failed: %s\n",             \
                          __FILE__, __LINE__, #condition);                     \
            return false;                                                     \
        }                                                                     \
    } while (false)

typedef struct test_pty {
    int master;
    int slave;
    int slave_flags;
    struct termios termios;
} test_pty;

static int64_t monotonic_milliseconds(void)
{
    struct timespec now;

    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) return 0;
    return (int64_t)now.tv_sec * 1000 + (int64_t)now.tv_nsec / 1000000;
}

static bool open_test_pty(test_pty *pty)
{
    struct winsize window = {24, 80, 640, 480};
    int master_flags;

    if (pty == NULL) return false;
    *pty = (test_pty){-1, -1, -1, {0}};
    if (openpty(&pty->master, &pty->slave, NULL, NULL, &window) != 0)
        return false;
    master_flags = fcntl(pty->master, F_GETFL);
    pty->slave_flags = fcntl(pty->slave, F_GETFL);
    if (master_flags < 0 || pty->slave_flags < 0 ||
        tcgetattr(pty->slave, &pty->termios) != 0 ||
        fcntl(pty->master, F_SETFL, master_flags | O_NONBLOCK) != 0) {
        (void)close(pty->slave);
        (void)close(pty->master);
        pty->master = -1;
        pty->slave = -1;
        return false;
    }
    return true;
}

static bool close_test_pty(test_pty *pty)
{
    bool ok = true;

    if (pty->slave >= 0 && close(pty->slave) != 0) ok = false;
    if (pty->master >= 0 && close(pty->master) != 0) ok = false;
    pty->slave = -1;
    pty->master = -1;
    return ok;
}

static bool same_termios(const struct termios *left,
                         const struct termios *right)
{
    return left->c_iflag == right->c_iflag &&
           left->c_oflag == right->c_oflag &&
           left->c_cflag == right->c_cflag &&
           left->c_lflag == right->c_lflag &&
           memcmp(left->c_cc, right->c_cc, sizeof left->c_cc) == 0;
}

static size_t read_available(int fd, void *output, size_t capacity)
{
    unsigned char *bytes = output;
    size_t total = 0u;

    while (total < capacity) {
        const ssize_t count = read(fd, bytes + total, capacity - total);

        if (count > 0) total += (size_t)count;
        else if (count < 0 && errno == EINTR) continue;
        else break;
    }
    return total;
}

static size_t find_bytes(const void *haystack, size_t haystack_size,
                         const void *needle, size_t needle_size)
{
    const unsigned char *bytes = haystack;
    size_t index;

    if (needle_size == 0u || needle_size > haystack_size) return SIZE_MAX;
    for (index = 0u; index + needle_size <= haystack_size; ++index)
        if (memcmp(bytes + index, needle, needle_size) == 0) return index;
    return SIZE_MAX;
}

static bool contains_string(const void *haystack, size_t haystack_size,
                            const char *needle)
{
    return find_bytes(haystack, haystack_size, needle, strlen(needle)) !=
           SIZE_MAX;
}

static size_t count_string(const void *haystack, size_t haystack_size,
                           const char *needle)
{
    const unsigned char *bytes = haystack;
    const size_t needle_size = strlen(needle);
    size_t offset = 0u;
    size_t count = 0u;

    while (offset + needle_size <= haystack_size) {
        const size_t found = find_bytes(bytes + offset,
                                        haystack_size - offset,
                                        needle, needle_size);
        if (found == SIZE_MAX) break;
        ++count;
        offset += found + needle_size;
    }
    return count;
}

static void drain_descriptor(int fd)
{
    unsigned char scratch[8192];

    (void)read_available(fd, scratch, sizeof scratch);
}

static void init_options(kittyts_options *options)
{
    kittyts_options_init(options);
    options->framebuffer.probe_graphics = false;
    options->framebuffer.install_winch_handler = false;
    options->framebuffer.transport = KITTYFB_TRANSPORT_INLINE;
    options->framebuffer.min_width = 16;
    options->framebuffer.max_width = 16;
    options->framebuffer.min_height = 8;
    options->framebuffer.max_height = 8;
    options->keyboard.probe_timeout_ms = 0;
}

static bool wait_for_encoded_frame(kittyts_session *session, int master,
                                   uint64_t minimum)
{
    const int64_t deadline = monotonic_milliseconds() + 3000;

    while (monotonic_milliseconds() < deadline) {
        kittyfb_stats stats = {0};
        struct pollfd descriptor = {master, POLLIN, 0};

        kittyts_get_stats(session, &stats);
        if (stats.frames_encoded >= minimum) return true;
        (void)poll(&descriptor, 1u, 10);
        drain_descriptor(master);
    }
    return false;
}

static bool test_defaults_and_inactive_api(void)
{
    kittyts_options options;
    kittyts_session session;
    kittyfb_rect rect = {0, 0, 1, 1};
    uint8_t pixel[4] = {0, 0, 0, 255};
    kittyin_event input_event;
    kittykb_event key_event;

    kittyts_options_init(&options);
    kittyts_session_init(&session);
    CHECK(KITTYTS_VERSION_MAJOR == 0);
    CHECK(KITTYTS_VERSION_MINOR == 4);
    CHECK(options.framebuffer.manage_raw_mode);
    CHECK(!options.keyboard.make_raw);
    CHECK(options.keyboard.make_nonblocking);
    CHECK(options.keyboard.flags == KITTYKB_FLAGS_KEY_STATE);
    CHECK(options.mouse_tracking == KITTYIN_MOUSE_TRACKING_OFF);
    CHECK(options.pixel_mouse);
    CHECK(!options.focus_events);
    CHECK(!options.bracketed_paste);
    CHECK(&session.keyboard == &session.input.keyboard);
    CHECK(session.output_fd == -1);
    CHECK(!session.framebuffer_active && !session.framebuffer_suspended &&
          !session.keyboard_active);

    errno = 0;
    CHECK(kittyts_start(NULL, 0, 1, &options) == -1 && errno == EINVAL);
    errno = 0;
    CHECK(kittyts_start(&session, -1, 1, &options) == -1 && errno == EINVAL);
    CHECK(kittyts_width(NULL) == 0 && kittyts_height(&session) == 0);
    CHECK(kittyts_cell_width(&session) == 0 &&
          kittyts_cell_height(&session) == 0);
    CHECK(!kittyts_check_resize(&session, NULL, NULL));
    CHECK(!kittyts_present(&session, pixel, 1, 1));
    CHECK(!kittyts_present_damage(&session, pixel, 1, 1, &rect, 1u));
    CHECK(!kittyts_present_scroll(
        &session, pixel, 1, 1, 0, -1, &rect, 1u));
    CHECK(!kittyts_present_scroll_region(
        &session, pixel, 1, 1, &rect, 0, -1, NULL, 0u));
    CHECK(kittyts_active_transport(NULL) == KITTYFB_TRANSPORT_INLINE);
    CHECK(!kittyts_failed(NULL));
    errno = 0;
    CHECK(kittyts_read_input(&session) == -1 && errno == EINVAL);
    CHECK(!kittyts_next_event(&session, &input_event));
    CHECK(!kittyts_next_key_event(&session, &key_event));
    CHECK(!kittyts_key_down(&session, (uint32_t)'a'));
    CHECK(!kittyts_has_release_events(&session));
    kittyts_suspend(NULL);
    kittyts_stop(NULL);
    kittyts_emergency_restore(NULL);
    return true;
}

static bool test_lifecycle_and_ordered_input(void)
{
    static const char mixed_input[] =
        "\x1b[<0;12;8M\x1b[I\x1b[200~hello\x1b[201~";
    test_pty pty;
    kittyts_session session;
    kittyts_options options;
    kittykb_event key_event;
    kittyin_event event;
    struct termios current;
    unsigned char controls[4096];
    size_t used;
    size_t disable_at;
    size_t keyboard_pop_at;
    size_t framebuffer_stop_at;

    CHECK(open_test_pty(&pty));
    kittyts_session_init(&session);
    init_options(&options);
    options.mouse_tracking = KITTYIN_MOUSE_TRACKING_MOTION;
    options.focus_events = true;
    options.bracketed_paste = true;
    CHECK(kittyts_start(&session, pty.slave, pty.slave, &options) == 0);
    CHECK((fcntl(pty.slave, F_GETFL) & O_NONBLOCK) != 0);
    CHECK(!session.input.keyboard.has_saved_fd_flags);
    CHECK(tcgetattr(pty.slave, &current) == 0);
    CHECK((current.c_lflag & ICANON) == 0);
    CHECK(kittyts_width(&session) == 16 && kittyts_height(&session) == 8);
    CHECK(kittyts_cell_width(&session) > 0 &&
          kittyts_cell_height(&session) > 0);
    /* A 16x8 frame occupies two 8-pixel columns and one 20-pixel row in
     * the 80x24, 640x480 test terminal.  Pixel mouse reports are relative
     * to the terminal, so callers subtract this centered origin. */
    CHECK(kittyts_origin_x(&session) == 312);
    CHECK(kittyts_origin_y(&session) == 220);
    CHECK(kittyts_origin_x(NULL) == 0 && kittyts_origin_y(NULL) == 0);
    CHECK(kittyts_active_transport(&session) == KITTYFB_TRANSPORT_INLINE);
    CHECK(!kittyts_failed(&session));
    errno = 0;
    CHECK(kittyts_start(&session, pty.slave, pty.slave, &options) == -1 &&
          errno == EBUSY);

    used = read_available(pty.master, controls, sizeof controls);
    CHECK(contains_string(controls, used, "\x1b[?1049h"));
    CHECK(contains_string(controls, used, "\x1b[>"));
    CHECK(contains_string(controls, used, "\x1b[?1003h"));
    CHECK(contains_string(controls, used, "\x1b[?1004h"));
    CHECK(contains_string(controls, used, "\x1b[?2004h"));

    CHECK(write(pty.master, "\x1b[119;1:1u", 10u) == 10);
    CHECK(kittyts_read_input(&session) == 10);
    CHECK(kittyts_next_key_event(&session, &key_event));
    CHECK(key_event.key == (uint32_t)'w');
    CHECK(key_event.action == KITTYKB_ACTION_PRESS);
    /* A zero-timeout blind start deliberately reports no release-event
     * capability, so held state must stay unavailable even though the
     * decoded event itself carries a press action. */
    CHECK(!kittyts_has_release_events(&session));
    CHECK(!kittyts_key_down(&session, (uint32_t)'w'));

    CHECK(write(pty.master, mixed_input, sizeof mixed_input - 1u) ==
          (ssize_t)(sizeof mixed_input - 1u));
    CHECK(kittyts_read_input(&session) == (int)(sizeof mixed_input - 1u));
    CHECK(kittyts_next_event(&session, &event));
    CHECK(event.kind == KITTYIN_EVENT_MOUSE);
    CHECK(event.data.mouse.x == 11 && event.data.mouse.y == 7);
    CHECK(kittyts_next_event(&session, &event));
    CHECK(event.kind == KITTYIN_EVENT_FOCUS && event.data.focus.focused);
    CHECK(kittyts_next_event(&session, &event));
    CHECK(event.kind == KITTYIN_EVENT_PASTE && event.data.paste.final);
    CHECK(event.data.paste.length == 5u);
    CHECK(memcmp(event.data.paste.bytes, "hello", 5u) == 0);
    CHECK(!kittyts_next_event(&session, &event));

    /* The compatibility API intentionally discards non-key events. */
    CHECK(write(pty.master, "\x1b[Ox", 4u) == 4);
    CHECK(kittyts_read_input(&session) == 4);
    CHECK(kittyts_next_key_event(&session, &key_event));
    CHECK(key_event.key == (uint32_t)'x');
    CHECK(!kittyts_next_key_event(&session, &key_event));

    kittyts_stop(&session);
    kittyts_stop(&session);
    CHECK(!session.framebuffer_active && !session.framebuffer_suspended &&
          !session.keyboard_active && session.output_fd == -1);
    CHECK(session.shutdown_claimed == 0);
    CHECK(fcntl(pty.slave, F_GETFL) == pty.slave_flags);
    CHECK(tcgetattr(pty.slave, &current) == 0);
    CHECK(same_termios(&pty.termios, &current));

    used = read_available(pty.master, controls, sizeof controls);
    disable_at = find_bytes(controls, used, "\x1b[?2004l", 8u);
    keyboard_pop_at = find_bytes(controls, used, "\x1b[<u", 4u);
    framebuffer_stop_at = find_bytes(controls, used, "\x1b[?1049l", 8u);
    CHECK(disable_at != SIZE_MAX && keyboard_pop_at != SIZE_MAX &&
          framebuffer_stop_at != SIZE_MAX);
    CHECK(disable_at < keyboard_pop_at &&
          keyboard_pop_at < framebuffer_stop_at);
    CHECK(close_test_pty(&pty));
    return true;
}

static bool test_separate_descriptors_are_nonblocking(void)
{
    test_pty input;
    test_pty output;
    kittyts_session session;
    kittyts_options options;
    kittykb_event event;

    CHECK(open_test_pty(&input));
    CHECK(open_test_pty(&output));
    kittyts_session_init(&session);
    init_options(&options);
    CHECK(kittyts_start(&session, input.slave, output.slave, &options) == 0);
    CHECK((fcntl(input.slave, F_GETFL) & O_NONBLOCK) != 0);
    CHECK((fcntl(output.slave, F_GETFL) & O_NONBLOCK) != 0);
    CHECK(session.input.keyboard.has_saved_fd_flags);
    CHECK(kittyts_read_input(&session) == 0);
    CHECK(write(input.master, "q", 1u) == 1);
    CHECK(kittyts_read_input(&session) == 1);
    CHECK(kittyts_next_key_event(&session, &event));
    CHECK(event.key == (uint32_t)'q');
    kittyts_stop(&session);
    CHECK(fcntl(input.slave, F_GETFL) == input.slave_flags);
    CHECK(fcntl(output.slave, F_GETFL) == output.slave_flags);
    CHECK(close_test_pty(&output));
    CHECK(close_test_pty(&input));
    return true;
}

static bool test_start_failure_rollback(void)
{
    test_pty pty;
    kittyts_session session;
    kittyts_options options;
    struct termios current;
    unsigned char controls[4096];
    int pipe_fds[2] = {-1, -1};
    size_t used;

    CHECK(open_test_pty(&pty));
    CHECK(pipe(pipe_fds) == 0);
    kittyts_session_init(&session);
    init_options(&options);
    errno = 0;
    CHECK(kittyts_start(&session, pty.slave, pipe_fds[1], &options) == -1);
    CHECK(errno == ENOTTY);
    CHECK(session.output_fd == -1);
    CHECK(!session.framebuffer_active && !session.keyboard_active);
    CHECK(close(pipe_fds[1]) == 0);
    CHECK(close(pipe_fds[0]) == 0);

    options.mouse_tracking = (kittyin_mouse_tracking)99;
    errno = 0;
    CHECK(kittyts_start(&session, pty.slave, pty.slave, &options) == -1);
    CHECK(errno == EINVAL);
    CHECK(session.output_fd == -1);
    CHECK(!session.framebuffer_active && !session.framebuffer_suspended &&
          !session.keyboard_active);
    CHECK(fcntl(pty.slave, F_GETFL) == pty.slave_flags);
    CHECK(tcgetattr(pty.slave, &current) == 0);
    CHECK(same_termios(&pty.termios, &current));
    used = read_available(pty.master, controls, sizeof controls);
    CHECK(contains_string(controls, used, "\x1b[?1049h"));
    CHECK(contains_string(controls, used, "\x1b[?1049l"));

    init_options(&options);
    CHECK(kittyts_start(&session, pty.slave, pty.slave, &options) == 0);
    kittyts_stop(&session);
    CHECK(close_test_pty(&pty));
    return true;
}

static bool test_suspend_failed_resume_and_reuse(void)
{
    static uint8_t frame[16u * 8u * 4u];
    test_pty pty;
    kittyts_session session;
    kittyts_options options;
    struct termios current;
    uint8_t *retained;

    CHECK(open_test_pty(&pty));
    kittyts_session_init(&session);
    init_options(&options);
    CHECK(kittyts_start(&session, pty.slave, pty.slave, &options) == 0);
    CHECK(kittyts_present(&session, frame, 16, 8));
    CHECK(wait_for_encoded_frame(&session, pty.master, 1u));
    retained = session.framebuffer.encode_buffer;
    CHECK(retained != NULL);

    kittyts_suspend(&session);
    kittyts_suspend(&session);
    CHECK(!session.framebuffer_active && session.framebuffer_suspended &&
          !session.keyboard_active && session.output_fd == -1);
    CHECK(session.framebuffer.encode_buffer == retained);
    CHECK(fcntl(pty.slave, F_GETFL) == pty.slave_flags);
    CHECK(tcgetattr(pty.slave, &current) == 0);
    CHECK(same_termios(&pty.termios, &current));

    options.framebuffer.max_width = 15;
    errno = 0;
    CHECK(kittyts_start(&session, pty.slave, pty.slave, &options) == -1);
    CHECK(errno == EINVAL);
    CHECK(session.framebuffer_suspended && session.output_fd == -1);
    options.framebuffer.max_width = 16;
    CHECK(kittyts_start(&session, pty.slave, pty.slave, &options) == 0);
    CHECK(session.framebuffer.encode_buffer == retained);
    CHECK(session.framebuffer_active && !session.framebuffer_suspended &&
          session.keyboard_active);
    kittyts_stop(&session);
    CHECK(session.framebuffer.encode_buffer == NULL);
    CHECK(close_test_pty(&pty));
    return true;
}

static bool test_emergency_restore_without_presenter(void)
{
    test_pty pty;
    kittyts_session session;
    kittyts_options options;
    struct termios current;
    unsigned char controls[4096];
    int output_fd;
    size_t used;

    CHECK(open_test_pty(&pty));
    kittyts_session_init(&session);
    init_options(&options);
    options.mouse_tracking = KITTYIN_MOUSE_TRACKING_MOTION;
    options.focus_events = true;
    options.bracketed_paste = true;
    output_fd = dup(pty.slave);
    CHECK(output_fd >= 0);
    CHECK(kittyts_start(&session, pty.slave, output_fd, &options) == 0);
    CHECK(session.input.keyboard.has_saved_fd_flags);
    drain_descriptor(pty.master);

    /* Dependency state becomes live before the aggregate's mirror booleans
     * during startup. Emergency restoration must not trust only the mirrors. */
    session.framebuffer_active = false;
    session.keyboard_active = false;
    kittyts_emergency_restore(&session);
    kittyts_emergency_restore(&session);
    used = read_available(pty.master, controls, sizeof controls);
    CHECK(count_string(controls, used, "\x1b[<u") == 1u);
    CHECK(count_string(controls, used, "\x1b[?1049l") == 1u);
    CHECK(fcntl(pty.slave, F_GETFL) == pty.slave_flags);
    CHECK(tcgetattr(pty.slave, &current) == 0);
    CHECK(same_termios(&pty.termios, &current));

    /* Ordinary cleanup must not re-enable O_NONBLOCK through the inner
     * input layer after the framebuffer emergency path restored it. */
    session.framebuffer_active = true;
    session.keyboard_active = true;
    kittyts_stop(&session);
    CHECK(read_available(pty.master, controls, sizeof controls) == 0u);
    CHECK(fcntl(pty.slave, F_GETFL) == pty.slave_flags);
    CHECK(tcgetattr(pty.slave, &current) == 0);
    CHECK(same_termios(&pty.termios, &current));
    CHECK(!session.framebuffer.output_flags_saved);
    CHECK(!session.framebuffer.termios_saved);
    CHECK(!session.framebuffer_active && !session.framebuffer_suspended &&
          !session.keyboard_active);

    CHECK(kittyts_start(&session, pty.slave, output_fd, &options) == 0);
    kittyts_stop(&session);
    CHECK(close(output_fd) == 0);
    CHECK(close_test_pty(&pty));
    return true;
}

static bool test_framebuffer_forwarding(void)
{
    static uint8_t frame[16u * 8u * 4u];
    const kittyfb_rect damage = {0, 0, 4, 4};
    const kittyfb_rect toolbar = {0, 0, 16, 1};
    const kittyfb_rect viewport = {0, 1, 16, 8};
    test_pty pty;
    kittyts_session session;
    kittyts_options options;
    kittyfb_stats stats = {0};
    unsigned char wire[8192];
    size_t used;
    size_t index;

    for (index = 0u; index < sizeof frame; index += 4u) {
        frame[index] = (uint8_t)index;
        frame[index + 1u] = (uint8_t)(index >> 1);
        frame[index + 2u] = 127u;
        frame[index + 3u] = 255u;
    }
    CHECK(open_test_pty(&pty));
    kittyts_session_init(&session);
    init_options(&options);
    CHECK(kittyts_start(&session, pty.slave, pty.slave, &options) == 0);
    drain_descriptor(pty.master);
    CHECK(kittyts_present(&session, frame, 16, 8));
    CHECK(wait_for_encoded_frame(&session, pty.master, 1u));
    frame[0] ^= 255u;
    CHECK(kittyts_present_damage(&session, frame, 16, 8, &damage, 1u));
    kittyts_get_stats(&session, &stats);
    CHECK(stats.frames_presented == 1u);
    CHECK(stats.frames_encoded == 1u);
    CHECK(stats.damage_presents == 1u);
    CHECK(stats.damage_fallbacks == 0u);

    drain_descriptor(pty.master);
    CHECK(setenv("KITTY_KILIX_RENDERING", "1", 1) == 0);
    frame[sizeof frame - 4u] ^= 255u;
    CHECK(kittyts_present_scroll_region(
        &session, frame, 16, 8, &viewport, 0, -1, NULL, 0u));
    used = read_available(pty.master, wire, sizeof wire);
    CHECK(contains_string(wire, used, "a=c,i=1,r=1,c=1"));
    CHECK(contains_string(wire, used, "x=0,y=1,X=0,Y=2,w=16,h=6"));
    CHECK(!contains_string(wire, used, "x=0,y=0,s=16,v=1"));
    CHECK(contains_string(wire, used, "C=1,N=2,q=2"));
    kittyts_get_stats(&session, &stats);
    CHECK(stats.scroll_presents == 1u);
    CHECK(stats.scroll_fallbacks == 0u);
    CHECK(unsetenv("KITTY_KILIX_RENDERING") == 0);

    CHECK(!kittyts_failed(&session));
    kittyts_notify_resize();
    CHECK(!kittyts_check_resize(&session, NULL, NULL));
    kittyts_stop(&session);
    CHECK(!kittyts_present_damage(&session, frame, 16, 8, &damage, 1u));
    CHECK(!kittyts_present_scroll(
        &session, frame, 16, 8, 0, -1, &toolbar, 1u));
    CHECK(!kittyts_present_scroll_region(
        &session, frame, 16, 8, &viewport, 0, -1, NULL, 0u));
    CHECK(close_test_pty(&pty));
    return true;
}

int main(void)
{
    static const struct test_case {
        const char *name;
        bool (*run)(void);
    } tests[] = {
        {"defaults and inactive API", test_defaults_and_inactive_api},
        {"lifecycle and ordered input", test_lifecycle_and_ordered_input},
        {"separate descriptor nonblocking",
         test_separate_descriptors_are_nonblocking},
        {"start failure rollback", test_start_failure_rollback},
        {"suspend, failed resume, and reuse",
         test_suspend_failed_resume_and_reuse},
        {"emergency restore without presenter",
         test_emergency_restore_without_presenter},
        {"framebuffer forwarding", test_framebuffer_forwarding}
    };
    size_t index;

    for (index = 0u; index < sizeof tests / sizeof tests[0]; ++index) {
        if (!tests[index].run()) {
            (void)fprintf(stderr, "not ok - %s\n", tests[index].name);
            return 1;
        }
        (void)printf("ok - %s\n", tests[index].name);
    }
    return 0;
}
