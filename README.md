# kitty-terminal-session

`kitty-terminal-session` is the single-object lifecycle aggregate for games
that use [`kitty-framebuffer`](https://github.com/itsmygithubacct/kitty-framebuffer)
and [`kitty-input`](https://github.com/itsmygithubacct/kitty-input). Its static
library includes both dependencies and provides one start, suspend, emergency,
and stop boundary plus thin presentation and input forwarding calls.

Version 0.4 requires `kitty-framebuffer` 0.5 or newer and `kitty-input` 0.2.1
or newer. Dependent objects must be rebuilt because the framebuffer 0.5 public
statistics layout changed.

## Build, test, and install

Clone with submodules, then use the maintained entry points:

```sh
git clone --recurse-submodules \
  https://github.com/itsmygithubacct/kitty-terminal-session.git
cd kitty-terminal-session
make test
make sanitize
make benchmark
make DESTDIR="$staging" PREFIX=/usr install
```

`BUILD_DIR`, `KITTY_FRAMEBUFFER_DIR`, `KITTY_INPUT_DIR`, and
`KITTY_KEYBOARD_DIR` are overridable. Sanitizers use a separate nested build
directory and do not replace the normal artifacts. Installation provides the
aggregate static archive and every public dependency header it exposes; link
consumers with `-lz -pthread`.

## Lifecycle and descriptor ownership

Initialize options and a session once, customize the options, then reuse that
session through any number of start/stop cycles:

```c
kittyts_options options;
kittyts_session session;

kittyts_options_init(&options);
kittyts_session_init(&session);
if (kittyts_start(&session, input_fd, output_fd, &options) != 0) {
    /* inspect errno */
}
/* render and read events */
kittyts_stop(&session);
```

Descriptors are borrowed, never closed, and may be distinct. By default the
framebuffer owns raw mode on the input descriptor and nonblocking mode on the
output descriptor; the input layer owns nonblocking mode on a distinct input
descriptor. When both arguments are the same descriptor, duplicate `fcntl`
bookkeeping is skipped. All saved state is restored on failure, suspension,
and stop. Callers that disable both nested ownership options must provide the
equivalent terminal state themselves.

Start enters the framebuffer first and then enables keyboard, mouse, focus,
and paste modes. Shutdown reverses that exact order. `kittyts_suspend()` joins
the presenter and restores the terminal while retaining high-water heap
buffers; a later start reuses them. A final stop releases them. Lifecycle calls
belong on one owning thread and are idempotent, but concurrent ordinary
start/suspend/stop calls are not supported.

Fatal-signal handlers may call `kittyts_emergency_restore()`. It performs only
the signal-safe, best-effort terminal restoration and fences presentation; an
ordinary `kittyts_stop()` is still required when execution continues so the
presenter, memory, signal disposition, and saved descriptor bookkeeping are
reclaimed.

## Presentation and input

`kittyts_present()` queues a full RGBA frame. `kittyts_present_damage()` edits
small changed rectangles in the displayed frame and automatically falls back
to a full presentation when patching would be unsafe or more expensive.
`kittyts_present_scroll()` shifts retained pixels in the terminal and sends
only exposed strips plus caller-supplied chrome damage. Kilix Kitty composes
the overlap on its GPU when available and uses its CPU upload path otherwise;
standard terminals automatically receive a full frame.
`kittyts_failed()`, `kittyts_get_stats()`, and
`kittyts_active_transport()` expose presenter health and transport evidence;
resize notification and shared-memory orphan reaping are available through
the corresponding `kittyts_` calls.

Mouse, focus, and bracketed-paste modes default off to preserve existing game
wire behavior. Opt in on `kittyts_options`, call `kittyts_read_input()`, and
consume the ordered queue with `kittyts_next_event()`. The compatibility helper
`kittyts_next_key_event()` skips and permanently discards non-key events, so do
not mix the two queue APIs. Held-key state is available only after the terminal
confirms release-event capability.

## Scope

This repository does not contain rendering, sound, semantic game bindings, or
game-loop policy. Those remain in their dedicated libraries or in the game
that gives them meaning.

## License

MIT. See `LICENSE`. The bundled dependencies retain their own notices.
