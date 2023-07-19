#include <linux/input-event-codes.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#include <wayland-client-protocol.h>
#include <wayland-client.h>

#include "shm.h"
#include "xdg-shell-client-protocol.h"

struct client_state {
    /* Globals */
    struct wl_display *wl_display;
    struct wl_registry *wl_registry;
    struct wl_shm *wl_shm;
    struct wl_compositor *wl_compositor;
    struct xdg_wm_base *xdg_wm_base;
    /* Objects */
    struct wl_seat *wl_seat;
    struct wl_surface *wl_surface;
    struct xdg_surface *xdg_surface;
    struct xdg_toplevel *xdg_toplevel;
    struct wl_pointer *wl_pointer;
    /* State */
    bool running;
    int width;
    int height;
};

static void
noop() {
    // This space intentionally left blank
}

static void
xdg_surface_handle_configure(void *data,
                             struct xdg_surface *xdg_surface,
                             uint32_t serial) {
    struct client_state *state = data;
    xdg_surface_ack_configure(xdg_surface, serial);
    wl_surface_commit(state->wl_surface);
}

static const struct xdg_surface_listener xdg_surface_listener = {
    .configure = xdg_surface_handle_configure,
};

static void
xdg_toplevel_handle_close(void *data, struct xdg_toplevel *xdg_toplevel) {
    struct client_state *state = data;
    state->running = false;
}

static const struct xdg_toplevel_listener xdg_toplevel_listener = {
    .configure = noop,
    .close = xdg_toplevel_handle_close,
};

static void
pointer_handle_button(void *data,
                      struct wl_pointer *pointer,
                      uint32_t serial,
                      uint32_t time,
                      uint32_t button,
                      uint32_t state) {
    struct client_state *client_state = data;

    if (button == BTN_LEFT && state == WL_POINTER_BUTTON_STATE_PRESSED) {
        xdg_toplevel_move(
            client_state->xdg_toplevel, client_state->wl_seat, serial);
    }
}

static const struct wl_pointer_listener pointer_listener = {
    .enter = noop,
    .leave = noop,
    .motion = noop,
    .button = pointer_handle_button,
    .axis = noop,
};

static void
seat_handle_capabilities(void *data,
                         struct wl_seat *seat,
                         uint32_t capabilities) {
    struct client_state *state = data;

    bool has_pointer = capabilities & WL_SEAT_CAPABILITY_POINTER;
    if (has_pointer && !state->wl_pointer) {
        state->wl_pointer = wl_seat_get_pointer(seat);
        wl_pointer_add_listener(state->wl_pointer, &pointer_listener, state);
    } else if (!has_pointer && state->wl_pointer) {
        wl_pointer_destroy(state->wl_pointer);
        state->wl_pointer = NULL;
    }
}

static const struct wl_seat_listener seat_listener = {
    .capabilities = seat_handle_capabilities,
};

static void
handle_global(void *data,
              struct wl_registry *registry,
              uint32_t name,
              const char *interface,
              uint32_t version) {
    struct client_state *state = data;
    if (strcmp(interface, wl_shm_interface.name) == 0) {
        state->wl_shm = wl_registry_bind(registry, name, &wl_shm_interface, 1);
    } else if (strcmp(interface, wl_seat_interface.name) == 0) {
        state->wl_seat =
            wl_registry_bind(registry, name, &wl_seat_interface, 1);
        wl_seat_add_listener(state->wl_seat, &seat_listener, state);
    } else if (strcmp(interface, wl_compositor_interface.name) == 0) {
        state->wl_compositor =
            wl_registry_bind(registry, name, &wl_compositor_interface, 1);
    } else if (strcmp(interface, xdg_wm_base_interface.name) == 0) {
        state->xdg_wm_base =
            wl_registry_bind(registry, name, &xdg_wm_base_interface, 1);
    }
}

static void
handle_global_remove(void *data, struct wl_registry *registry, uint32_t name) {
    // Who cares
}

static const struct wl_registry_listener registry_listener = {
    .global = handle_global,
    .global_remove = handle_global_remove,
};

static struct wl_buffer *
create_buffer(struct client_state *state) {
    int stride = state->width * 4;
    int size = stride * state->height;

    int fd = create_shm_file(size);
    if (fd < 0) {
        fprintf(stderr, "creating a buffer file for %d B failed: %m\n", size);
        return NULL;
    }

    uint32_t *data =
        mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (data == MAP_FAILED) {
        fprintf(stderr, "mmap failed: %m\n");
        close(fd);
        return NULL;
    }

    struct wl_shm_pool *pool = wl_shm_create_pool(state->wl_shm, fd, size);
    struct wl_buffer *buffer = wl_shm_pool_create_buffer(
        pool, 0, state->width, state->height, stride, WL_SHM_FORMAT_ARGB8888);
    wl_shm_pool_destroy(pool);

    /* Draw checkerboxed background */
    for (int y = 0; y < state->height; ++y) {
        for (int x = 0; x < state->width; ++x) {
            if ((x + y / 8 * 8) % 16 < 8)
                data[y * state->width + x] = 0xFF666666;
            else
                data[y * state->width + x] = 0xFFEEEEEE;
        }
    }

    return buffer;
}

int
main(int argc, char *argv[]) {
    struct client_state state = {
        .running = true,
        .width = 480,
        .height = 480,
    };

    state.wl_display = wl_display_connect(NULL);
    if (state.wl_display == NULL) {
        fprintf(stderr, "failed to create display\n");
        return EXIT_FAILURE;
    }

    struct wl_registry *registry = wl_display_get_registry(state.wl_display);
    wl_registry_add_listener(registry, &registry_listener, &state);
    wl_display_roundtrip(state.wl_display);

    if (state.wl_shm == NULL || state.wl_compositor == NULL ||
        state.xdg_wm_base == NULL) {
        fprintf(stderr, "no wl_shm, wl_compositor or xdg_wm_base support\n");
        return EXIT_FAILURE;
    }

    struct wl_buffer *buffer = create_buffer(&state);
    if (buffer == NULL) {
        return EXIT_FAILURE;
    }

    state.wl_surface = wl_compositor_create_surface(state.wl_compositor);
    struct xdg_surface *xdg_surface =
        xdg_wm_base_get_xdg_surface(state.xdg_wm_base, state.wl_surface);
    state.xdg_toplevel = xdg_surface_get_toplevel(xdg_surface);

    xdg_surface_add_listener(xdg_surface, &xdg_surface_listener, &state);
    xdg_toplevel_add_listener(
        state.xdg_toplevel, &xdg_toplevel_listener, &state);

    wl_surface_commit(state.wl_surface);
    wl_display_roundtrip(state.wl_display);

    wl_surface_attach(state.wl_surface, buffer, 0, 0);
    wl_surface_commit(state.wl_surface);

    while (wl_display_dispatch(state.wl_display) != -1 && state.running) {
        // This space intentionally left blank
    }

    xdg_toplevel_destroy(state.xdg_toplevel);
    xdg_surface_destroy(state.xdg_surface);
    wl_surface_destroy(state.wl_surface);
    wl_buffer_destroy(buffer);

    return EXIT_SUCCESS;
}
