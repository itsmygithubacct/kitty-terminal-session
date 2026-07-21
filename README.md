# kitty-terminal-session

`kitty-terminal-session` owns the lifecycle shared by terminal games that use
[`kitty-framebuffer`](https://github.com/itsmygithubacct/kitty-framebuffer) and
[`kitty-input`](https://github.com/itsmygithubacct/kitty-input). It starts
the framebuffer first, lets it own raw mode and nonblocking input, then pushes
the keyboard, mouse, focus, and paste modes. Normal and emergency shutdown
always unwind those layers in the opposite order.

The library also provides the thin forwarding calls games otherwise repeat:
frame presentation, resize checks, ordered input events, held-key state, and
release-event capability. The 0.1 keyboard-only API remains source
compatible; C callers also retain the public `keyboard` session alias.

## Checkout and test

```sh
git clone --recurse-submodules \
  https://github.com/itsmygithubacct/kitty-terminal-session.git
cd kitty-terminal-session
make test
make sanitize
```

The public header is `include/kitty_terminal_session.h`. Call
`kittyts_options_init()`, customize the nested framebuffer or keyboard
options, initialize a `kittyts_session`, and start it. Fatal-signal handlers
may call `kittyts_emergency_restore()`; ordinary cleanup calls
`kittyts_stop()`.

Mouse, focus, and bracketed-paste modes default off so existing games retain
their exact wire behavior. Opt into them on `kittyts_options`, then consume
the ordered queue with `kittyts_next_event()`. `kittyts_next_key_event()`
remains available and skips non-key events; do not mix the two queue APIs.

## Scope

This repository deliberately does not contain rendering, sound, game input
bindings, or game-loop policy. Those remain in their own libraries or in the
game that gives them meaning.

## License

MIT. See `LICENSE`. The pinned dependencies retain their own notices.
