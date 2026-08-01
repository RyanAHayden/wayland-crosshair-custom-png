#include <gtk/gtk.h>
#include <gdk/gdkwayland.h>
#include <gtk-layer-shell.h>
#include <wayland-client.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define RADIUS 3.0

static pid_t wlsunset_pid = -1;
static const char *image_path = NULL;

/* GTK3 only ever reports an integer buffer scale (gtk_widget_get_scale_factor),
 * rounded up from the compositor's real (possibly fractional) output scale.
 * Drawing compensated only by that rounded value leaves the image short of
 * native size, because the compositor still composites the whole surface at
 * the true fractional scale. We recover the true scale ourselves by binding
 * the wl_output directly and comparing its reported physical mode (real
 * device pixels) against GTK's logical window size. */
static int32_t native_output_width = 0;

static void output_mode(void *data, struct wl_output *output, uint32_t flags,
                         int32_t width, int32_t height, int32_t refresh) {
    (void) data; (void) output; (void) height; (void) refresh;
    if (flags & WL_OUTPUT_MODE_CURRENT) {
        native_output_width = width;
    }
}
static void output_geometry(void *data, struct wl_output *output, int32_t x, int32_t y,
                             int32_t pw, int32_t ph, int32_t subpixel, const char *make,
                             const char *model, int32_t transform) {
    (void) data; (void) output; (void) x; (void) y; (void) pw; (void) ph;
    (void) subpixel; (void) make; (void) model; (void) transform;
}
static void output_scale(void *data, struct wl_output *output, int32_t factor) {
    (void) data; (void) output; (void) factor;
}
static void output_done(void *data, struct wl_output *output) {
    (void) data; (void) output;
}
static const struct wl_output_listener output_listener = {
    .geometry = output_geometry,
    .mode = output_mode,
    .done = output_done,
    .scale = output_scale,
};

static void registry_global(void *data, struct wl_registry *registry, uint32_t name,
                             const char *interface, uint32_t version) {
    (void) data;
    if (native_output_width == 0 && strcmp(interface, wl_output_interface.name) == 0) {
        struct wl_output *output = wl_registry_bind(registry, name, &wl_output_interface,
                                                      version < 2 ? version : 2);
        wl_output_add_listener(output, &output_listener, NULL);
    }
}
static void registry_global_remove(void *data, struct wl_registry *registry, uint32_t name) {
    (void) data; (void) registry; (void) name;
}
static const struct wl_registry_listener registry_listener = {
    .global = registry_global,
    .global_remove = registry_global_remove,
};

/* Binds the first advertised output and round-trips twice: once to receive
 * the registry's global announcements, once more to receive that output's
 * initial geometry/mode/scale/done burst. */
static void detect_native_output_resolution(void) {
    struct wl_display *display = gdk_wayland_display_get_wl_display(gdk_display_get_default());
    struct wl_registry *registry = wl_display_get_registry(display);
    wl_registry_add_listener(registry, &registry_listener, NULL);
    wl_display_roundtrip(display);
    wl_display_roundtrip(display);
}

static void kill_wlsunset(void) {
    if (wlsunset_pid > 0)
        kill(wlsunset_pid, SIGTERM);
}

static void spawn_wlsunset(const char *gamma) {
    wlsunset_pid = fork();
    if (wlsunset_pid == 0) {
        execlp("wlsunset", "wlsunset", "-T", "6501", "-t", "6500", "-g", gamma, NULL);
        _exit(1);
    }
    atexit(kill_wlsunset);
}

static gboolean on_draw(GtkWidget *w, cairo_t *cr, gpointer _) {
    (void)_;
    GtkAllocation a;
    gtk_widget_get_allocation(w, &a);
    cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
    cairo_set_source_rgba(cr, 0, 0, 0, 0);
    cairo_paint(cr);
    if (image_path) {
        static cairo_surface_t *img = NULL;
        static cairo_status_t img_status = CAIRO_STATUS_SUCCESS;

        if (!img) {
            img = cairo_image_surface_create_from_png(image_path);
            img_status = cairo_surface_status(img);
            if (img_status != CAIRO_STATUS_SUCCESS) {
                g_printerr("Failed to load PNG '%s': %s\n", image_path, cairo_status_to_string(img_status));
            }
        }

        if (img_status == CAIRO_STATUS_SUCCESS) {
            /* GTK draws in logical pixels, which Wayland then scales to physical
             * pixels using the output's true (possibly fractional) scale. Using
             * the real physical/logical ratio (rather than GTK's rounded integer
             * scale factor) keeps the image pixel-for-pixel native regardless of
             * monitor resolution or scaling. */
            double scale = gtk_widget_get_scale_factor(w);
            if (native_output_width > 0 && a.width > 0)
                scale = (double) native_output_width / (double) a.width;
            double iw = cairo_image_surface_get_width(img) / scale;
            double ih = cairo_image_surface_get_height(img) / scale;
            cairo_save(cr);
            cairo_translate(cr, (a.width - iw) / 2.0, (a.height - ih) / 2.0);
            cairo_scale(cr, 1.0 / scale, 1.0 / scale);
            cairo_set_source_surface(cr, img, 0, 0);
            /* Nearest-neighbor: cairo's default bilinear filter blurs the source
             * pixels whenever the compensating scale isn't a clean integer. */
            cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_NEAREST);
            cairo_paint(cr);
            cairo_restore(cr);
            return FALSE;
        }
        /* fall through to default dot on error */
    }

    cairo_set_source_rgb(cr, 0, 1, 0);
    cairo_arc(cr, a.width / 2.0, a.height / 2.0, RADIUS, 0, 2 * G_PI);
    cairo_fill(cr);
    return FALSE;
}

static void activate(GtkApplication *app, gpointer _) {
    (void)_;
    detect_native_output_resolution();

    GtkWidget *win = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_application_add_window(GTK_APPLICATION(app), GTK_WINDOW(win));
    gtk_window_set_decorated(GTK_WINDOW(win), FALSE);

    GdkScreen *screen = gtk_window_get_screen(GTK_WINDOW(win));
    GdkVisual *visual = gdk_screen_get_rgba_visual(screen);
    if (visual) gtk_widget_set_visual(win, visual);
    gtk_widget_set_app_paintable(win, TRUE);

    gtk_layer_init_for_window(GTK_WINDOW(win));
    gtk_layer_set_layer(GTK_WINDOW(win), GTK_LAYER_SHELL_LAYER_OVERLAY);
    gtk_layer_set_keyboard_mode(GTK_WINDOW(win), GTK_LAYER_SHELL_KEYBOARD_MODE_NONE);
    gtk_layer_set_anchor(GTK_WINDOW(win), GTK_LAYER_SHELL_EDGE_LEFT,   TRUE);
    gtk_layer_set_anchor(GTK_WINDOW(win), GTK_LAYER_SHELL_EDGE_RIGHT,  TRUE);
    gtk_layer_set_anchor(GTK_WINDOW(win), GTK_LAYER_SHELL_EDGE_TOP,    TRUE);
    gtk_layer_set_anchor(GTK_WINDOW(win), GTK_LAYER_SHELL_EDGE_BOTTOM, TRUE);
    /* -1 = ignore exclusive zones from bars/docks, use full screen dimensions */
    gtk_layer_set_exclusive_zone(GTK_WINDOW(win), -1);

    GtkWidget *da = gtk_drawing_area_new();
    g_signal_connect(da, "draw", G_CALLBACK(on_draw), NULL);
    gtk_container_add(GTK_CONTAINER(win), da);

    gtk_widget_show_all(win);

    cairo_region_t *empty = cairo_region_create();
    gdk_window_input_shape_combine_region(gtk_widget_get_window(win), empty, 0, 0);
    cairo_region_destroy(empty);
}

int main(int argc, char **argv) {
    const char *gamma = NULL;
    for (int i = 1; i < argc; i++) {
        if ((strcmp(argv[i], "--image") == 0 || strcmp(argv[i], "-i") == 0) && i + 1 < argc) {
            image_path = argv[++i];
        } else if ((strcmp(argv[i], "--gamma") == 0 || strcmp(argv[i], "-g") == 0) && i + 1 < argc) {
            gamma = argv[++i];
        } else {
            gamma = argv[i];
        }
    }
    if (gamma)
        spawn_wlsunset(gamma);

    GtkApplication *app = gtk_application_new("se.n1k0.crosshair", G_APPLICATION_DEFAULT_FLAGS);
    g_signal_connect(app, "activate", G_CALLBACK(activate), NULL);
    int status = g_application_run(G_APPLICATION(app), 0, NULL);
    g_object_unref(app);
    return status;
}
