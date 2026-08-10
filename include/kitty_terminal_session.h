#ifndef KITTY_TERMINAL_SESSION_H
#define KITTY_TERMINAL_SESSION_H

#include "kitty_framebuffer.h"
#include "kitty_input_posix.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define KITTYTS_VERSION_MAJOR 0
#define KITTYTS_VERSION_MINOR 3
#define KITTYTS_VERSION_PATCH 0

#if KITTYFB_VERSION_MAJOR == 0 && KITTYFB_VERSION_MINOR < 4
#error "kitty-terminal-session 0.3 requires kitty-framebuffer 0.4 or newer"
#endif
#if KITTYIN_VERSION_MAJOR == 0 &&                                      \
    (KITTYIN_VERSION_MINOR < 2 ||                                     \
     (KITTYIN_VERSION_MINOR == 2 && KITTYIN_VERSION_PATCH < 1))
#error "kitty-terminal-session 0.3 requires kitty-input 0.2.1 or newer"
#endif

typedef struct kittyts_options {
    kittyfb_options framebuffer;
    kittykb_terminal_options keyboard;
    kittyin_mouse_tracking mouse_tracking;
    bool pixel_mouse;
    bool focus_events;
    bool bracketed_paste;
} kittyts_options;

typedef struct kittyts_session {
    kittyfb_session framebuffer;
#ifdef __cplusplus
    kittyin_terminal input;
#else
    union {
        kittyin_terminal input;
        struct {
            /* Source-compatible alias for 0.1 callers. */
            kittykb_terminal keyboard;
        };
    };
#endif
    int output_fd;
    bool framebuffer_active;
    bool framebuffer_suspended;
    bool keyboard_active;
    volatile int shutdown_claimed;
    volatile int cleanup_claimed;
} kittyts_session;

void kittyts_options_init(kittyts_options *options);
/* Initialize once; the same object supports repeated start/stop cycles. */
void kittyts_session_init(kittyts_session *session);

/* Borrow input_fd/output_fd until suspend or stop. They may be distinct. */
int kittyts_start(kittyts_session *session, int input_fd, int output_fd,
                  const kittyts_options *options);
/* Suspend retains framebuffer heap capacity; final stop releases it. */
void kittyts_suspend(kittyts_session *session);
void kittyts_stop(kittyts_session *session);
/* Async-signal-safe best effort. Always follow with kittyts_stop normally. */
void kittyts_emergency_restore(kittyts_session *session);

int kittyts_width(const kittyts_session *session);
int kittyts_height(const kittyts_session *session);
int kittyts_cell_width(const kittyts_session *session);
int kittyts_cell_height(const kittyts_session *session);

/*
 * Where the frame's top-left corner sits inside the terminal, in pixels;
 * see kittyfb_origin_x().  A pixel mouse report is relative to the
 * terminal, so subtracting these turns one into a coordinate in the frame
 * the application drew - which is the whole reason this session enables
 * pixel mouse coordinates by default.
 */
int kittyts_origin_x(const kittyts_session *session);
int kittyts_origin_y(const kittyts_session *session);
bool kittyts_check_resize(kittyts_session *session, int *width, int *height);
bool kittyts_present(kittyts_session *session, const uint8_t *rgba,
                     int width, int height);
kittyfb_transport kittyts_active_transport(const kittyts_session *session);
bool kittyts_failed(const kittyts_session *session);
void kittyts_get_stats(kittyts_session *session, kittyfb_stats *out);
void kittyts_notify_resize(void);
int kittyts_reap_orphans(void);

/*
 * Present only the parts of a frame that changed, patching the image
 * already on screen rather than retransmitting it.  Right for an
 * interactive application, where moving the pointer changes a few hundred
 * pixels and a full frame costs the whole image again.
 *
 * See kittyfb_present_damage() for the details.  Rectangles are in
 * framebuffer pixels with x1/y1 exclusive, and it falls back to a full
 * frame by itself whenever patching cannot help - so a caller can use it
 * unconditionally and never has to decide which is cheaper.
 */
bool kittyts_present_damage(kittyts_session *session, const uint8_t *rgba,
                            int width, int height,
                            const kittyfb_rect *rects, size_t rect_count);

int kittyts_read_input(kittyts_session *session);
bool kittyts_next_event(kittyts_session *session, kittyin_event *event);
/* Compatibility helper: discards queued non-key events; do not mix APIs. */
bool kittyts_next_key_event(kittyts_session *session, kittykb_event *event);
bool kittyts_key_down(const kittyts_session *session, uint32_t key);
bool kittyts_has_release_events(const kittyts_session *session);

#ifdef __cplusplus
}
#endif

#endif
