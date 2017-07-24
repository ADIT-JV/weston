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
#include <linux/input.h>

#include "compositor.h"
#include "helpers.h"
#include "timespec-util.h"

#include "plugin.h"
#include "transmitter_api.h"

/* waltham */
#include <errno.h>
#include <sys/epoll.h>
#include <sys/timerfd.h>
#include <waltham-object.h>
#include <waltham-client.h>
#include <waltham-connection.h>

#define MAX_EPOLL_WATCHES 2
#define ESTABLISH_CONNECTION_PERIOD 500
#define RETRY_CONNECTION_PERIOD 5000

/* XXX: all functions and variables with a name, and things marked with a
 * comment, containing the word "fake" are mockups that need to be
 * removed from the final implementation.
 */

/** Send configure event through ivi-shell.
 *
 * \param txs The Transmitter surface.
 * \param width Suggestion for surface width.
 * \param height Suggestion for surface height.
 *
 * When the networking code receives a ivi_surface.configure event, it calls
 * this function to relay it to the application.
 *
 * \c txs cannot be a zombie, because transmitter_surface_zombify() must
 * tear down the network link, so a zombie cannot receive events.
 */
void
transmitter_surface_ivi_resize(struct weston_transmitter_surface *txs,
			       int32_t width, int32_t height)
{
	assert(txs->resize_handler);
	if (!txs->resize_handler)
		return;

	assert(txs->surface);
	if (!txs->surface)
		return;

	txs->resize_handler(txs->resize_handler_data, width, height);
}

static int
frame_callback_handler(void *data) /* fake */
{
	struct weston_transmitter_surface *txs = data;
	struct weston_frame_callback *cb, *cnext;
	struct weston_output *output;
	struct weston_compositor *compositor;
	uint32_t frame_time;
	uint32_t presented_flags;
	int32_t refresh_nsec;
	struct timespec stamp;

	compositor = txs->remote->transmitter->compositor;
	output = txs->sync_output;

	/* wl_surface.enter should arrive before any frame callbacks,
	 * but remote might send frame callbacks for non-visible too.
	 */
	if (!output)
		return 0;

	/* XXX: eeeew */
	frame_time = weston_compositor_get_time();

	wl_list_for_each_safe(cb, cnext, &txs->frame_callback_list, link) {
		wl_callback_send_done(cb->resource, frame_time);
		wl_resource_destroy(cb->resource);
	}

	presented_flags = 0;
	refresh_nsec = millihz_to_nsec(output->current_mode->refresh);
	/* XXX: waaahhhahaa */
	weston_compositor_read_presentation_clock(compositor, &stamp);
	weston_presentation_feedback_present_list(&txs->feedback_list,
						  output, refresh_nsec, &stamp,
						  output->msc,
						  presented_flags);

	return 0;
}

static void
fake_frame_callback(struct weston_transmitter_surface *txs)
{
	struct weston_transmitter *txr = txs->remote->transmitter;
	struct wl_event_loop *loop;

	if (!txs->frame_timer) {
		loop = wl_display_get_event_loop(txr->compositor->wl_display);
		txs->frame_timer =
			wl_event_loop_add_timer(loop,
						frame_callback_handler, txs);
	}

	wl_event_source_timer_update(txs->frame_timer, 1);
}

static void
transmitter_surface_configure(struct weston_transmitter_surface *txs,
			      int32_t dx, int32_t dy)
{
	assert(txs->surface);
	if (!txs->surface)
		return;

	txs->attach_dx += dx;
	txs->attach_dy += dy;
}

static void
buffer_send_complete(struct wthp_buffer *b, uint32_t serial)
{
	weston_log("wth_buffer.send_complete(%d)\n", serial);
	/*struct weston_transmitter_surface *txs =
	  wth_object_get_user_data((struct wth_object *)b);*/
	//frame_callback_handler(txs);
	wthp_buffer_destroy(b);
}

static const struct wthp_buffer_listener buffer_listener = {
	buffer_send_complete
};

static void
surface_render_complete(struct wthp_callback *wthp_cb, uint32_t serial)
{
        weston_log("wth_callback.surface_render_complete(%d)\n", serial);
	struct weston_transmitter_surface *txs;
	struct weston_frame_callback *cb, *cnext;
	uint32_t frame_time;

	txs = wth_object_get_user_data((struct wth_object *)wthp_cb);
	frame_time = weston_compositor_get_time();
	wl_list_for_each_safe(cb, cnext, &txs->frame_callback_list, link) {
	        wl_callback_send_done(cb->resource, frame_time);
		wl_resource_destroy(cb->resource);
	}

	wthp_callback_free(wthp_cb);
}

static const struct wthp_callback_listener callback_listener = {
        surface_render_complete
};


static void
transmitter_surface_gather_state(struct weston_transmitter_surface *txs)
{
	struct weston_transmitter *txr = txs->remote->transmitter;
	struct wthp_callback *cb;
	fprintf(stderr, "transmitter_surface_gather_state %p\n", txs); fflush(stderr);
	weston_log("Transmitter: update surface %p (%d, %d), %d cb\n",
		   txs->surface, txs->attach_dx, txs->attach_dy,
		   wl_list_length(&txs->surface->frame_callback_list));

	wl_list_insert_list(&txs->frame_callback_list,
			    &txs->surface->frame_callback_list);
	wl_list_init(&txs->surface->frame_callback_list);

	wl_list_insert_list(&txs->feedback_list, &txs->surface->feedback_list);
	wl_list_init(&txs->surface->feedback_list);


	/* TODO: transmit surface state to remote */

	/* waltham */
	struct weston_surface *surf = txs->surface;
	struct weston_compositor *comp = surf->compositor;
	int32_t stride, data_sz, width, height;
	void *data;
	
	width = 1;
	height = 1;
	weston_log("width %d height %d\n", surf->width, surf->height);
	//stride = surf->width * (PIXMAN_FORMAT_BPP(comp->read_format) / 8);
	stride = width * (PIXMAN_FORMAT_BPP(comp->read_format) / 8);
	weston_log("stride %d\n", stride);

	data = malloc(stride * height);
	data_sz = stride * height;
	weston_log("data_sz = %d\n", data_sz);

	//weston_surface_copy_content(surf, data, data_sz, 0, 0, surf->width, surf->height);
	/* fake sending buffer */
	txs->wthp_buf = wthp_blob_factory_create_buffer(txr->display->blob_factory,
							data_sz,
							data,
							surf->width,
							surf->height,
							stride,
							PIXMAN_FORMAT_BPP(comp->read_format));

	wthp_buffer_set_listener(txs->wthp_buf, &buffer_listener, txs);
	cb = wthp_surface_frame(txs->wthp_surf);
	wthp_callback_set_listener(cb, &callback_listener, txs);
	
	wthp_surface_attach(txs->wthp_surf, txs->wthp_buf, txs->attach_dx, txs->attach_dy);
	wthp_surface_damage(txs->wthp_surf, txs->attach_dx, txs->attach_dy, surf->width, surf->height);
	wthp_surface_commit(txs->wthp_surf);

	wth_connection_flush(txr->display->connection);

	txs->attach_dx = 0;
	txs->attach_dy = 0;
}

/** weston_surface apply state signal handler */
static void
transmitter_surface_apply_state(struct wl_listener *listener, void *data)
{
	struct weston_transmitter_surface *txs =
		container_of(listener, struct weston_transmitter_surface,
			     apply_state_listener);
	weston_log("get signal and apply state\n");
	assert(data == NULL);

	transmitter_surface_gather_state(txs);
//	fake_frame_callback(txs);
}

/** Mark the weston_transmitter_surface dead.
 *
 * Stop all remoting actions on this surface.
 *
 * Still keeps the pointer stored by a shell valid, so it can be freed later.
 */
static void
transmitter_surface_zombify(struct weston_transmitter_surface *txs)
{
	struct weston_frame_callback *framecb, *cnext;
	struct weston_transmitter *txr;
	weston_log("surface zombify\n");
	/* may be called multiple times */
	if (!txs->surface)
		return;

	wl_signal_emit(&txs->destroy_signal, txs);

	wl_list_remove(&txs->surface_destroy_listener.link);
	weston_log("Transmitter unbound surface %p.\n", txs->surface);
	txs->surface = NULL;

	wl_list_remove(&txs->sync_output_destroy_listener.link);
	wl_list_remove(&txs->apply_state_listener.link);

	if (txs->map_timer)
		wl_event_source_remove(txs->map_timer);
	if (txs->frame_timer)
		wl_event_source_remove(txs->frame_timer);

	weston_presentation_feedback_discard_list(&txs->feedback_list);
	wl_list_for_each_safe(framecb, cnext, &txs->frame_callback_list, link)
		wl_resource_destroy(framecb->resource);

	txr = txs->remote->transmitter;
	if (!txr->display->compositor)
		weston_log("txr->compositor is NULL\n");
	wthp_surface_destroy(txs->wthp_surf);
	ivi_surface_destroy(txs->ivi_surface);

	/* In case called from destroy_transmitter() */
	txs->remote = NULL;
}

static void
transmitter_surface_destroy(struct weston_transmitter_surface *txs)
{
	transmitter_surface_zombify(txs);

	wl_list_remove(&txs->link);
	free(txs);
}

/** weston_surface destroy signal handler */
static void
transmitter_surface_destroyed(struct wl_listener *listener, void *data)
{
	struct weston_transmitter_surface *txs =
		container_of(listener, struct weston_transmitter_surface,
			     surface_destroy_listener);

	assert(data == txs->surface);

	transmitter_surface_zombify(txs);
}

static struct weston_transmitter_surface *
transmitter_surface_get(struct weston_surface *ws)
{
	struct wl_listener *listener;
	struct weston_transmitter_surface *txs;

	listener = wl_signal_get(&ws->destroy_signal,
				 transmitter_surface_destroyed);

	if (!listener)
		return NULL;

	txs = container_of(listener, struct weston_transmitter_surface,
			   surface_destroy_listener);
	assert(ws == txs->surface);

	return txs;
}

static void
sync_output_destroy_handler(struct wl_listener *listener, void *data)
{
	struct weston_transmitter_surface *txs;

	txs = container_of(listener, struct weston_transmitter_surface,
			   sync_output_destroy_listener);

	wl_list_remove(&txs->sync_output_destroy_listener.link);
	wl_list_init(&txs->sync_output_destroy_listener.link);

	weston_surface_force_output(txs->surface, NULL);
}

static void
fake_input(struct weston_transmitter_surface *txs)
{
	struct wl_list *seat_list = &txs->remote->seat_list;
	struct weston_transmitter_seat *seat;

	assert(wl_list_length(seat_list) == 1);
	seat = container_of(seat_list->next,
			    struct weston_transmitter_seat, link);

	transmitter_seat_fake_pointer_input(seat, txs);
}

/* fake receiving wl_surface.enter(output) */
static int
map_timer_handler(void *data)
{
	struct weston_transmitter_surface *txs = data;
	struct weston_transmitter_output *output;

	assert(!wl_list_empty(&txs->remote->output_list));

	output = container_of(txs->remote->output_list.next,
			      struct weston_transmitter_output, link);

	txs->sync_output = &output->base;
	txs->sync_output_destroy_listener.notify = sync_output_destroy_handler;
	wl_list_remove(&txs->sync_output_destroy_listener.link);
	wl_signal_add(&txs->sync_output->destroy_signal,
		      &txs->sync_output_destroy_listener);

	weston_surface_force_output(txs->surface, txs->sync_output);

	weston_log("Transmitter: surface %p entered output %s\n",
		   txs->surface, txs->sync_output->name);

	fake_frame_callback(txs);
	fake_input(txs);

	return 0;
}

/* Fake a delay for the remote end to map the surface to an output */
static void
fake_output_mapping(struct weston_transmitter_surface *txs)
{
	struct weston_transmitter *txr = txs->remote->transmitter;
	struct wl_event_loop *loop;

	loop = wl_display_get_event_loop(txr->compositor->wl_display);
	txs->map_timer = wl_event_loop_add_timer(loop, map_timer_handler, txs);
	wl_event_source_timer_update(txs->map_timer, 400);
}

/* Fake getting "connection established" from the content streamer. */
static void
fake_stream_opening_handler(void *data)
{
	struct weston_transmitter_surface *txs = data;

	/* ...once the connection is up: */
	txs->status = WESTON_TRANSMITTER_STREAM_LIVE;
	wl_signal_emit(&txs->stream_status_signal, txs);

	/* need to create the surface on the remote and set all state */
	transmitter_surface_gather_state(txs);

	fake_output_mapping(txs);
}

/* Fake a callback from content streamer. */
static void
fake_stream_opening(struct weston_transmitter_surface *txs)
{
	struct weston_transmitter *txr = txs->remote->transmitter;
	struct wl_event_loop *loop;

	loop = wl_display_get_event_loop(txr->compositor->wl_display);
	wl_event_loop_add_idle(loop, fake_stream_opening_handler, txs);
}

static struct weston_transmitter_surface *
transmitter_surface_push_to_remote(struct weston_surface *ws,
				   struct weston_transmitter_remote *remote,
				   struct wl_listener *stream_status)
{
	weston_log("transmitter_surface_push_to_remote\n");
	struct weston_transmitter_surface *txs;
	struct weston_transmitter *txr;

	txs = transmitter_surface_get(ws);
	if (!txs) {
		txs = zalloc(sizeof (*txs));
		if (!txs)
			return NULL;

		txs->remote = remote;
		wl_signal_init(&txs->destroy_signal);
		wl_list_insert(&remote->surface_list, &txs->link);

		txs->status = WESTON_TRANSMITTER_STREAM_INITIALIZING;
		wl_signal_init(&txs->stream_status_signal);
		wl_signal_add(&txs->stream_status_signal, stream_status);

		txs->surface = ws;
		txs->surface_destroy_listener.notify = transmitter_surface_destroyed;
		wl_signal_add(&ws->destroy_signal, &txs->surface_destroy_listener);

		txs->apply_state_listener.notify = transmitter_surface_apply_state;
		wl_signal_add(&ws->apply_state_signal, &txs->apply_state_listener);

		wl_list_init(&txs->sync_output_destroy_listener.link);

		wl_list_init(&txs->frame_callback_list);
		wl_list_init(&txs->feedback_list);
	}
	/* TODO: create the content stream connection... */

	txr = txs->remote->transmitter;
	if (!txr->display->compositor)
		weston_log("txr->compositor is NULL\n");
	txs->wthp_surf = wthp_compositor_create_surface(txr->display->compositor);

	//fake_stream_opening(txs);

	return txs;
}

static enum weston_transmitter_stream_status
transmitter_surface_get_stream_status(struct weston_transmitter_surface *txs)
{
	return txs->status;
}

#if 0
static int
conn_timer_handler(void *data) /* fake */
{
	struct weston_transmitter_remote *remote = data;
	struct weston_transmitter_output_info info = {
		WL_OUTPUT_SUBPIXEL_NONE,
		WL_OUTPUT_TRANSFORM_NORMAL,
		1,
		0, 0,
		300, 200,
		"fake",
		{
			WL_OUTPUT_MODE_CURRENT | WL_OUTPUT_MODE_PREFERRED,
			800, 600,
			51519,
			{ NULL, NULL }
		}
	};

	weston_log("Transmitter connected to %s.\n", remote->addr);
	remote->status = WESTON_TRANSMITTER_CONNECTION_READY;
	wl_signal_emit(&remote->connection_status_signal, remote);

	wl_event_source_remove(remote->conn_timer);
	remote->conn_timer = NULL;
	/* Outputs and seats are dynamic, do not guarantee they are all
	 * present when signalling connection status.
	 */
	transmitter_remote_create_output(remote, &info);
	transmitter_remote_create_seat(remote);

	return 0;
}
#endif
/* notify connection ready */
static void
conn_ready_notify(struct wl_listener *l, void *data)
{
	struct weston_transmitter_remote *remote =
	  container_of(l, struct weston_transmitter_remote, 
		       establish_listener);
	struct weston_transmitter_output_info info = {
		WL_OUTPUT_SUBPIXEL_NONE,
		WL_OUTPUT_TRANSFORM_NORMAL,
		1,
		0, 0,
		300, 200,
		"fake",
		{
			WL_OUTPUT_MODE_CURRENT | WL_OUTPUT_MODE_PREFERRED,
			800, 600,
			51519,
			{ NULL, NULL }
		}
	};

	/* Outputs and seats are dynamic, do not guarantee they are all
	 * present when signalling connection status.
	 */

	transmitter_remote_create_output(remote, &info);
	transmitter_remote_create_seat(remote);
}

/* waltham */
/* The server advertises a global interface.
 * We can store the ad for later and/or bind to it immediately
 * if we want to.
 * We also need to keep track of the globals we bind to, so that
 * global_remove can be handled properly (not implemented).
 */
static void
registry_handle_global(struct wthp_registry *registry,
		       uint32_t name,
		       const char *interface,
		       uint32_t version)
{
	struct waltham_display *dpy = wth_object_get_user_data((struct wth_object *)registry);
	
	printf("got global %d: '%s' version '%d'\n",name, interface, version);

	if (strcmp(interface, "wthp_compositor") == 0) {
		assert(!dpy->compositor); 
		dpy->compositor = (struct wthp_compositor *)wthp_registry_bind(registry, name, interface, 1);
		/* has no events to handle */
	} else if (strcmp(interface, "wthp_blob_factory") == 0) {
		assert(!dpy->blob_factory); 
		dpy->blob_factory = (struct wthp_blob_factory *)wthp_registry_bind(registry, name, interface, 1);
		/* has no events to handle */
	} else if (strcmp(interface, "wthp_seat") == 0) {
		assert(!dpy->seat); 
		dpy->seat = (struct wthp_seat *)wthp_registry_bind(registry, name, interface, 1);
	} else if (strcmp(interface, "ivi_application") == 0) {
	        assert(!dpy->application);
		dpy->application = (struct ivi_application *)wthp_registry_bind(registry, name, interface, 1);
	}
}

/* The server removed a global.
 * We should destroy everything we created through that global,
 * and destroy the objects we created by binding to it.
 * The identification happens by global's name, so we need to keep
 * track what names we bound.
 * (not implemented)
 */
static void
registry_handle_global_remove(struct wthp_registry *wthp_registry,
			      uint32_t name)
{
	printf("global %d removed\n", name);
	wthp_registry_free(wthp_registry);
}

static const struct wthp_registry_listener registry_listener = {
	registry_handle_global,
	registry_handle_global_remove
};

static int
watch_ctl(struct watch *w, int op, uint32_t events)
{
	struct epoll_event ee;

	ee.events = events;
	ee.data.ptr = w;
	return epoll_ctl(w->display->epoll_fd, op, w->fd, &ee);
}

static void
connection_handle_data(struct watch *w, uint32_t events)
{
	struct waltham_display *dpy = container_of(w, struct waltham_display, conn_watch);
	int ret;

	if (events & EPOLLERR) {
		fprintf(stderr, "Connection errored out.\n");
		dpy->running = false;

		return;
	}

	if (events & EPOLLOUT) {
		/* Flush out again. If the flush completes, stop
		 * polling for writable as everything has been written.
		 */
		ret = wth_connection_flush(dpy->connection);
		if (ret == 0)
			watch_ctl(&dpy->conn_watch, EPOLL_CTL_MOD, EPOLLIN);
		else if (ret < 0 && errno != EAGAIN)
			dpy->running = false;
	}

	if (events & EPOLLIN) {
		/* Do not ignore EPROTO */
		ret = wth_connection_read(dpy->connection);
		if (ret < 0) {
			perror("Connection read error\n");
			dpy->running = false;

			return;
		}
	}

	if (events & EPOLLHUP) {
		fprintf(stderr, "Connection hung up.\n");
		dpy->running = false;

		return;
	}
}

static void
waltham_mainloop(void *data)
{
	struct waltham_display *dpy = (struct waltham_display *) data;
	struct epoll_event ee[MAX_EPOLL_WATCHES];
	struct watch *w;
	int count;
	int i;
	int ret;

	dpy->running = true;

	while (1) {
		/* Dispatch queued events. */
		ret = wth_connection_dispatch(dpy->connection);
		if (ret < 0)
			dpy->running = false;
                
		if (!dpy->running)
			break;

		/* Run any application idle tasks at this point. */
		/* (nothing to run so far) */

		/* Flush out buffered requests. If the Waltham socket is
		 * full, poll it for writable too, and continue flushing then.
		 */
		ret = wth_connection_flush(dpy->connection);
		if (ret < 0 && errno == EAGAIN) {
			watch_ctl(&dpy->conn_watch, EPOLL_CTL_MOD, EPOLLIN | EPOLLOUT);
		} else if (ret < 0) {
			perror("Connection flush failed");
			break;
		}

		/* Wait for events or signals */
		count = epoll_wait(dpy->epoll_fd,
		ee, ARRAY_LENGTH(ee), -1);
		if (count < 0 && errno != EINTR) {
			perror("Error with epoll_wait");
		break;
		}

		/* Waltham events only read in the callback, not dispatched,
		 * if the Waltham socket signalled readable. If it signalled
		 * writable, flush more. See connection_handle_data().
		 */
		for (i = 0; i < count; i++) {
			w = ee[i].data.ptr;
			w->cb(w, ee[i].events);
		}
	}
}

/* A one-off asynchronous open-coded roundtrip handler. */
static void
bling_done(struct wthp_callback *cb, uint32_t arg)
{
	fprintf(stderr, "...sync done.\n");

	wthp_callback_free(cb);
}

static const struct wthp_callback_listener bling_listener = {
	bling_done
};

/* XXX: these three handlers should not be here */

static void
not_here_error(struct wth_display *d, struct wth_object *obj,
	       uint32_t code, const char *msg)
{
	struct wth_connection *conn;

	conn = wth_object_get_user_data((struct wth_object *)d);
	fprintf(stderr, "fatal protocol error %d: %s\n", code, msg);
	wth_connection_set_protocol_error(conn, 0xf000dead /* XXX obj->id */,
					  "unknown", code);
}

static void
not_here_delete_id(struct wth_display *d, uint32_t id)
{
	fprintf(stderr, "wth_display.delete_id(%d)\n", id);
}

static void
not_here_server_version(struct wth_display *d, uint32_t ver)
{
	fprintf(stderr, "wth_display.server_version(%d)\n", ver);
}

static const struct wth_display_listener not_here_listener = {
	not_here_error,
	not_here_delete_id,
	not_here_server_version
};

static int
waltham_client_init(struct waltham_display *dpy)
{
	if (!dpy)
		return -1;
	/*
	 * get server_address from controller (adrress is set to weston.ini)
	 */
	if (dpy->server_addr)
		dpy->connection = wth_connect_to_server(dpy->server_addr, "34400");
	else
		dpy->connection = wth_connect_to_server("localhost", "34400");
	if(!dpy->connection)
		return -2;

	dpy->epoll_fd = epoll_create1(EPOLL_CLOEXEC);
	if (dpy->epoll_fd == -1) {
		perror("Error on epoll_create1");
		return -1;
	}

	dpy->conn_watch.display = dpy;
	dpy->conn_watch.cb = connection_handle_data;
	dpy->conn_watch.fd = wth_connection_get_fd(dpy->connection);
	if (watch_ctl(&dpy->conn_watch, EPOLL_CTL_ADD, EPOLLIN) < 0) {
		perror("Error setting up connection polling");
		return -1;
	}

	dpy->display = wth_connection_get_display(dpy->connection);
	/* wth_display_set_listener() is already done by waltham, as
	 * all the events are just control messaging.
	 */

	/* ..except Waltham does not yet, so let's plug in something
	 * to print out stuff at least.
	 */
	wth_display_set_listener(dpy->display, &not_here_listener, dpy->connection);
	/* Create a registry so that we will get advertisements of the
	 * interfaces implemented by the server.
	 */
	dpy->registry = wth_display_get_registry(dpy->display);
	wthp_registry_set_listener(dpy->registry, &registry_listener, dpy);

	/* Roundtrip ensures all globals' ads have been received. */
	if (wth_connection_roundtrip(dpy->connection) < 0) {
		fprintf(stderr, "Roundtrip failed.\n");
		return -1;
	}

	if (!dpy->compositor) {
		fprintf(stderr, "Did not find wthp_compositor, quitting.\n");
		return -1;
	}

	/* A one-off asynchronous roundtrip, just for fun. */
	fprintf(stderr, "sending wth_display.sync...\n");
	dpy->bling = wth_display_sync(dpy->display);
	wthp_callback_set_listener(dpy->bling, &bling_listener, dpy);

	pthread_t run_thread;
	pthread_create(&run_thread, NULL, waltham_mainloop, dpy);

	return 0;
}

static int
establish_timer_handler(void *data)
{
	struct weston_transmitter_remote *remote = data;
	int ret;

	ret = waltham_client_init(remote->transmitter->display);
	if(ret == -2) {
		wl_event_source_timer_update(remote->establish_timer, ESTABLISH_CONNECTION_PERIOD);
		return 0;
	}
	wl_event_source_timer_update(remote->retry_timer, RETRY_CONNECTION_PERIOD);
	remote->status = WESTON_TRANSMITTER_CONNECTION_READY;
	wl_signal_emit(&remote->connection_status_signal, remote);
	return 0;
}

static void
init_globals(struct waltham_display *dpy)
{
	dpy->compositor = NULL;
	dpy->blob_factory = NULL;
	dpy->seat = NULL;
	dpy->application = NULL;
}

static void
disconnect_surface(struct weston_transmitter_remote *remote)
{
	struct weston_transmitter_surface *txs;
	wl_list_for_each(txs, &remote->surface_list, link)
	{
		free(txs->ivi_surface);
		free(txs->wthp_surf);
	}
}

static int
retry_timer_handler(void *data)
{
	struct weston_transmitter_remote *remote = data;
	struct waltham_display *dpy = remote->transmitter->display;

	if(!dpy->running)
	{
		remote->status = WESTON_TRANSMITTER_CONNECTION_DISCONNECTED;
		registry_handle_global_remove(dpy->registry, 1);
		init_globals(dpy);
		disconnect_surface(remote);
		wl_event_source_timer_update(remote->establish_timer, ESTABLISH_CONNECTION_PERIOD);
		return 0;
	}
	else
		wl_event_source_timer_update(remote->retry_timer, RETRY_CONNECTION_PERIOD);
	return 0;
}

static struct weston_transmitter_remote *
transmitter_connect_to_remote(struct weston_transmitter *txr,
			      const char *addr,
			      struct wl_listener *status)
{
	struct weston_transmitter_remote *remote;
	struct wl_event_loop *loop_est, *loop_retry;
	int ret;

	remote = zalloc(sizeof (*remote));
	if (!remote)
		return NULL;

	remote->transmitter = txr;
	wl_list_insert(&txr->remote_list, &remote->link);
	remote->addr = strdup(addr);
	remote->status = WESTON_TRANSMITTER_CONNECTION_INITIALIZING;
	wl_signal_init(&remote->connection_status_signal);
	wl_signal_add(&remote->connection_status_signal, status);
	wl_list_init(&remote->output_list);
	wl_list_init(&remote->surface_list);
	wl_list_init(&remote->seat_list);
	wl_signal_init(&remote->conn_establish_signal);
	remote->establish_listener.notify = conn_ready_notify;
	wl_signal_add(&remote->conn_establish_signal, &remote->establish_listener);

	/* XXX: actually start connecting */
	weston_log("Transmitter connecting to %s...\n", addr);

	/* waltham */
	txr->display = zalloc(sizeof *txr->display);
	if (!txr->display)
		return NULL;
	txr->display->server_addr = remote->addr;
	/* set connection establish timer */
	loop_est = wl_display_get_event_loop(txr->compositor->wl_display);
	remote->establish_timer = wl_event_loop_add_timer(loop_est, establish_timer_handler, remote);
	wl_event_source_timer_update(remote->establish_timer, 1);
	/* set connection retry timer */
	loop_retry = wl_display_get_event_loop(txr->compositor->wl_display);
	remote->retry_timer = wl_event_loop_add_timer(loop_retry, retry_timer_handler, remote);

	if (ret < 0) {
		weston_log("Fatal: Transmitter waltham connecting failed.\n");
		return NULL;
	}

	wl_signal_emit(&remote->conn_establish_signal, NULL);
	return remote;
}

static enum weston_transmitter_connection_status
transmitter_remote_get_status(struct weston_transmitter_remote *remote)
{
	return remote->status;
}

static void
transmitter_remote_destroy(struct weston_transmitter_remote *remote)
{
	struct weston_transmitter_surface *txs;
	struct weston_transmitter_output *output, *otmp;
	struct weston_transmitter_seat *seat, *stmp;

	/* Do not emit connection_status_signal. */

	/*
	 *  Must not touch remote->transmitter as it may be stale:
	 * the desctruction order between the shell and Transmitter is
	 * undefined.
	 */
	weston_log("Transmitter disconnecting from %s.\n", remote->addr);

	if (remote->conn_timer)
		wl_event_source_remove(remote->conn_timer);

	if (!wl_list_empty(&remote->surface_list))
		weston_log("Transmitter warning: surfaces remain in %s.\n",
			   __func__);
	wl_list_for_each(txs, &remote->surface_list, link)
		txs->remote = NULL;
	wl_list_remove(&remote->surface_list);

	wl_list_for_each_safe(seat, stmp, &remote->seat_list, link)
		transmitter_seat_destroy(seat);

	wl_list_for_each_safe(output, otmp, &remote->output_list, link)
		transmitter_output_destroy(output);

	free(remote->addr);
	wl_list_remove(&remote->link);

	free(remote);
}

/** Transmitter is destroyed on compositor shutdown. */
static void
transmitter_compositor_destroyed(struct wl_listener *listener, void *data)
{
	struct weston_transmitter_remote *remote;
	struct weston_transmitter_surface *txs;
	struct weston_transmitter *txr =
		container_of(listener, struct weston_transmitter,
			     compositor_destroy_listener);

	assert(data == txr->compositor);

	/* may be called before or after shell cleans up */
	wl_list_for_each(remote, &txr->remote_list, link) {
		wl_list_for_each(txs, &remote->surface_list, link) {
			transmitter_surface_zombify(txs);
		}
	}

	/*
	 * Remove the head in case the list is not empty, to avoid
	 * transmitter_remote_destroy() accessing freed memory if the shell
	 * cleans up after Transmitter.
	 */
	wl_list_remove(&txr->remote_list);

	weston_log("Transmitter terminating.\n");
	free(txr);
}

static struct weston_transmitter *
transmitter_get(struct weston_compositor *compositor)
{
	struct wl_listener *listener;
	struct weston_transmitter *txr;

	listener = wl_signal_get(&compositor->destroy_signal,
				 transmitter_compositor_destroyed);
	if (!listener)
		return NULL;

	txr = container_of(listener, struct weston_transmitter,
			   compositor_destroy_listener);
	assert(compositor == txr->compositor);

	return txr;
}

static const struct weston_transmitter_api transmitter_api_impl = {
	transmitter_get,
	transmitter_connect_to_remote,
	transmitter_remote_get_status,
	transmitter_remote_destroy,
	transmitter_surface_push_to_remote,
	transmitter_surface_get_stream_status,
	transmitter_surface_destroy,
	transmitter_surface_configure,
	transmitter_surface_gather_state,
};

static void
transmitter_surface_set_ivi_id(struct weston_transmitter_surface *txs,
			       uint32_t ivi_id)
{
        struct weston_transmitter *txr = txs->remote->transmitter;
	struct waltham_display *dpy = txr->display;
	
	assert(txs->surface);
	if (!txs->surface)
		return;
	weston_log("ID %d\n", ivi_id);
	if(!txs)
		weston_log("no content in transmitter_surface\n");
	if(!txs->remote)
		weston_log("no content in transmitter_surface_remote\n");
	if(!dpy)
		weston_log("no content in waltham_display\n");
	if(!dpy->compositor)
		weston_log("no content in compositor object\n");
	if(!dpy->seat)
		weston_log("no content in seat object\n");

	if(!dpy->application)
		weston_log("no content in ivi-application object\n");
	txs->ivi_surface = ivi_application_surface_create(dpy->application,
							  ivi_id,  txs->wthp_surf);
	if(!txs->ivi_surface){
		weston_log("Failed to create txs->ivi_surf\n");
	}
         
	weston_log("%s(%p, %#x)\n", __func__, txs->surface, ivi_id);
}

static void
transmitter_surface_set_resize_callback(
	struct weston_transmitter_surface *txs,
	weston_transmitter_ivi_resize_handler_t cb,
	void *data)
{
	txs->resize_handler = cb;
	txs->resize_handler_data = data;
}

static const struct weston_transmitter_ivi_api transmitter_ivi_api_impl = {
	transmitter_surface_set_ivi_id,
	transmitter_surface_set_resize_callback,
};

WL_EXPORT int
wet_module_init(struct weston_compositor *compositor, int *argc, char *argv[])
{
	struct weston_transmitter *txr;
	int ret;

	txr = zalloc(sizeof *txr);
	if (!txr)
		return -1;

	wl_list_init(&txr->remote_list);

	txr->compositor = compositor;
	txr->compositor_destroy_listener.notify =
		transmitter_compositor_destroyed;
	wl_signal_add(&compositor->destroy_signal,
		      &txr->compositor_destroy_listener);

	ret = weston_plugin_api_register(compositor,
					 WESTON_TRANSMITTER_API_NAME,
					 &transmitter_api_impl,
					 sizeof(transmitter_api_impl));
	if (ret < 0) {
		weston_log("Fatal: Transmitter API registration failed.\n");
		goto fail;
	}

	ret = weston_plugin_api_register(compositor,
					 WESTON_TRANSMITTER_IVI_API_NAME,
					 &transmitter_ivi_api_impl,
					 sizeof(transmitter_ivi_api_impl));
	if (ret < 0) {
		weston_log("Fatal: Transmitter IVI API registration failed.\n");
		goto fail;
	}

	weston_log("Transmitter initialized.\n");

	return 0;

fail:
	wl_list_remove(&txr->compositor_destroy_listener.link);
	free(txr);

	return -1;
}
