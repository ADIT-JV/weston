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

#include "compositor.h"
#include "helpers.h"

#include "plugin.h"
#include "transmitter_api.h"

/** @file
 *
 * This is an implementation of a remote output.
 *
 * A remote output must not be accepted as an argument to:
 * - wl_shell_surface.set_fullscreen
 * - wl_shell_surface.set_maximized
 * - zwp_fullscreen_shell_v1.present_surface
 * - zwp_fullscreen_shell_v1.present_surface_for_mode
 * - zwp_input_panel_surface_v1.set_toplevel
 * - xdg_surface.set_fullscreen
 *
 * If a remote output is an argument to the above or similar requests,
 * it should have the same effect as NULL if possible.
 *
 * @todo Should we instead accept the argument and have it start remoting
 * automatically? That would be shell-specific.
 *
 * In ivi-shell's case, only zwp_input_panel_surface_v1.set_toplevel is
 * reachable from keyboard.c. That just blindly uses whatever the first
 * output happens to be, so there is no need to check for now.
 *
 * @todo Add weston_output_set_remote() which sets weston_output::is_remote
 * to true and inits weston_output::link. This should be made mutually
 * exclusive with weston_compositor_add_output().
 */

static inline struct weston_transmitter_output *
to_transmitter_output(struct weston_output *base)
{
	return container_of(base, struct weston_transmitter_output, base);
}

static void
frame_done(void *data, struct wl_callback *callback, uint32_t time)
{
	struct weston_transmitter_output *output = data;
	struct timespec ts;

	assert(callback == output->frame_cb);
	wl_callback_destroy(callback);
	output->frame_cb = NULL;

	/* XXX: use the presentation extension for proper timings */

	/*
	 * This is the fallback case, where Presentation extension is not
	 * available from the parent compositor. We do not know the base for
	 * 'time', so we cannot feed it to finish_frame(). Do the only thing
	 * we can, and pretend finish_frame time is when we process this
	 * event.
	 */
	weston_compositor_read_presentation_clock(output->base.compositor, &ts);
	weston_output_finish_frame(&output->base, &ts, 0);
}

static char *
make_model(struct weston_transmitter_remote *remote, int name)
{
	char *str;

	if (asprintf(&str, "transmitter-%s:%s-%d", remote->addr, remote->port, name) < 0)
		return NULL;

	return str;
}

static int
make_mode_list(struct wl_list *list,
	       const struct weston_transmitter_output_info *info)
{
	struct weston_mode *mode;

	mode = zalloc(sizeof *mode);
	if (!mode)
		return -1;

	*mode = info->mode;
	wl_list_insert(list->prev, &mode->link);

	return 0;
}

static struct weston_mode *
get_current_mode(struct wl_list *mode_list)
{
	struct weston_mode *mode;

	wl_list_for_each(mode, mode_list, link)
		if (mode->flags & WL_OUTPUT_MODE_CURRENT)
			return mode;

	assert(0);
	return NULL;
}

static void
free_mode_list(struct wl_list *mode_list)
{
	struct weston_mode *mode;

	while (!wl_list_empty(mode_list)) {
		mode = container_of(mode_list->next, struct weston_mode, link);

		wl_list_remove(&mode->link);
		free(mode);
	}
}

void
transmitter_output_destroy(struct weston_transmitter_output *output)
{
	weston_log("Transmitter destroying output '%s'\n", output->base.name);

	wl_list_remove(&output->link);

	free_mode_list(&output->base.mode_list);
	free(output->base.serial_number);
	free(output->base.model);
	free(output->base.make);

	weston_output_destroy(&output->base);
	free(output);
}

static void
transmitter_output_destroy_(struct weston_output *base)
{
	struct weston_transmitter_output *output = to_transmitter_output(base);

	transmitter_output_destroy(output);
}


static void
transmitter_start_repaint_loop(struct weston_output *base)
{
	weston_log("%s(%s)\n", __func__, base->name);
	struct timespec ts;
	struct weston_transmitter_output *output = to_transmitter_output(base);
	
	weston_compositor_read_presentation_clock(output->base.compositor, &ts);
	weston_output_finish_frame(&output->base, &ts, 0);
}

static void *
transmitter_reset_commit_signal(struct weston_transmitter_output *output)
{
	struct weston_transmitter_remote *remote = output->remote;
	struct weston_transmitter_surface *txs;
	struct weston_frame_callback *cb, *cnext;
	uint32_t frame_time;

	wl_list_for_each(txs, &remote->surface_list, link) {
		if (txs->commit_listener.notify) {
			frame_time = weston_compositor_get_time();
			wl_list_for_each_safe(cb, cnext, &txs->frame_callback_list, link) {
				wl_callback_send_done(cb->resource, frame_time);
				wl_resource_destroy(cb->resource);
			}
			wl_list_remove(&txs->commit_listener.link);
			txs->commit_listener.notify = NULL;
		}
	}
}

static int
transmitter_output_repaint(struct weston_output *base,
			   pixman_region32_t *damage)
{
	weston_log("%s(%s)\n", __func__, base->name);
	struct weston_transmitter_output* output = to_transmitter_output(base);
	struct weston_transmitter_remote* remote = output->remote;
	struct weston_transmitter* txr = remote->transmitter;
	struct weston_transmitter_api* transmitter_api = 
		weston_get_transmitter_api(txr->compositor);
	struct weston_transmitter_surface* txs;
	struct weston_compositor *compositor = base->compositor;
	struct weston_view *view;
	bool found_outut = false;

	/* 
	 * Pick up weston_view in transmitter_output and check weston_view's surface
	 * If the surface hasn't been conbined to weston_transmitter_surface, 
	 * then call push_to_remote.
	 * If the surface has already been combined, call gather_state.
	 */
	if (wl_list_empty(&compositor->view_list))
		goto out;

	wl_list_for_each_reverse(view, &compositor->view_list, link) {
		bool found_surface = false;
		weston_log("wl_list_for_each weston_view... %s:%s\n", remote->addr, remote->port);
		if (view->output == &output->base) {
			weston_log("This view is contained on this output\n");
			found_outut = true;
			wl_list_for_each(txs, &remote->surface_list, link) {
				if (txs->surface == view->surface) {
					weston_log("test log::surface on transmitter output\n");
					if (!txs->wthp_surf)
						transmitter_api->surface_push_to_remote(view->surface,
											remote, NULL);
					transmitter_api->surface_gather_state(txs);
					found_surface = true;
					break;
				}
			}
			if (!found_surface) {
				weston_log("test log::add new remote surface \n");
				transmitter_api->surface_push_to_remote(view->surface, remote, NULL);
			}
		}
	}
	if (!found_outut)
		goto out;

	transmitter_start_repaint_loop(base);

	return 0;

out:
	transmitter_reset_commit_signal(output);
	transmitter_start_repaint_loop(base);

	return 0;

}
static void
transmitter_output_enable(struct weston_output *base)
{
	weston_log("%s(%s)\n", __func__, base->name);
	struct weston_transmitter_output *output = to_transmitter_output(base);
	int ret = 0;
	
	output->base.assign_planes = NULL;
	output->base.set_backlight = NULL;
	output->base.set_dpms = NULL;
	output->base.switch_mode = NULL;
	
	return 0;
}

int
transmitter_remote_create_output(struct weston_transmitter_remote *remote,
				 const struct weston_transmitter_output_info *info)
{
	struct weston_transmitter_output *output;
	struct weston_transmitter *txr = remote->transmitter;

	output = zalloc(sizeof *output);
	if (!output)
		return -1;

	output->parent.draw_initial_frame = true;

	output->base.subpixel = info->subpixel;

	output->base.name = make_model(remote, 1);
	output->base.make = strdup(WESTON_TRANSMITTER_OUTPUT_MAKE);
	output->base.model = make_model(remote, 1);
	output->base.serial_number = strdup("0");
	/* x and y is fake value */
	wl_list_init(&output->base.mode_list);
	if (make_mode_list(&output->base.mode_list, info) < 0)
		goto fail;

	output->base.current_mode = get_current_mode(&output->base.mode_list);
	output->base.height = output->base.current_mode->height;
	output->base.width = output->base.current_mode->width;
	/* WL_OUTPUT_MODE_CURRENT already set */
	weston_output_init(&output->base, remote->transmitter->compositor);

	/*
	 * renderer_output_create skipped:
	 * no renderer awareness is needed for this output
	 */

	/*
	 * weston_compositor_add_output() skipped:
	 * Most other code uses weston_compositor::output_list when traversing
	 * all outputs, we do not want any of that.
	 * Also weston_compositor::output_created_signal must not trigger
	 * for this output, since we must not involve input device management
	 * or color management or any kind of local management.
	 */
	output->base.enable = transmitter_output_enable;
	output->base.start_repaint_loop = transmitter_start_repaint_loop;
	output->base.repaint = transmitter_output_repaint;
	output->base.destroy = transmitter_output_destroy_;
	output->base.assign_planes = NULL;
	output->base.set_dpms = NULL;
	output->base.switch_mode = NULL;
	output->base.gamma_size = 0;
	output->base.set_gamma = NULL;

	output->base.native_mode = output->base.current_mode;
	output->base.native_scale = output->base.current_scale;
	output->base.scale = 1;
	output->base.transform = WL_OUTPUT_TRANSFORM_NORMAL;

	output->remote = remote;
	wl_list_insert(&remote->output_list, &output->link);
	weston_log("Not calling weston_compositor_add_pending_output\n");

	weston_output_enable(&output->base);
	weston_log("Transmitter created output '%s': %s, %s, %s\n",
		   output->base.name, output->base.make, output->base.model,
		   output->base.serial_number);

	return 0;

fail:
	free_mode_list(&output->base.mode_list);
	free(output->base.serial_number);
	free(output->base.model);
	free(output->base.make);
	free(output->base.name);
	free(output);

	return -1;
}
