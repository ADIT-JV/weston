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

#ifndef WESTON_TRANSMITTER_API_H
#define WESTON_TRANSMITTER_API_H

#include "plugin-registry.h"

#include <stdint.h>

/** \file
 *
 * This is the Transmitter API published via weston_plugin_api_register().
 */

struct weston_transmitter;
struct weston_transmitter_remote;
struct weston_transmitter_surface;

#define WESTON_TRANSMITTER_API_NAME "transmitter_v1"

/** See weston_transmitter_api::remote_get_status */
enum weston_transmitter_connection_status {
	/** The connection hand-shake is not yet complete */
	WESTON_TRANSMITTER_CONNECTION_INITIALIZING,

	/** The connection is live and ready to be used. */
	WESTON_TRANSMITTER_CONNECTION_READY,

	/** The connection is dead. */
	WESTON_TRANSMITTER_CONNECTION_DISCONNECTED,
};

/** See weston_transmitter_api::surface_get_stream_status */
enum weston_transmitter_stream_status {
	/** The stream hand-shake is not yet complete. */
	WESTON_TRANSMITTER_STREAM_INITIALIZING,

	/** The stream is carrying surface content updates as needed. */
	WESTON_TRANSMITTER_STREAM_LIVE,

	/** The stream has failed and disconnected permanently. */
	WESTON_TRANSMITTER_STREAM_FAILED,
};

/** The Transmitter Base API
 *
 * Transmitter is a Weston plugin that provides remoting of weston_surfaces
 * over the network. Shells use this API to create remote connections and
 * push surfaces over the network. Shells are also responsible for relaying
 * basic window state changes to Transmitter.
 *
 * In addition to the Transmitter Base API, shells also need to use a
 * shell protocol specific Transmitter API to relay specific window state
 * changes.
 */
struct weston_transmitter_api {
	/** Fetch the Transmitter plugin context
	 *
	 * \param compositor The compositor instance.
	 * \return The weston_transmitter context, which is always the same
	 * for the given compositor instance.
	 */
	struct weston_transmitter *
	(*transmitter_get)(struct weston_compositor *compositor);

	/**
	 * Connect to a remote server via Transmitter.
	 *
	 * \param txr The Transmitter context.
	 * \param addr Address of the remote server.
	 * \param status Listener to inform of connection status changes.
	 * \return A handle to the remote connection, or NULL on failure.
	 *
	 * This call attempts to open a connection asynchronously. The
	 * connection is not usable until the listener signals it is ready.
	 * The listener may also signal that the connection failed instead.
	 *
	 * The listener callback argument is the weston_transmitter_remote
	 * returned by this function. Use remote_get_status() to fetch the
	 * current status.
	 *
	 * The address argument is a string in the form "host:port".
	 */
	struct weston_transmitter_remote *
	(*connect_to_remote)(struct weston_transmitter *txr,
			     const char *addr,
			     struct wl_listener *status);

	/**
	 * Retrieve the connection status.
	 *
	 * If the status is WESTON_TRANSMITTER_CONNECTION_DISCONNECTED,
	 * you have to shut the remote down completely. There is no automatic
	 * reconnect.
	 */
	enum weston_transmitter_connection_status
	(*remote_get_status)(struct weston_transmitter_remote *remote);

	/**
	 * Destroy/disconnect a remote connection.
	 *
	 * Disconnects if connected, and destroys the connection.
	 * The connection status handler is not called.
	 *
	 * The caller is responsible for destroying all
	 * weston_transmitter_surfaces before calling this.
	 */
	void
	(*remote_destroy)(struct weston_transmitter_remote *remote);

	/** Push a weston_surface to be transmitted to a remote.
	 *
	 * \param ws The surface to push.
	 * \param remote The remote connection to use.
	 * \param stream_status Listener for stream status changes.
	 * \return The Transmitter surface handle.
	 *
	 * The surface cannot be visible on the remote until the stream
	 * status listener signals WESTON_TRANSMITTER_STREAM_LIVE. After that,
	 * surface updates made by the application will be automatically
	 * streamed to the remote, and input events from the remote will be
	 * delivered to the application.
	 *
	 * The listener callback argument is the weston_transmitter_surface
	 * returned by this function. Use surface_get_stream_status() to
	 * fetch the current status.
	 */
	struct weston_transmitter_surface *
	(*surface_push_to_remote)(struct weston_surface *ws,
				  struct weston_transmitter_remote *remote,
				  struct wl_listener *stream_status);

	/**
	 * Retrieve the surface content stream status.
	 *
	 * If the status is WESTON_TRANSMITTER_STREAM_FAILED, remoting the
	 * surface has stopped. There is no automatic retry.
	 */
	enum weston_transmitter_stream_status
	(*surface_get_stream_status)(struct weston_transmitter_surface *txs);

	/** Stop remoting a weston_surface
	 *
	 * \param txs Transmitter surface handle to be stopped and freed.
	 *
	 * The surface stream status handler is not called.
	 */
	void
	(*surface_destroy)(struct weston_transmitter_surface *txs);

	/** Notify of weston_surface being configured
	 *
	 * \param txs The Transmitter surface handle.
	 * \param dx The x delta given in wl_surface.attach request.
	 * \param dy The y delta given in wl_surface.attach request.
	 *
	 * Notifies Transmitter of new surface confguration. Transmitter will
	 * forward the arguments, window state, and reference the buffer for
	 * image transmission.
	 *
	 * Shells are meant to call this function for remoted surfaces in
	 * the weston_surface::configure handler.
	 *
	 * XXX: Is this necessary if we have weston_surface::apply_state_signal?
	 *
	 * Essentially this is just an elaborate way to forward dx,dy.
	 */
	void
	(*surface_configure)(struct weston_transmitter_surface *txs,
			     int32_t dx, int32_t dy);

	void
	(*surface_gather_state)(struct weston_transmitter_surface *txs);
};

static inline const struct weston_transmitter_api *
weston_get_transmitter_api(struct weston_compositor *compositor)
{
	return weston_plugin_api_get(compositor, WESTON_TRANSMITTER_API_NAME,
				     sizeof(struct weston_transmitter_api));
}

#define WESTON_TRANSMITTER_IVI_API_NAME "transmitter_ivi_v1"

/** For relaying configure events from Transmitter to shell. */
typedef void (*weston_transmitter_ivi_resize_handler_t)(void *data,
							int32_t width,
							int32_t height);

/** The Transmitter IVI-shell API
 *
 * Contains the IVI-shell specifics required to remote an ivi-surface.
 */
struct weston_transmitter_ivi_api {
	/** Set IVI-id for a transmitter surface
	 *
	 * \param txs The transmitted surface.
	 * \param ivi_id The IVI-surface id as specified by the
	 * ivi_application.surface_create request.
	 */
	void
	(*set_ivi_id)(struct weston_transmitter_surface *txs, uint32_t ivi_id);

	/** Set callback to relay configure events.
	 *
	 * \param txs The transmitted surface.
	 * \param cb The callback function pointer.
	 * \param data User data to be passed to the callback.
	 *
	 * The arguments to the callback function are user data, and width and
	 * height from the configure event from the remote compositor. The
	 * shell must relay this event to the application.
	 */
	void
	(*set_resize_callback)(struct weston_transmitter_surface *txs,
			       weston_transmitter_ivi_resize_handler_t cb,
			       void *data);
};

static inline const struct weston_transmitter_ivi_api *
weston_get_transmitter_ivi_api(struct weston_compositor *compositor)
{
	return weston_plugin_api_get(compositor,
				     WESTON_TRANSMITTER_IVI_API_NAME,
				     sizeof(struct weston_transmitter_ivi_api));
}

/** Identifies outputs created by the Transmitter by make */
#define WESTON_TRANSMITTER_OUTPUT_MAKE "Weston-Transmitter"

/* Remote compositor/output are identified by model */

#endif /* WESTON_TRANSMITTER_API_H */
