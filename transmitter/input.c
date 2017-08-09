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

#include "config.h"

#include <stdlib.h>
#include <assert.h>
#include <string.h>

/* for fake stuff */
#include <math.h>

#include "compositor.h"
#include "helpers.h"

#include "plugin.h"
#include "transmitter_api.h"

/** @file
 *
 * This is an implementation of a remote input.
 *
 * Request wl_data_device_manager.get_data_device would need to be blocked,
 * except maybe it's not necessary, we just "forget" to forward data to/from
 * the remote wl_seat. It might still work inside the local compositor.
 *
 * weston_compositor_set_default_pointer_grab() will break our pointer
 * implementation, but no in-tree code is calling it.
 */

/* XXX: all functions and variables with a name, and things marked with a
 * comment, containing the word "fake" are mockups that need to be
 * removed from the final implementation.
 */

static void
pointer_focus_grab_handler(struct weston_pointer_grab *grab)
{
	/* No-op:
	 *
	 * Weston internal events do not change the focus.
	 */
}

static void
pointer_motion_grab_handler(struct weston_pointer_grab *grab,
			    uint32_t time,
			    struct weston_pointer_motion_event *event)
{
	weston_log("Unexpected! %s(pointer=%p, ...)\n",
		   __func__, grab->pointer);
}

static void
pointer_button_grab_handler(struct weston_pointer_grab *grab,
			    uint32_t time,
			    uint32_t button,
			    uint32_t state)
{
	weston_log("Unexpected! %s(pointer=%p, ...)\n",
		   __func__, grab->pointer);
}

static void
pointer_axis_grab_handler(struct weston_pointer_grab *grab,
			  uint32_t time,
			  struct weston_pointer_axis_event *event)
{
	weston_log("Unexpected! %s(pointer=%p, ...)\n",
		   __func__, grab->pointer);
}

static void
pointer_axis_source_grab_handler(struct weston_pointer_grab *grab,
				 uint32_t source)
{
	weston_log("Unexpected! %s(pointer=%p, ...)\n",
		   __func__, grab->pointer);
}

static void
pointer_frame_grab_handler(struct weston_pointer_grab *grab)
{
	weston_log("Unexpected! %s(pointer=%p, ...)\n",
		   __func__, grab->pointer);
}

static void
pointer_cancel_grab_handler(struct weston_pointer_grab *grab)
{
	weston_log("Unexpected! %s(pointer=%p, ...)\n",
		   __func__, grab->pointer);
}

/* These handlers would be called from the notify_*() functions in src/input.c.
 * However, as we do not use the low level input notify_*() functions that
 * backends drive, these are mostly uncalled, except the focus handler which
 * weston core generates internally.
 */
static const struct weston_pointer_grab_interface pointer_grab_impl = {
	pointer_focus_grab_handler,
	pointer_motion_grab_handler,
	pointer_button_grab_handler,
	pointer_axis_grab_handler,
	pointer_axis_source_grab_handler,
	pointer_frame_grab_handler,
	pointer_cancel_grab_handler,
};

/* The different ways to get pointer focus on a remoted surface:
 *
 * 1. Transmitter seat has pointer. The client has wl_pointer. Transmitter
 *    receives pointer.enter. (transmitter_seat_pointer_enter())
 *
 * 2. Transmitter seat has pointer. Transmitter has received pointer.enter.
 *    The client calls wl_seat.get_pointer. => send enter only on the new
 *    wl_pointer. (seat_get_pointer_handler())
 *
 * 3. Client has wl_pointer. Transmitter seat adds pointer capability.
 *    Transmitter receives pointer.enter. wl_pointer MUST NOT enter,
 *    specified by wl_seat.capabilities.
 *
 * By definition, Transmitter cannot receive pointer.enter without having
 * pointer capability in the seat, so no other combinations are possible.
 *
 * The same applies to wl_keyboard and wl_touch.
 */

/* Implementor notes:
 *
 * The handling of all of wl_pointer, wl_keyboard and wl_touch should be
 * similar. To make it work, we need to add a signal to each of the
 * wl_seat.get_pointer, wl_seat.get_keyboard, and wl_seat.get_touch request
 * handlers in Weston core. Otherwise we cannot implement the case 2 of gaining
 * input device focus.
 *
 * However, weston_keyboard::focus is a weston_surface, not a weston_view, so
 * we may be able to leverage more of the core implementation and maybe do
 * without the wl_seat.get_keyboard signal. Weston_touch uses a weston_view, so
 * that is similar to weston_pointer.
 *
 * It might be useful to convert weston_keyboard and weston_touch to use a
 * similar thing as weston_pointer_client, in case it makes things more
 * consistent. It might also fix issues when a client has multiple copies of a
 * wl_keyboard or a wl_touch, but that is getting off-topic.
 *
 * This file shows which part of the Weston input path we skip and where we
 * hook in. We skip everything starting from the notify_*() API used by
 * backends, and stub out the grab handlers. Instead of actual grab handlers,
 * we have our own network protocol events handlers. They do much of the same
 * as normal grab handlers would do, except focus is pre-given, and we do not
 * have weston_view for the focus surfaces, so we need to bypass core code
 * dealing with those.
 *
 * Our remote seat implementation will leave many struct members unused and
 * replicate some from weston_pointer, weston_keyboard, and weston_touch.
 * Weston core must be kept out from the focus handling business, because we
 * will send enter/leave events ourselves, and focus assignments are given
 * to us from the remote, they cannot be changed at will by the local Weston.
 */

/** Callback from the protocol request handler for wl_seat.get_pointer
 *
 * The Weston core handler never sees focus set on the weston_pointer,
 * so it won't send wl_pointer.enter nor set focus_client. It does call
 * weston_pointer_ensure_pointer_client() though.
 */
static void
seat_get_pointer_handler(struct wl_listener *listener, void *data)
{
	struct wl_resource *new_pointer = data;
	struct weston_transmitter_seat *seat;
	struct wl_resource *surface;
	struct weston_pointer_client *pointer_client;
	struct wl_client *client;
	struct weston_pointer *pointer;

	seat = wl_container_of(listener, seat, get_pointer_listener);
	if (!seat->pointer_focus)
		return;

	client = wl_resource_get_client(new_pointer);
	surface = seat->pointer_focus->surface->resource;

	if (wl_resource_get_client(surface) != client)
		return;

	pointer = weston_seat_get_pointer(&seat->base);
	assert(pointer); /* guaranteed by having pointer_focus */
	pointer_client = weston_pointer_get_pointer_client(pointer, client);

	if (!pointer->focus_client)
		pointer->focus_client = pointer_client;
	else
		assert(pointer->focus_client == pointer_client);

	wl_pointer_send_enter(new_pointer, pointer->focus_serial, surface,
			      seat->pointer_surface_x, seat->pointer_surface_y);

	if (wl_resource_get_version(new_pointer) >=
	    WL_POINTER_FRAME_SINCE_VERSION)
		wl_pointer_send_frame(new_pointer);
}

static void
transmitter_seat_create_pointer(struct weston_transmitter_seat *seat)
{
	struct weston_pointer *pointer;

	seat->pointer_phase = 0.0;
	seat->pointer_surface_x = wl_fixed_from_int(-1000000);
	seat->pointer_surface_y = wl_fixed_from_int(-1000000);
	seat->pointer_focus = NULL;
	wl_list_init(&seat->pointer_focus_destroy_listener.link);

	weston_seat_init_pointer(&seat->base);

	seat->get_pointer_listener.notify = seat_get_pointer_handler;
	wl_signal_add(&seat->base.get_pointer_signal,
		      &seat->get_pointer_listener);

	pointer = weston_seat_get_pointer(&seat->base);

	/* not exported:
	 * weston_pointer_set_default_grab(pointer, &pointer_grab_impl); */
	pointer->default_grab.interface = &pointer_grab_impl;

	/* Changes to local outputs are irrelevant. */
	wl_list_remove(&pointer->output_destroy_listener.link);
	wl_list_init(&pointer->output_destroy_listener.link);

	weston_log("Transmitter created pointer=%p for seat %p\n",
		   pointer, &seat->base);
}

static void
seat_pointer_focus_destroy_handler(struct wl_listener *listener, void *data)
{
	struct weston_transmitter_surface *txs = data;
	struct weston_transmitter_seat *seat;

	seat = wl_container_of(listener, seat, pointer_focus_destroy_listener);
	assert(seat->pointer_focus == txs);

	seat->pointer_focus = NULL;
}

void
transmitter_seat_pointer_enter(struct weston_transmitter_seat *seat,
			       uint32_t serial,
			       struct weston_transmitter_surface *txs,
			       wl_fixed_t surface_x,
			       wl_fixed_t surface_y)
{
	struct wl_client *client;
	struct weston_pointer *pointer;
	struct wl_list *focus_resource_list;
	struct wl_resource *resource;

	pointer = weston_seat_get_pointer(&seat->base);
	assert(pointer);

	assert(txs->surface);
	client = wl_resource_get_client(txs->surface->resource);

	seat->pointer_focus = txs;
	seat->pointer_focus_destroy_listener.notify =
		seat_pointer_focus_destroy_handler;
	wl_signal_add(&txs->destroy_signal,
		      &seat->pointer_focus_destroy_listener);

	/* If pointer-focus gets destroyed, txs will get destroyed, the
	 * remote surface object is destroyed, and the remote will send a
	 * leave and a frame.
	 */

	seat->pointer_surface_x = surface_x;
	seat->pointer_surface_y = surface_y;

	pointer->focus_client = weston_pointer_get_pointer_client(pointer,
								  client);
	pointer->focus_serial = serial;

	/* pointer->focus is not used, because it is a weston_view, while
	 * remoted surfaces have no views.
	 *
	 * pointer->x,y are not used because they are in global coordinates.
	 * Remoted surfaces are not in the global space at all, so there are
	 * no such coordinates.
	 */

	if (!pointer->focus_client)
		return;

	focus_resource_list = &pointer->focus_client->pointer_resources;
	wl_resource_for_each(resource, focus_resource_list) {
		wl_pointer_send_enter(resource,
				      serial,
				      txs->surface->resource,
				      surface_x, surface_y);
	}
}

void
transmitter_seat_pointer_leave(struct weston_transmitter_seat *seat,
			       uint32_t serial,
			       struct weston_transmitter_surface *txs)
{
	struct weston_pointer *pointer;
	struct wl_list *focus_resource_list;
	struct wl_resource *surface_resource;
	struct wl_resource *resource;

	if (txs != seat->pointer_focus) {
		weston_log("Transmitter Warning: pointer leave for %p, expected %p\n",
			   txs, seat->pointer_focus);
	}

	seat->pointer_focus = NULL;
	wl_list_remove(&seat->pointer_focus_destroy_listener.link);
	wl_list_init(&seat->pointer_focus_destroy_listener.link);

	if (!txs)
		return;
	assert(txs->surface);
	surface_resource = txs->surface->resource;

	pointer = weston_seat_get_pointer(&seat->base);
	assert(pointer);
	if (!pointer->focus_client)
		return;

	focus_resource_list = &pointer->focus_client->pointer_resources;
	wl_resource_for_each(resource, focus_resource_list)
		wl_pointer_send_leave(resource, serial, surface_resource);

	/* Do not reset pointer->focus_client, because we need to be able
	 * to send a following 'frame' event in
	 * transmitter_seat_pointer_frame().
	 */
}

void
transmitter_seat_pointer_motion(struct weston_transmitter_seat *seat,
				uint32_t time,
				wl_fixed_t surface_x,
				wl_fixed_t surface_y)
{
	struct weston_pointer *pointer;
	struct wl_list *focus_resource_list;
	struct wl_resource *resource;
	struct weston_transmitter_surface *txs;

	pointer = weston_seat_get_pointer(&seat->base);
	assert(pointer);

	seat->pointer_surface_x = surface_x;
	seat->pointer_surface_y = surface_y;

	if (!pointer->focus_client)
		return;

	txs = seat->pointer_focus;
	if (txs)
		assert(wl_resource_get_client(txs->surface->resource) ==
		       pointer->focus_client->client);

	focus_resource_list = &pointer->focus_client->pointer_resources;
	wl_resource_for_each(resource, focus_resource_list) {
		wl_pointer_send_motion(resource, time,
				       surface_x, surface_y);
	}
}

void
transmitter_seat_pointer_button(struct weston_transmitter_seat *seat,
				uint32_t serial,
				uint32_t time,
				uint32_t button,
				uint32_t state)
{
	struct weston_pointer *pointer;
	struct wl_list *focus_resource_list;
	struct wl_resource *resource;
	struct weston_transmitter_surface *txs;

	pointer = weston_seat_get_pointer(&seat->base);
	assert(pointer);

	if (!pointer->focus_client)
		return;

	txs = seat->pointer_focus;
	if (txs)
		assert(wl_resource_get_client(txs->surface->resource) ==
		       pointer->focus_client->client);

	focus_resource_list = &pointer->focus_client->pointer_resources;
	wl_resource_for_each(resource, focus_resource_list) {
		wl_pointer_send_button(resource, serial, time,
				       button, state);
        }
}

void
transmitter_seat_pointer_axis(struct weston_transmitter_seat *seat,
			      uint32_t time,
			      uint32_t axis,
			      wl_fixed_t value)
{
	struct weston_pointer *pointer;
	struct wl_list *focus_resource_list;
	struct wl_resource *resource;
	struct weston_transmitter_surface *txs;

	pointer = weston_seat_get_pointer(&seat->base);
	assert(pointer);

	if (!pointer->focus_client)
		return;

	txs = seat->pointer_focus;
	if (txs)
		assert(wl_resource_get_client(txs->surface->resource) ==
		       pointer->focus_client->client);

	focus_resource_list = &pointer->focus_client->pointer_resources;
	wl_resource_for_each(resource, focus_resource_list) {
		wl_pointer_send_axis(resource, time,
				     axis, value);
	}
}

void
transmitter_seat_pointer_frame(struct weston_transmitter_seat *seat)
{
	struct weston_pointer *pointer;

	pointer = weston_seat_get_pointer(&seat->base);
	if (pointer)
		weston_pointer_send_frame(pointer);
}

void
transmitter_seat_pointer_axis_source(struct weston_transmitter_seat *seat,
				     uint32_t axis_source)
{
	assert(!"TODO");
}

void
transmitter_seat_pointer_axis_stop(struct weston_transmitter_seat *seat,
				   uint32_t time,
				   uint32_t axis)
{
	assert(!"TODO");
}

void
transmitter_seat_pointer_axis_discrete(struct weston_transmitter_seat *seat,
				       uint32_t axis,
				       int32_t discrete)
{
	assert(!"TODO");
}

static char *
make_seat_name(struct weston_transmitter_remote *remote, const char *name)
{
	char *str;

	if (asprintf(&str, "transmitter-%s-%s", remote->addr, name) < 0)
		return NULL;

	return str;
}

void
transmitter_seat_destroy(struct weston_transmitter_seat *seat)
{
	wl_list_remove(&seat->link);

	weston_log("Transmitter destroy seat=%p\n", &seat->base);

	weston_seat_release(&seat->base);

	wl_list_remove(&seat->get_pointer_listener.link);
	wl_list_remove(&seat->pointer_focus_destroy_listener.link);

	if (seat->pointer_timer)
		wl_event_source_remove(seat->pointer_timer);

	free(seat);
}

static void
pointer_handle_enter(struct wthp_pointer *wthp_pointer,
		     uint32_t serial,
		     struct wthp_surface *surface,
		     wth_fixed_t surface_x,
		     wth_fixed_t surface_y)
{
	struct waltham_display *dpy =
		wth_object_get_user_data((struct wth_object *)wthp_pointer);
	struct weston_transmitter_remote *remote = dpy->remote;
	struct wl_list *seat_list = &remote->seat_list;
	struct weston_transmitter_seat *seat;
	struct weston_transmitter_surface *txs;

	seat = container_of(seat_list->next,
			    struct weston_transmitter_seat, link);

	wl_list_for_each(txs, &remote->surface_list, link)
	{
		if (txs->wthp_surf == surface) {
			transmitter_seat_pointer_enter(seat, serial, txs,
						       surface_x, surface_y);
		}
	}
}

static void
pointer_handle_leave(struct wthp_pointer *wthp_pointer,
		     uint32_t serial,
		     struct wthp_surface *surface)
{
	struct waltham_display *dpy =
		wth_object_get_user_data((struct wth_object *)wthp_pointer);
	struct weston_transmitter_remote *remote = dpy->remote;
	struct wl_list *seat_list = &remote->seat_list;
	struct weston_transmitter_seat *seat;
	struct weston_transmitter_surface *txs;

	seat = container_of(seat_list->next,
			    struct weston_transmitter_seat, link);

	wl_list_for_each(txs, &remote->surface_list, link)
	{
		if (txs->wthp_surf == surface) {
			transmitter_seat_pointer_leave(seat, serial, txs);
		}
	}
}

static void
pointer_handle_motion(struct wthp_pointer *wthp_pointer,
		      uint32_t time,
		      wth_fixed_t surface_x,
		      wth_fixed_t surface_y)
{
	struct waltham_display *dpy =
		wth_object_get_user_data((struct wth_object *)wthp_pointer);
	struct weston_transmitter_remote *remote = dpy->remote;
	struct wl_list *seat_list = &remote->seat_list;
	struct weston_transmitter_seat *seat;

	seat = container_of(seat_list->next,
			    struct weston_transmitter_seat, link);

	transmitter_seat_pointer_motion(seat, time,
					surface_x,
					surface_y);
}

static void
pointer_handle_button(struct wthp_pointer *wthp_pointer,
		      uint32_t serial,
		      uint32_t time,
		      uint32_t button,
		      uint32_t state)
{
	struct waltham_display *dpy =
		wth_object_get_user_data((struct wth_object *)wthp_pointer);
	struct weston_transmitter_remote *remote = dpy->remote;
	struct wl_list *seat_list = &remote->seat_list;
	struct weston_transmitter_seat *seat;

	seat = container_of(seat_list->next,
			    struct weston_transmitter_seat, link);

	transmitter_seat_pointer_button(seat, serial,
					time, button,
					state);
}

static void
pointer_handle_axis(struct wthp_pointer *wthp_pointer,
		    uint32_t time,
		    uint32_t axis, wth_fixed_t value)
{
	struct waltham_display *dpy =
		wth_object_get_user_data((struct wth_object *)wthp_pointer);
	struct weston_transmitter_remote *remote = dpy->remote;
	struct wl_list *seat_list = &remote->seat_list;
	struct weston_transmitter_seat *seat;

	seat = container_of(seat_list->next,
			    struct weston_transmitter_seat, link);

	transmitter_seat_pointer_axis(seat, time,
				      axis, value);
}

static void
pointer_handle_frame(struct wthp_pointer *wthp_pointer)
{
	weston_log("pointer_handle_frame\n");
}

static void
pointer_handle_axis_source(struct wthp_pointer *wthp_pointer,
			   uint32_t axis_source)
{
	weston_log("pointer_handle_axis_source\n");
}

static void
pointer_handle_axis_stop(struct wthp_pointer *wthp_pointer,
			 uint32_t time,
			 uint32_t axis)
{
	weston_log("pointer_handle_axis_stop\n");
}

static void
pointer_handle_axis_discrete(struct wthp_pointer *wthp_pointer,
			     uint32_t axis,
			     int32_t discrete)
{
	weston_log("pointer_handle_axis_discrete\n");
}

static const struct wthp_pointer_listener pointer_listener = {
	pointer_handle_enter,
	pointer_handle_leave,
	pointer_handle_motion,
	pointer_handle_button,
	pointer_handle_axis,
	pointer_handle_frame,
	pointer_handle_axis_source,
	pointer_handle_axis_stop,
	pointer_handle_axis_discrete
};

void
seat_capabilities(struct wthp_seat *wthp_seat,
		  enum wthp_seat_capability caps)
{
	struct waltham_display *dpy = wth_object_get_user_data((struct wth_object *)wthp_seat);

	weston_log("seat_capabilities\n");

	if ((caps & WTHP_SEAT_CAPABILITY_POINTER) && !dpy->pointer)
	{
		weston_log("WTHP_SEAT_CAPABILITY_POINTER\n");
		dpy->pointer = wthp_seat_get_pointer(dpy->seat);
		wthp_pointer_set_listener(dpy->pointer, &pointer_listener, dpy);
	}
}

int
transmitter_remote_create_seat(struct weston_transmitter_remote *remote)
{
	struct weston_transmitter_seat *seat = NULL;
	char *name = NULL;

	seat = zalloc(sizeof *seat);
	if (!seat)
		goto fail;

	wl_list_init(&seat->get_pointer_listener.link);
	wl_list_init(&seat->pointer_focus_destroy_listener.link);

	/* XXX: get the name from remote */
	name = make_seat_name(remote, "default");
	if (!name)
		goto fail;

	weston_seat_init(&seat->base, remote->transmitter->compositor, name);
	free(name);

	/* Hide the weston_seat from the rest of Weston, there are too many
	 * things making assumptions:
	 * - backends assume they control all seats
	 * - shells assume they control all input foci
	 * We do not want either to mess with our seat.
	 */
	wl_list_remove(&seat->base.link);
	wl_list_init(&seat->base.link);

	/* The weston_compositor::seat_created_signal has already been
	 * emitted. Shells use it to subscribe to focus changes, but we should
	 * never handle focus with weston core... except maybe with keyboard.
	 * text-backend.c will also act on the new seat.
	 * It is possible weston_seat_init() needs to be split to fix this
	 * properly.
	 */

	weston_log("Transmitter created seat=%p '%s'\n",
		   &seat->base, seat->base.seat_name);

	/* XXX: mirror remote capabilities */
	transmitter_seat_create_pointer(seat);

	wl_list_insert(&remote->seat_list, &seat->link);

	return 0;

fail:
	free(seat);
	free(name);

	return -1;
}

static void
fake_pointer_get_position(struct weston_transmitter_seat *seat, double step,
			  wl_fixed_t *x, wl_fixed_t *y)
{
	double s, c;

	seat->pointer_phase += step;
	while (seat->pointer_phase > 2.0 * M_PI)
		seat->pointer_phase -= 2.0 * M_PI;

	sincos(seat->pointer_phase, &s, &c);
	*x = wl_fixed_from_double(100.0 + 50.0 * c);
	*y = wl_fixed_from_double(100.0 + 50.0 * s);
}

static int
fake_pointer_timer_handler(void *data)
{
	struct weston_transmitter_seat *seat = data;
	wl_fixed_t x, y;
	uint32_t time;

	time = weston_compositor_get_time();

	fake_pointer_get_position(seat, 18.0 / 180.0 * M_PI, &x, &y);
	transmitter_seat_pointer_motion(seat, time, x, y);
	transmitter_seat_pointer_frame(seat);

	wl_event_source_timer_update(seat->pointer_timer, 100);

	return 0;
}

int
transmitter_seat_fake_pointer_input(struct weston_transmitter_seat *seat,
				    struct weston_transmitter_surface *txs)
{
	struct wl_event_loop *loop;
	wl_fixed_t x, y;
	uint32_t serial = 5;

	/* remove focus from earlier surface */
	transmitter_seat_pointer_leave(seat, serial++, seat->pointer_focus);
	transmitter_seat_pointer_frame(seat);

	/* set pointer focus to surface */
	fake_pointer_get_position(seat, 0.0, &x, &y);
	transmitter_seat_pointer_enter(seat, serial++, txs, x, y);
	transmitter_seat_pointer_frame(seat);

	if (!seat->pointer_timer) {
		/* schedule timer for motion */
		loop = wl_display_get_event_loop(seat->base.compositor->wl_display);
		seat->pointer_timer = wl_event_loop_add_timer(loop,
						fake_pointer_timer_handler, seat);
		wl_event_source_timer_update(seat->pointer_timer, 100);
	}

	/* XXX: if the now focused surface disappears, we should call
	 * transmitter_seat_pointer_leave() as part of the mockup. Otherwise
	 * you get a "Transmitter Warning: no pointer->focus_client?".
	 */

	return 0;
}
