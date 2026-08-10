#include "kitty_terminal_session.h"

#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <pty.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>

enum {
    SAMPLE_COUNT = 7,
    LIFECYCLE_ITERATIONS = 1000,
    FORWARD_ITERATIONS = 5000000
};

static uint64_t monotonic_nanoseconds(void)
{
    struct timespec now;

    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) return 0u;
    return (uint64_t)now.tv_sec * UINT64_C(1000000000) +
           (uint64_t)now.tv_nsec;
}

static void drain_descriptor(int fd)
{
    unsigned char buffer[4096];

    for (;;) {
        const ssize_t count = read(fd, buffer, sizeof buffer);

        if (count > 0) continue;
        if (count < 0 && errno == EINTR) continue;
        break;
    }
}

static int compare_u64(const void *left, const void *right)
{
    const uint64_t a = *(const uint64_t *)left;
    const uint64_t b = *(const uint64_t *)right;

    return a > b ? 1 : a < b ? -1 : 0;
}

static void init_options(kittyts_options *options)
{
    kittyts_options_init(options);
    options->framebuffer.manage_alt_screen = false;
    options->framebuffer.hide_cursor = false;
    options->framebuffer.probe_graphics = false;
    options->framebuffer.install_winch_handler = false;
    options->framebuffer.transport = KITTYFB_TRANSPORT_INLINE;
    options->framebuffer.min_width = 16;
    options->framebuffer.max_width = 16;
    options->framebuffer.min_height = 8;
    options->framebuffer.max_height = 8;
    options->keyboard.probe_timeout_ms = 0;
}

int main(void)
{
    struct winsize window = {24, 80, 640, 480};
    kittyts_session session;
    kittyts_options options;
    kittyin_event event;
    uint64_t lifecycle_samples[SAMPLE_COUNT];
    uint64_t geometry_samples[SAMPLE_COUNT];
    uint64_t event_samples[SAMPLE_COUNT];
    volatile uint64_t checksum = 0u;
    int master = -1;
    int slave = -1;
    int master_flags;
    size_t sample;

    if (openpty(&master, &slave, NULL, NULL, &window) != 0) {
        perror("openpty");
        return EXIT_FAILURE;
    }
    master_flags = fcntl(master, F_GETFL);
    if (master_flags < 0 ||
        fcntl(master, F_SETFL, master_flags | O_NONBLOCK) != 0) {
        perror("fcntl");
        return EXIT_FAILURE;
    }
    init_options(&options);
    kittyts_session_init(&session);

    for (sample = 0u; sample < SAMPLE_COUNT; ++sample) {
        const uint64_t started = monotonic_nanoseconds();
        int iteration;

        for (iteration = 0; iteration < LIFECYCLE_ITERATIONS; ++iteration) {
            if (kittyts_start(&session, slave, slave, &options) != 0 ||
                kittyts_read_input(&session) != 0) {
                perror("lifecycle");
                return EXIT_FAILURE;
            }
            kittyts_stop(&session);
            drain_descriptor(master);
        }
        lifecycle_samples[sample] = monotonic_nanoseconds() - started;
    }

    if (kittyts_start(&session, slave, slave, &options) != 0) {
        perror("start");
        return EXIT_FAILURE;
    }
    drain_descriptor(master);
    for (sample = 0u; sample < SAMPLE_COUNT; ++sample) {
        const uint64_t started = monotonic_nanoseconds();
        int iteration;

        for (iteration = 0; iteration < FORWARD_ITERATIONS; ++iteration) {
            checksum += (uint64_t)kittyts_width(&session);
            checksum += (uint64_t)kittyts_height(&session);
            checksum += (uint64_t)kittyts_cell_width(&session);
            checksum += (uint64_t)kittyts_cell_height(&session);
        }
        geometry_samples[sample] = monotonic_nanoseconds() - started;
    }
    for (sample = 0u; sample < SAMPLE_COUNT; ++sample) {
        const uint64_t started = monotonic_nanoseconds();
        int iteration;

        for (iteration = 0; iteration < FORWARD_ITERATIONS; ++iteration)
            checksum += kittyts_next_event(&session, &event) ? 1u : 0u;
        event_samples[sample] = monotonic_nanoseconds() - started;
    }
    kittyts_stop(&session);
    drain_descriptor(master);

    qsort(lifecycle_samples, SAMPLE_COUNT, sizeof lifecycle_samples[0],
          compare_u64);
    qsort(geometry_samples, SAMPLE_COUNT, sizeof geometry_samples[0],
          compare_u64);
    qsort(event_samples, SAMPLE_COUNT, sizeof event_samples[0], compare_u64);
    (void)printf("lifecycle_ns_per_cycle=%.1f\n",
                 (double)lifecycle_samples[SAMPLE_COUNT / 2] /
                 (double)LIFECYCLE_ITERATIONS);
    (void)printf("geometry_ns_per_call=%.3f\n",
                 (double)geometry_samples[SAMPLE_COUNT / 2] /
                 ((double)FORWARD_ITERATIONS * 4.0));
    (void)printf("empty_event_ns_per_call=%.3f\n",
                 (double)event_samples[SAMPLE_COUNT / 2] /
                 (double)FORWARD_ITERATIONS);
    (void)printf("checksum=%" PRIu64 "\n", checksum);

    if (close(slave) != 0 || close(master) != 0) {
        perror("close");
        return EXIT_FAILURE;
    }
    return checksum != 0u ? EXIT_SUCCESS : EXIT_FAILURE;
}
