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

#ifndef __VM_SHARED_H__
#define __VM_SHARED_H__

#include <stdint.h>

#define SURFACE_NAME_LENGTH     64
#define BIT(a)                  (1<<a)
/*
 * If a buffer has been updated by a VM client app, the status field will have
 * this UPDATED bit set. Otherwise, it would have this bit cleared
 */
#define UPDATED                 BIT(0)
/*
 * Some fields may not have been filled by the compositor and stay unused.
 * Those would be marked as UNUSED_FIELD. For example, currently the
 * tile_format is not populated by the compositor and will be set to this.
 */
#define UNUSED_FIELD            (BIT(16) - 1)

struct vm_header {
	int32_t version;
	int32_t counter;
	int32_t n_buffers;
};

struct vm_buffer_info {
	int32_t width, height;
	int32_t format;
	int32_t pitch[3];
	int32_t offset[3];
	int32_t bpp;
	int32_t tile_format;
	int32_t status;
	unsigned long ggtt_offset;
	char surface_name[SURFACE_NAME_LENGTH];
};


#endif
