/*
 * Copyright (C) 2016 DENSO CORPORATION
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial
 * portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT.  IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#ifndef WESTON_TRANSMITTER_PLUGIN_H
#define WESTON_TRANSMITTER_PLUGIN_H

/* XXX: all functions and variables with a name, and things marked with a
 * comment, containing the word "fake" are mockups that need to be
 * removed from the final implementation.
 */

#include <stdint.h>
#include <wayland-client.h>

#include "compositor.h"
#include "transmitter_api.h"

#include <waltham-client.h>

struct waltham_display;

/* epoll structure */
struct watch { 
	struct waltham_display *display;
	int fd;
	void (*cb)(struct watch *w, uint32_t events);
};

struct waltham_display {
	struct wth_connection *connection;
	struct watch conn_watch;
	struct wth_display *display;

	bool running;
	int epoll_fd;

	struct wthp_registry *registry;

	struct wthp_callback *bling;

	struct wthp_compositor *compositor;
	struct wthp_blob_factory *blob_factory;
	struct wthp_seat *seat;
        struct ivi_application *application;
	struct wtimer *fiddle_timer;

	struct weston_transmitter_remote *remote;
        char *addr;
        char *port;
};

/* a timerfd based timer */
struct wtimer {
	struct watch watch;
	void (*func)(struct wtimer *, void *);
	void *data;
};

struct weston_transmitter {
	struct weston_compositor *compositor;
	struct wl_listener compositor_destroy_listener;

	struct wl_list remote_list; /* transmitter_remote::link */

	struct wl_listener connection_listener;
	struct wl_listener stream_listener;

	int epoll_fd;
};

struct weston_transmitter_remote {
	struct weston_transmitter *transmitter;
	struct wl_list link;
	char *model;
	char *addr;
	char *port;

	enum weston_transmitter_connection_status status;
	struct wl_signal connection_status_signal;
        struct wl_signal conn_establish_signal;

	struct wl_list output_list; /* weston_transmitter_output::link */
	struct wl_list surface_list; /* weston_transmitter_surface::link */
	struct wl_list seat_list; /* weston_transmitter_seat::link */

        struct wl_listener establish_listener;

	struct wl_event_source *conn_timer; /* fake */
        struct wl_event_source *establish_timer; /* for establish connection */
	struct wl_event_source *retry_timer; /* for retry connection */

	struct waltham_display *display; /* waltham */
};


struct weston_transmitter_surface {
	struct weston_transmitter_remote *remote;
	struct wl_list link; /* weston_transmitter_remote::surface_list */
	struct wl_signal destroy_signal; /* data: weston_transmitter_surface */

	enum weston_transmitter_stream_status status;
	struct wl_signal stream_status_signal;

	struct weston_surface *surface;
	struct wl_listener surface_destroy_listener;
	struct wl_listener apply_state_listener;

	weston_transmitter_ivi_resize_handler_t resize_handler;
	void *resize_handler_data;

	struct weston_output *sync_output;
	struct wl_listener sync_output_destroy_listener;

	struct wl_event_source *map_timer; /* fake */
	struct wl_event_source *frame_timer; /* fake */

	int32_t attach_dx; /**< wl_surface.attach(buffer, dx, dy) */
	int32_t attach_dy; /**< wl_surface.attach(buffer, dx, dy) */
	struct wl_list frame_callback_list; /* weston_frame_callback::link */
	struct wl_list feedback_list; /* weston_presentation_feedback::link */

	/* waltham */
	struct wthp_surface *wthp_surf;
	struct wthp_blob_factory *wthp_blob;
	struct wthp_buffer *wthp_buf;
        struct ivi_surface *ivi_surface;
        struct ivi_application *ivi_application;
};

struct weston_transmitter_output_info {
	uint32_t subpixel; /* enum wl_output_subpixel */
	uint32_t transform; /* enum wl_output_transform */
	int32_t scale;
	int32_t x;
	int32_t y;
	int32_t width_mm;
	int32_t height_mm;
	/* char *make; is WESTON_TRANSMITTER_OUTPUT_MAKE */
	char *model;

	struct weston_mode mode;
};

struct weston_transmitter_output {
	struct weston_output base;

	struct {
		bool draw_initial_frame;
		struct wl_surface *surface;
		struct wl_output *output;
		struct wl_display *display;
		int configure_width, configure_height;
		bool wait_for_configure;
	} parent;

	struct weston_transmitter_remote *remote;
	struct wl_list link; /* weston_transmitter_remote::output_list */

	struct frame *frame;

	struct wl_callback *frame_cb;
};

struct weston_transmitter_seat {
	struct weston_seat base;
	struct wl_list link;

	/* pointer */
	wl_fixed_t pointer_surface_x;
	wl_fixed_t pointer_surface_y;

	struct wl_listener get_pointer_listener;
	struct weston_transmitter_surface *pointer_focus;
	struct wl_listener pointer_focus_destroy_listener;

	struct wl_event_source *pointer_timer; /* fake */

	double pointer_phase; /* fake */

	/* keyboard */

	/* touch */
};

void
transmitter_surface_ivi_resize(struct weston_transmitter_surface *txs,
			       int32_t width, int32_t height);

int
transmitter_remote_create_output(struct weston_transmitter_remote *remote,
			const struct weston_transmitter_output_info *info);

void
transmitter_output_destroy(struct weston_transmitter_output *output);

int
transmitter_remote_create_seat(struct weston_transmitter_remote *remote);

void
transmitter_seat_destroy(struct weston_transmitter_seat *seat);

/* The below are the functions to be called from the network protocol
 * input event handlers.
 */

void
transmitter_seat_pointer_enter(struct weston_transmitter_seat *seat,
			       uint32_t serial,
			       struct weston_transmitter_surface *txs,
			       wl_fixed_t surface_x,
			       wl_fixed_t surface_y);

void
transmitter_seat_pointer_leave(struct weston_transmitter_seat *seat,
			       uint32_t serial,
			       struct weston_transmitter_surface *txs);

void
transmitter_seat_pointer_motion(struct weston_transmitter_seat *seat,
				uint32_t time,
				wl_fixed_t surface_x,
				wl_fixed_t surface_y);

void
transmitter_seat_pointer_button(struct weston_transmitter_seat *seat,
				uint32_t serial,
				uint32_t time,
				uint32_t button,
				uint32_t state);

void
transmitter_seat_pointer_axis(struct weston_transmitter_seat *seat,
			      uint32_t time,
			      uint32_t axis,
			      wl_fixed_t value);

void
transmitter_seat_pointer_frame(struct weston_transmitter_seat *seat);

void
transmitter_seat_pointer_axis_source(struct weston_transmitter_seat *seat,
				     uint32_t axis_source);

void
transmitter_seat_pointer_axis_stop(struct weston_transmitter_seat *seat,
				   uint32_t time,
				   uint32_t axis);

void
transmitter_seat_pointer_axis_discrete(struct weston_transmitter_seat *seat,
				       uint32_t axis,
				       int32_t discrete);

/* Fake functions for mockup testing: */

int
transmitter_seat_fake_pointer_input(struct weston_transmitter_seat *seat,
				    struct weston_transmitter_surface *txs);


#endif /* WESTON_TRANSMITTER_PLUGIN_H */
