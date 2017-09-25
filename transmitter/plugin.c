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
#include <pthread.h>

#include "compositor.h"
#include "helpers.h"
#include "timespec-util.h"

#include "compositor/weston.h"
#include "plugin.h"
#include "transmitter_api.h"
#include "plugin-registry.h"
#include "ivi-shell/ivi-layout-export.h"

/* waltham */
#include <errno.h>
#include <sys/epoll.h>
#include <sys/timerfd.h>
#include <waltham-object.h>
#include <waltham-client.h>
#include <waltham-connection.h>

#define MAX_EPOLL_WATCHES 2
#define ESTABLISH_CONNECTION_PERIOD 2000
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
	if (b)
		wthp_buffer_destroy(b);
}

static const struct wthp_buffer_listener buffer_listener = {
	buffer_send_complete
};

static void
transmitter_surface_gather_state(struct weston_transmitter_surface *txs)
{
	struct weston_transmitter_remote *remote = txs->remote;
	struct waltham_display *dpy = remote->display;
	if(!dpy->running) {
		if(remote->status != WESTON_TRANSMITTER_CONNECTION_DISCONNECTED) {
			remote->status = WESTON_TRANSMITTER_CONNECTION_DISCONNECTED;
			wl_event_source_timer_update(remote->retry_timer, 1);
		}
	}
	else {
	/* TODO: transmit surface state to remote */
	/* The buffer must be transmitted to remote side */
	
	/* waltham */
	struct weston_surface *surf = txs->surface;
	struct weston_compositor *comp = surf->compositor;
	int32_t stride, data_sz, width, height;
	void *data;
	
	width = 1;
	height = 1;
	stride = width * (PIXMAN_FORMAT_BPP(comp->read_format) / 8);

	data = malloc(stride * height);
	data_sz = stride * height;

	/* fake sending buffer */
	txs->wthp_buf = wthp_blob_factory_create_buffer(remote->display->blob_factory,
							data_sz,
							data,
							surf->width,
							surf->height,
							stride,
							PIXMAN_FORMAT_BPP(comp->read_format));

	wthp_buffer_set_listener(txs->wthp_buf, &buffer_listener, txs);
	
	wthp_surface_attach(txs->wthp_surf, txs->wthp_buf, txs->attach_dx, txs->attach_dy);
	wthp_surface_damage(txs->wthp_surf, txs->attach_dx, txs->attach_dy, surf->width, surf->height);
	wthp_surface_commit(txs->wthp_surf);

	wth_connection_flush(remote->display->connection);

	txs->attach_dx = 0;
	txs->attach_dy = 0;
	}
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
	struct weston_transmitter_remote *remote;
	/* may be called multiple times */
	if (!txs->surface)
		return;

	wl_signal_emit(&txs->destroy_signal, txs);

	wl_list_remove(&txs->surface_destroy_listener.link);
	txs->surface = NULL;

	wl_list_remove(&txs->sync_output_destroy_listener.link);

	remote = txs->remote;
	if (!remote->display->compositor)
		weston_log("remote->compositor is NULL\n");
	if (txs->wthp_surf)
		wthp_surface_destroy(txs->wthp_surf);
	if (txs->wthp_ivi_surface)
		wthp_ivi_surface_destroy(txs->wthp_ivi_surface);

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
transmitter_surface_set_ivi_id(struct weston_transmitter_surface *txs)
{
        struct weston_transmitter_remote *remote = txs->remote;
	struct waltham_display *dpy = remote->display;
	struct weston_surface *ws;
	struct ivi_layout_surface **pp_surface = NULL;
	struct ivi_layout_surface *ivi_surf = NULL;
	int32_t surface_length = 0;
	int32_t ret = 0;
	int32_t i = 0;

	ret = txs->lyt->get_surfaces(&surface_length, &pp_surface);
	if(!ret)
		weston_log("No ivi_surface\n");

	ws = txs->surface;

	for(i = 0; i < surface_length; i++) {
		ivi_surf = pp_surface[i];
		if (ivi_surf->surface == ws) {
			assert(txs->surface);
			if (!txs->surface)
				return;
			if(!dpy)
				weston_log("no content in waltham_display\n");
			if(!dpy->compositor)
				weston_log("no content in compositor object\n");
			if(!dpy->seat)
				weston_log("no content in seat object\n");
			if(!dpy->application)
				weston_log("no content in ivi-application object\n");
			
			txs->wthp_ivi_surface = wthp_ivi_application_surface_create
				(dpy->application, ivi_surf->id_surface,  txs->wthp_surf);
			weston_log("surface ID %d\n", ivi_surf->id_surface);
			if(!txs->wthp_ivi_surface){
				weston_log("Failed to create txs->ivi_surf\n");
			}
		}
	}
	free(pp_surface);
	pp_surface = NULL;
}

static struct weston_transmitter_surface *
transmitter_surface_push_to_remote(struct weston_surface *ws,
				   struct weston_transmitter_remote *remote,
				   struct wl_listener *stream_status)
{
	struct weston_transmitter *txr = remote->transmitter;
	struct weston_transmitter_surface *txs;
	bool found = false;

	if (remote->status != WESTON_TRANSMITTER_CONNECTION_READY)
	{
		return NULL;
	}

	wl_list_for_each(txs, &remote->surface_list, link) {
		if (txs->surface == ws) {
			found = true;
			break;
		}
	}

	if (!found) {
		txs = NULL;
		txs = zalloc(sizeof (*txs));
		if (!txs)
			return NULL;

		txs->remote = remote;
		wl_signal_init(&txs->destroy_signal);
		wl_list_insert(&remote->surface_list, &txs->link);

		txs->status = WESTON_TRANSMITTER_STREAM_INITIALIZING;
		wl_signal_init(&txs->stream_status_signal);
		if (stream_status)
			wl_signal_add(&txs->stream_status_signal, stream_status);

		txs->surface = ws;
		txs->surface_destroy_listener.notify = transmitter_surface_destroyed;
		wl_signal_add(&ws->destroy_signal, &txs->surface_destroy_listener);

		wl_list_init(&txs->sync_output_destroy_listener.link);

		wl_list_init(&txs->frame_callback_list);
		wl_list_init(&txs->feedback_list);

		txs->lyt = weston_plugin_api_get(txr->compositor, 
						 IVI_LAYOUT_API_NAME, sizeof(txs->lyt));
	}

	/* TODO: create the content stream connection... */
	if (!remote->display->compositor)
		weston_log("remote->compositor is NULL\n");
	if (!txs->wthp_surf) {
		weston_log("txs->wthp_surf is NULL\n");
		txs->wthp_surf = wthp_compositor_create_surface(remote->display->compositor);
		transmitter_surface_set_ivi_id(txs);
	}

	return txs;
}

static enum weston_transmitter_stream_status
transmitter_surface_get_stream_status(struct weston_transmitter_surface *txs)
{
	return txs->status;
}

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
		strdup(remote->model),
		{
			WL_OUTPUT_MODE_CURRENT | WL_OUTPUT_MODE_PREFERRED,
			800, 600,
			51519,
			{ NULL, NULL }
		}
	};
	if(remote->width != 0) {
		if(remote->height != 0) {
			info.mode.width = remote->width;
			info.mode.height = remote->height;
		}
	}
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
		wthp_seat_set_listener(dpy->seat, &seat_listener, dpy);
	} else if (strcmp(interface, "wthp_ivi_application") == 0) {
	        assert(!dpy->application);
		dpy->application = (struct wthp_ivi_application *)wthp_registry_bind(registry, name, interface, 1);
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
	if (wthp_registry)
		wthp_registry_free(wthp_registry);
}

static const struct wthp_registry_listener registry_listener = {
	registry_handle_global,
	registry_handle_global_remove
};

static int
watch_ctl(struct watch *w, int op, uint32_t events)
{
	struct weston_transmitter *txr = w->display->remote->transmitter;
	struct epoll_event ee;

	ee.events = events;
	ee.data.ptr = w;
	return epoll_ctl(txr->epoll_fd, op, w->fd, &ee);
}

static void
connection_handle_data(struct watch *w, uint32_t events)
{
	struct waltham_display *dpy = container_of(w, struct waltham_display, conn_watch);
	struct weston_transmitter_remote *remote = dpy->remote;
	int ret;


	if (!dpy->running) {
		weston_log("This server is not running yet. %s:%s\n", remote->addr, remote->port);
		return;
	}

	if (events & EPOLLERR) {
		weston_log("Connection errored out.\n");
		dpy->running = false;
		if (watch_ctl(&dpy->conn_watch, EPOLL_CTL_DEL, EPOLLIN | EPOLLOUT) < 0) {
			return;
		}

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
			weston_log("Connection read error %s:%s\n", remote->addr, remote->port);
			perror("Connection read error\n");
			dpy->running = false;
			perror("EPOLL_CTL_DEL\n");
			if (watch_ctl(&dpy->conn_watch, EPOLL_CTL_DEL, EPOLLIN | EPOLLOUT) < 0) {
				return;
			}
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
	struct weston_transmitter *txr = data;
	struct weston_transmitter_remote *remote;
	struct epoll_event ee[MAX_EPOLL_WATCHES];
	struct watch *w;
	int count;
	int i;
	int ret;
	int running_display;

	while (1) {
		running_display = 0;
		pthread_mutex_lock(&txr->txr_mutex);
		wl_list_for_each(remote, &txr->remote_list, link) {
			struct waltham_display *dpy = remote->display;
			if (!dpy)
				continue;
			if (!dpy->connection)
				continue;
			if (!dpy->running)
				continue;
			running_display++;
			/* Dispatch queued events. */
			pthread_mutex_lock(&dpy->mutex);
			ret = wth_connection_dispatch(dpy->connection);
			if (ret < 0)
				dpy->running = false;
			if (!dpy->running)
				continue;
			pthread_mutex_unlock(&dpy->mutex);
			/* Run any application idle tasks at this point. */
			/* (nothing to run so far) */

			/* Flush out buffered requests. If the Waltham socket is
			 * full, poll it for writable too, and continue flushing then.
			 */
			pthread_mutex_lock(&dpy->mutex);
			ret = wth_connection_flush(dpy->connection);
			if (ret < 0 && errno == EAGAIN) {
				watch_ctl(&dpy->conn_watch, EPOLL_CTL_MOD, EPOLLIN | EPOLLOUT);
			} else if (ret < 0) {
				perror("Connection flush failed");
				pthread_mutex_unlock(&dpy->mutex);
				break;
			}
			pthread_mutex_unlock(&dpy->mutex);
		}
		pthread_mutex_unlock(&txr->txr_mutex);

		if (0 < running_display) {
			/* Wait for events or signals */
			count = epoll_wait(txr->epoll_fd,
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
				pthread_mutex_lock(&w->display->mutex);
				w->cb(w, ee[i].events);
				pthread_mutex_unlock(&w->display->mutex);
			}
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

static int
waltham_client_init(struct waltham_display *dpy)
{
	pthread_mutex_init(&dpy->mutex, NULL);

	if (!dpy)
		return -1;
	/*
	 * get server_address from controller (adrress is set to weston.ini)
	 */
	dpy->connection = wth_connect_to_server(dpy->remote->addr, dpy->remote->port);

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

	dpy->running = true;

	return 0;
}

static int
establish_timer_handler(void *data)
{
	struct weston_transmitter_remote *remote = data;
	int ret;

	ret = waltham_client_init(remote->display);
	if(ret == -2) {
		wl_event_source_timer_update(remote->establish_timer, 
					     ESTABLISH_CONNECTION_PERIOD);
		return 0;
	}
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
	dpy->pointer = NULL;
	dpy->keyboard = NULL;
	dpy->touch = NULL;
}

static void
disconnect_surface(struct weston_transmitter_remote *remote)
{
	struct weston_transmitter_surface *txs;
	wl_list_for_each(txs, &remote->surface_list, link)
	{
		free(txs->wthp_ivi_surface);
		free(txs->wthp_surf);
		txs->wthp_ivi_surface = NULL;
		txs->wthp_surf = NULL;
	}
}

static int
retry_timer_handler(void *data)
{
	struct weston_transmitter_remote *remote = data;
	struct waltham_display *dpy = remote->display;

	if(!dpy->running)
	{
		pthread_mutex_lock(&dpy->mutex);
		remote->status = WESTON_TRANSMITTER_CONNECTION_DISCONNECTED;
		registry_handle_global_remove(dpy->registry, 1);
		init_globals(dpy);
		disconnect_surface(remote);
		wl_event_source_timer_update(remote->establish_timer, 
					     ESTABLISH_CONNECTION_PERIOD);
		pthread_mutex_unlock(&dpy->mutex);
		return 0;
	}
	else
		wl_event_source_timer_update(remote->retry_timer, 
					     RETRY_CONNECTION_PERIOD);
	return 0;
}

static struct weston_transmitter_remote *
transmitter_connect_to_remote(struct weston_transmitter *txr)
{
	struct weston_transmitter_remote *remote;
	struct wl_event_loop *loop_est, *loop_retry;
	int ret;

	wl_list_for_each_reverse(remote, &txr->remote_list, link) {
		/* XXX: actually start connecting */
		/* waltham */
		remote->display = zalloc(sizeof *remote->display);
		if (!remote->display)
			return NULL;
		remote->display->remote = remote;
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
	}

	pthread_t run_thread;
	pthread_create(&run_thread, NULL, waltham_mainloop, txr);
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
	pthread_mutex_lock(&txr->txr_mutex);
	wl_list_remove(&txr->remote_list);
	pthread_mutex_unlock(&txr->txr_mutex);

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

static void
transmitter_register_connection_status(struct weston_transmitter *txr,
				       struct wl_listener *connected_listener)
{
	wl_signal_add(&txr->connected_signal, connected_listener);
}

static struct weston_surface *
transmitter_get_weston_surface(struct weston_transmitter_surface *txs)
{
	return txs->surface;
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
	transmitter_register_connection_status,
	transmitter_get_weston_surface,
};

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
	transmitter_surface_set_resize_callback,
};

static int
transmitter_create_remote(struct weston_transmitter *txr,
			  const char *model,
			  const char *addr,
			  const char *port,
	                  const char *width,
	                  const char *height)
{
	struct weston_transmitter_remote *remote;

	remote = zalloc(sizeof (*remote));
	if (!remote)
		return -1;

	remote->transmitter = txr;
	wl_list_insert(&txr->remote_list, &remote->link);
	remote->model = strdup(model);
	remote->addr = strdup(addr);
	remote->port = strdup(port);
	remote->width = atoi(width);
	remote->height = atoi(height);
	remote->status = WESTON_TRANSMITTER_CONNECTION_INITIALIZING;
	wl_signal_init(&remote->connection_status_signal);
	wl_list_init(&remote->output_list);
	wl_list_init(&remote->surface_list);
	wl_list_init(&remote->seat_list);
	wl_signal_init(&remote->conn_establish_signal);
	remote->establish_listener.notify = conn_ready_notify;
	wl_signal_add(&remote->conn_establish_signal, &remote->establish_listener);

	return 0;
}

static void
transmitter_get_server_config(struct weston_transmitter *txr)
{
	struct weston_config *config = wet_get_config(txr->compositor);
	struct weston_config_section *section;
	const char *name = NULL;
	char *model = NULL;
	char *addr = NULL;
	char *port = NULL;
	char *width = '0';
	char *height = '0';
	int ret;

	section = weston_config_get_section(config, "remote", NULL, NULL);

	while (weston_config_next_section(config, &section, &name)) {
		if (0 == strcmp(name, "remote-output")) {
			if (0 != weston_config_section_get_string(section, "output-name",
								  &model, 0))
				continue;

			if (0 != weston_config_section_get_string(section, "server-address",
								  &addr, 0))
				continue;

			if (0 != weston_config_section_get_string(section, "port",
								  &port, 0))
				continue;

			if (0 != weston_config_section_get_string(section, "width",
								  &width, 0))
				continue;

			if (0 != weston_config_section_get_string(section, "height",
								  &height, 0))
				continue;
			ret = transmitter_create_remote(txr, model, addr,
							port, width, height);
			if (ret < 0) {
				weston_log("Fatal: Transmitter create_remote failed.\n");
			}
		}
	}
}

static void
transmitter_post_init(void *data)
{
	struct weston_transmitter *txr = data;

	if (!txr) {
		weston_log("Transmitter disabled\n");
	} else {
		txr->epoll_fd = epoll_create1(EPOLL_CLOEXEC);
		if (txr->epoll_fd == -1)
			perror("Error on epoll_create1");

		transmitter_get_server_config(txr);
		transmitter_connect_to_remote(txr);
	}
}

WL_EXPORT int
wet_module_init(struct weston_compositor *compositor, int *argc, char *argv[])
{
	struct weston_transmitter *txr;
	int ret;
	struct wl_event_loop *loop = NULL;

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

	loop = wl_display_get_event_loop(compositor->wl_display);
	wl_event_loop_add_idle(loop, transmitter_post_init, txr);

	pthread_mutex_init(&txr->txr_mutex, NULL);

	return 0;

fail:
	wl_list_remove(&txr->compositor_destroy_listener.link);
	free(txr);

	return -1;
}
