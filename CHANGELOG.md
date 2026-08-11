# Changelog

## 0.4.0

- Forward framebuffer 0.5 retained-scroll composition and its statistics.
- Preserve full-frame behavior on standard terminals through the framebuffer's
  capability and cost fallbacks.
- Pin the framebuffer provider revision that implements the 0.5 protocol.

## 0.3.0

- Keep distinct input descriptors nonblocking while avoiding duplicate flag
  ownership when input and output are the same descriptor.
- Delegate emergency restoration to each dependency's live state so wrapper
  bookkeeping and an ordinary teardown claim cannot suppress signal cleanup.
- Reset the public output descriptor after framebuffer-start failure.
- Forward framebuffer 0.4 damage presentation, health, statistics, transport,
  resize notification, and shared-memory orphan cleanup.
- Expand lifecycle coverage for rollback, suspension, descriptor restoration,
  emergency cleanup, ordered modes, input dispatch, and damage forwarding.
- Add isolated sanitizer builds, a reproducible benchmark, staged installation,
  and explicit dependency/version contracts.

## 0.2.0

- Add ordered mouse, focus, and bracketed-paste input while preserving the 0.1
  keyboard alias for C callers.
- Add framebuffer suspension/restart and ordinary cleanup after emergency
  restoration.

## 0.1.0

- Add the combined framebuffer and keyboard terminal lifecycle.
