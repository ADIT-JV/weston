/*
 * Copyright © 2017 Intel Corporation
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

#ifndef __VM_H__
#define __VM_H__

#include "vm-shared.h"
#include "compositor.h"

struct vm_buffer_table {
	struct vm_header h;
	struct wl_list vm_buffer_info_list;
};

struct gr_buffer_ref {
	struct vm_buffer_info vm_buffer_info;
	struct weston_buffer *buffer;
	struct gl_renderer *gr;
	struct wl_listener buffer_destroy_listener;
	struct wl_list elm;
	int cleanup_required;
	struct weston_surface* surface;
};

struct gl_renderer;
struct gl_output_state;

static void vm_init(struct gl_renderer *gr);
static int vm_table_draw(struct weston_output *output, struct gl_output_state *go,
		struct gl_renderer *gr);
static void pin_bo(struct gl_renderer *gr, void *buf, struct vm_buffer_info *vb);
static void unpin_bo(struct gl_renderer *gr, void *buf);
static void vm_destroy(struct gl_renderer *gr);
static void vm_table_clean(struct gl_renderer *gr);

 #endif
