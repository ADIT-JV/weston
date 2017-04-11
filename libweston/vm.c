#include "vm.h"

static struct gl_shader std_shader;
static GLuint pos_att, tex_att;
static GLuint tex_uniform, bufpos_uniform;

static const char *vertex_shader =
	"uniform mat4 proj;\n"
	"uniform int bufpos;\n"
	"attribute vec2 position;\n"
	"attribute vec2 texcoord;\n"
	"varying vec2 v_texcoord;\n"
	"\n"
	"void main() {\n"
	"  vec2 p;\n"
	"  p = position + vec2(bufpos, 0.0);\n"
	"  gl_Position = proj * vec4(p, 0.0, 1.0);\n"
	"  v_texcoord = texcoord;\n"
	"}\n";

static const char *solid_fragment_shader =
	"precision mediump float;\n"
	"varying vec2 v_texcoord;\n"
	"uniform sampler2D tex;\n"
	"void main() {\n"
	"  gl_FragColor = texture2D(tex, v_texcoord)\n;"
	;

void vm_init(struct gl_renderer *gr)
{
	struct vm_buffer_table *vbt;
	gr->vm_buffer_table =
			(struct vm_buffer_table *) zalloc(sizeof (struct vm_buffer_table));
	vbt = gr->vm_buffer_table;
	vbt->h.version = 1;
	vbt->h.counter = 0;
	vbt->h.n_buffers = 0;

	wl_list_init(&vbt->vm_buffer_info_list);
}

static void unpin(struct gr_buffer_ref *gr_buf)
{
	if(!gr_buf) {
		return;
	}

	if ((gr_buf->buffer != NULL) && (gr_buf->buffer->priv_buffer != NULL)) {
		unpin_bo(gr_buf->gr, gr_buf->buffer->legacy_buffer);
	}

	wl_list_remove(&gr_buf->elm);

	if(gl_renderer_interface.vm_dbg) {
		weston_log("UnPinned ggtt_offset = 0x%lX\n",
			gr_buf->vm_buffer_info.ggtt_offset);
	}
}

static void vm_table_clean(struct gl_renderer *gr)
{
	struct gr_buffer_ref *gr_buf, *tmp;
	struct vm_buffer_table *vbt = gr->vm_buffer_table;

	/* remove any buffer refs inside this table */
	wl_list_for_each_safe(gr_buf, tmp, &vbt->vm_buffer_info_list, elm) {
		unpin(gr_buf);
		gr_buf->cleanup_required = 0;
	}
}

void buffer_destroy(struct gr_buffer_ref *gr_buf)
{
	if(gr_buf->cleanup_required) {
		unpin(gr_buf);
	}
	gr_buf->buffer->priv_buffer = NULL;
	free(gr_buf);
}

void vm_destroy(struct gl_renderer * gr)
{
	struct gr_buffer_ref *gr_buf, *tmp;
	struct vm_buffer_table *vbt = gr->vm_buffer_table;

	/* free any buffers inside this table */
	wl_list_for_each_safe(gr_buf, tmp, &vbt->vm_buffer_info_list, elm) {
		buffer_destroy(gr_buf);
	}

	free(gr->vm_buffer_table);
}

static void
gr_buffer_destroy_handler(struct wl_listener *listener,
				       void *data)
{
	struct gr_buffer_ref *gr_buf =
		container_of(listener, struct gr_buffer_ref, buffer_destroy_listener);

	buffer_destroy(gr_buf);
}

static void print_vm_buf_list(struct gl_renderer *gr)
{
	struct gr_buffer_ref *gr_buf;
	struct vm_buffer_table *vbt = gr->vm_buffer_table;

	weston_log("+++++++++++++++++++++++++\n");

	weston_log("version = %d\n", vbt->h.version);
	weston_log("counter = %d\n", vbt->h.counter);
	weston_log("n_buffers = %d\n\n", vbt->h.n_buffers);


	wl_list_for_each(gr_buf, &vbt->vm_buffer_info_list, elm) {
		weston_log("Surface id = %s\n", gr_buf->vm_buffer_info.surface_name);
		weston_log("Width = %d\n", gr_buf->vm_buffer_info.width);
		weston_log("Height = %d\n", gr_buf->vm_buffer_info.height);
		weston_log("Pitch[0] = %d offset[0] = %d\n",
				 gr_buf->vm_buffer_info.pitch[0],
				 gr_buf->vm_buffer_info.offset[0]);
		weston_log("Pitch[1] = %d offset[1] = %d\n",
				 gr_buf->vm_buffer_info.pitch[1],
				 gr_buf->vm_buffer_info.offset[1]);
		weston_log("Pitch[2] = %d offset[2] = %d\n",
				 gr_buf->vm_buffer_info.pitch[2],
				 gr_buf->vm_buffer_info.offset[2]);
		weston_log("Tiling= %d\n", gr_buf->vm_buffer_info.tile_format);
		weston_log("BPP = %d\n", gr_buf->vm_buffer_info.bpp);
		weston_log("Pixel format = 0x%X\n", gr_buf->vm_buffer_info.format);
		weston_log("ggtt_offset = 0x%lX\n", gr_buf->vm_buffer_info.ggtt_offset);
		weston_log("%s\n\n", gr_buf->vm_buffer_info.status & UPDATED ? "Updated" : "Not Updated");
	}
	weston_log("-------------------------\n");
}

static int get_bpp(struct ias_backend *bc, struct weston_buffer *buffer)
{
	struct gbm_bo *bo;
	uint32_t format;

	bo = gbm_bo_import(bc->gbm, GBM_BO_IMPORT_WL_BUFFER, buffer->resource, GBM_BO_USE_SCANOUT);
	if (!bo) {
		return 0;
	}

	format = gbm_bo_get_format(bo);
	gbm_bo_destroy(bo);
	switch(format)
	{
		case GBM_FORMAT_ARGB8888:
		case GBM_FORMAT_XRGB8888:
		case GBM_FORMAT_XRGB2101010:
			return 32;

		case GBM_FORMAT_RGB565:
			return 16;

		default:
			return 16;
	}
}

static void vm_add_buf(struct weston_compositor *ec, struct gl_renderer *gr,
		struct gl_surface_state *gs, struct ias_backend *bc, struct weston_buffer *buffer)
{
	struct vm_buffer_table *vbt = gr->vm_buffer_table;
	struct gr_buffer_ref *gr_buffer_ref_ptr;
	int32_t tiling_format[2];

	if(!buffer->priv_buffer) {
		gr_buffer_ref_ptr = zalloc(sizeof(struct gr_buffer_ref));
		gr_buffer_ref_ptr->buffer = buffer;
		gr_buffer_ref_ptr->gr = gr;
		gr_buffer_ref_ptr->buffer_destroy_listener.notify =
				gr_buffer_destroy_handler;
		buffer->priv_buffer = gr_buffer_ref_ptr;
		wl_signal_add(&buffer->destroy_signal,
				&gr_buffer_ref_ptr->buffer_destroy_listener);
	} else {
		gr_buffer_ref_ptr = buffer->priv_buffer;
	}

	gr_buffer_ref_ptr->cleanup_required = 1;
	wl_list_insert(&vbt->vm_buffer_info_list, &gr_buffer_ref_ptr->elm);
	pin_bo(gr, buffer->legacy_buffer,
			&(gr_buffer_ref_ptr->vm_buffer_info));
	gr_buffer_ref_ptr->vm_buffer_info.width = buffer->width;
	gr_buffer_ref_ptr->vm_buffer_info.height = buffer->height;
	gr->query_buffer(gr->egl_display, (void *) buffer->resource,
			  EGL_TEXTURE_FORMAT,
			  &(gr_buffer_ref_ptr->vm_buffer_info.format));
	gr->query_buffer(gr->egl_display, (void *) buffer->resource,
			  EGL_STRIDE,
			  gr_buffer_ref_ptr->vm_buffer_info.pitch);
	gr->query_buffer(gr->egl_display, (void *) buffer->resource,
			  EGL_OFFSET,
			  gr_buffer_ref_ptr->vm_buffer_info.offset);
	gr->query_buffer(gr->egl_display, (void *) buffer->resource,
			  EGL_TILING,
			  tiling_format);
	gr_buffer_ref_ptr->vm_buffer_info.bpp = get_bpp(bc, buffer);

	gr_buffer_ref_ptr->vm_buffer_info.status |= UPDATED;
	gr_buffer_ref_ptr->vm_buffer_info.tile_format = tiling_format[0];

	if(ec->renderer->fill_surf_name) {
		ec->renderer->fill_surf_name(gs->surface, SURFACE_NAME_LENGTH - 1,
				gr_buffer_ref_ptr->vm_buffer_info.surface_name);
	}

	if(gl_renderer_interface.vm_dbg) {
		print_vm_buf_list(gr);
	}
}

int vm_table_draw(struct weston_output *output, struct gl_output_state *go,
		struct gl_renderer *gr)
{
	struct weston_compositor *ec = output->compositor;
	struct ias_output *ias_output = (struct ias_output *) output;
	struct ias_backend *bc = ias_output->ias_crtc->backend;
	struct weston_view *view;
	struct gl_surface_state *gs;
	struct gr_buffer_ref *gr_buf;
	struct vm_buffer_table *vbt = gr->vm_buffer_table;
	int num_textures;
	GLuint *textures;
	int i = 0;
	EGLint buffer_age = 0;
	EGLBoolean ret;
	int full_width, full_height;
	struct weston_matrix matrix;
	struct gl_border_image *top, *bottom, *left, *right;
	static const GLfloat tile_verts[] = {
		0.0, 0.0,                           /**/  0.0, 0.0,
		sizeof(struct vm_header), 0.0, /**/  1.0, 0.0,
		0.0, 1.0,                           /**/  0.0, 1.0,
		sizeof(struct vm_header), 1.0, /**/  1.0, 1.0,
	};

	static const GLfloat tile_verts1[] = {
		0.0, 0.0,                           /**/  0.0, 0.0,
		sizeof(struct vm_buffer_info), 0.0, /**/  1.0, 0.0,
		0.0, 1.0,                           /**/  0.0, 1.0,
		sizeof(struct vm_buffer_info), 1.0, /**/  1.0, 1.0,
	};

	/*
	 * Clear the old table before filling it again.
	 */
	vm_table_clean(gr);

	/*
	 * eglQuerySurface has to be called in order for the back
	 * pointer in the DRI to not be NULL, or else eglSwapBuffers
	 * will segfault when there are no client buffers.
	 */
	if (gr->has_egl_buffer_age) {
		ret = eglQuerySurface(gr->egl_display, go->egl_surface,
				      EGL_BUFFER_AGE_EXT, &buffer_age);
		if (ret == EGL_FALSE) {
			weston_log("buffer age query failed.\n");
		}
	}

	wl_list_for_each_reverse(view, &ec->view_list, link) {
		struct ias_output *io = (struct ias_output *) view->output;
		if (io->vm && view->plane == &ec->primary_plane) {
			gs = get_surface_state(view->surface);
			if(gs->buffer_ref.buffer) {
				vm_add_buf(ec, gr, gs, bc, gs->buffer_ref.buffer);
			}
		}
	}

	num_textures = wl_list_length(&vbt->vm_buffer_info_list);

	std_shader.vertex_source = vertex_shader;
	std_shader.fragment_source = solid_fragment_shader;
	use_shader(gr, &std_shader);
	pos_att = glGetAttribLocation(std_shader.program, "position");
	tex_att = glGetAttribLocation(std_shader.program, "texcoord");
	std_shader.proj_uniform = glGetUniformLocation(std_shader.program, "proj");
	tex_uniform = glGetUniformLocation(std_shader.program, "tex");
	bufpos_uniform = glGetUniformLocation(std_shader.program, "bufpos");

	top = &go->borders[GL_RENDERER_BORDER_TOP];
	bottom = &go->borders[GL_RENDERER_BORDER_BOTTOM];
	left = &go->borders[GL_RENDERER_BORDER_LEFT];
	right = &go->borders[GL_RENDERER_BORDER_RIGHT];

	full_width = output->current_mode->width + left->width + right->width;
	full_height = output->current_mode->height + top->height + bottom->height;

	glClear(GL_COLOR_BUFFER_BIT);
	glDisable(GL_BLEND);

	glViewport(0, 0, full_width, full_height);

	weston_matrix_init(&matrix);
	weston_matrix_translate(&matrix, -full_width/2.0, -full_height/2.0, 0);
	weston_matrix_scale(&matrix, 2.0/full_width, -2.0/full_height, 1);
	glUniformMatrix4fv(std_shader.proj_uniform, 1, GL_FALSE, matrix.d);

	/* Buffer format is vert_x, vert_y, tex_x, tex_y */
	glVertexAttribPointer(pos_att, 2, GL_FLOAT, GL_FALSE,
			4 * sizeof(GLfloat), &tile_verts[0]);
	glVertexAttribPointer(tex_att, 2, GL_FLOAT, GL_FALSE,
			4 * sizeof(GLfloat), &tile_verts[2]);
	glEnableVertexAttribArray(pos_att);
	glEnableVertexAttribArray(tex_att);

	/*
	 * Allocate num_textures + 1. The extra 1 is for the header info that
	 * also needs to be flipped.
	 */
	textures = zalloc(sizeof(GLuint) * (num_textures + 1));

	/*
	 * Update the counter to be +1 each time before we flip and n_buffers to be
	 * the total number of buffers that we are providing to the host.
	 */
	vbt->h.counter++;
	vbt->h.n_buffers = num_textures;

        /* number of textures should be the number of buffers + 1 for the buffer table header */
        num_textures++;

	glGenTextures(num_textures, textures);

	glActiveTexture(GL_TEXTURE0);

	/* Write the header */
	glBindTexture(GL_TEXTURE_2D, textures[0]);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_BGRA_EXT, sizeof(struct vm_header), 1, 0,
			GL_BGRA_EXT, GL_UNSIGNED_BYTE, &vbt->h);
	glUniform1i(bufpos_uniform, 0);
	glUniform1i(tex_uniform, 0);
	glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

	/* No buffers */
	if(num_textures == 1) {
		glDeleteTextures(1, textures);
	        free(textures);
		return 1;
	}

	/*
	 * We start writing individual buffers from position 1 because 0 was used
	 * already to write the header
	 */

	glDisableVertexAttribArray(pos_att);
	glDisableVertexAttribArray(tex_att);


	glVertexAttribPointer(pos_att, 2, GL_FLOAT, GL_FALSE,
			4 * sizeof(GLfloat), &tile_verts1[0]);
	glVertexAttribPointer(tex_att, 2, GL_FLOAT, GL_FALSE,
			4 * sizeof(GLfloat), &tile_verts1[2]);
	glEnableVertexAttribArray(pos_att);
	glEnableVertexAttribArray(tex_att);

	/* Write individual buffers */
	wl_list_for_each(gr_buf, &vbt->vm_buffer_info_list, elm) {
		glBindTexture(GL_TEXTURE_2D, textures[i+1]);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

		glTexImage2D(GL_TEXTURE_2D, 0, GL_BGRA_EXT,
				sizeof(struct vm_buffer_info), 1, 0,
				GL_BGRA_EXT, GL_UNSIGNED_BYTE, &gr_buf->vm_buffer_info);
		glUniform1i(bufpos_uniform,
				(sizeof(struct vm_header) + (i*sizeof(struct vm_buffer_info)))/4);
		glUniform1i(tex_uniform, 0);

		glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
		i++;
	}

	/* Make sure we delete the textures */
	glDeleteTextures(num_textures, textures);
	free(textures);
	return 1;
}

void vm_output_init(struct weston_output *output)
{
	output->disable_planes++;
}

void pin_bo(struct gl_renderer *gr, void *buf, struct vm_buffer_info *vb)
{
	int ret;

	if(!gr->pin_bo) {
		return;
	}

	ret = gr->pin_bo(gr->egl_display, buf, &vb->ggtt_offset);
	if(!ret) {
		return;
	}

	if(gl_renderer_interface.vm_dbg) {
		weston_log("Pinned ggtt_offset = 0x%lX\n", vb->ggtt_offset);
	}
}


void unpin_bo(struct gl_renderer *gr, void *buf)
{
	int ret;

	if(!gr->unpin_bo) {
		return;
	}

	ret = gr->unpin_bo(gr->egl_display, buf);
	if(!ret) {
		return;
	}
}
