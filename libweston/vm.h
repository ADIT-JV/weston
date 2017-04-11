#ifndef __VM_H__
#define __VM_H__

#include "vm-shared.h"
#include "gl-renderer.h"
#include "ias-backend.h"

#define VM_INIT(gr)                 if(gl_renderer_interface.vm_exec) \
										{ vm_init(gr); }
#define VM_TABLE_DRAW(o, go, gr)    if(gl_renderer_interface.vm_exec) \
										{ struct ias_output *io = (struct ias_output *) o; \
										  if(io->vm) \
											{ if(vm_table_draw(o, go, gr)) { return; } } \
										}
#define VM_OUTPUT_INIT(o)           if(gl_renderer_interface.vm_exec) \
										{ struct ias_output *io = (struct ias_output *) o; \
										  if(io->vm) \
											{ vm_output_init(o); } \
										}
#define VM_DESTROY(gr)              if(gl_renderer_interface.vm_exec) \
										{ vm_destroy(gr); }

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
};

void vm_init(struct gl_renderer *gr);
int vm_table_draw(struct weston_output *output, struct gl_output_state *go,
		struct gl_renderer *gr);
void vm_output_init(struct weston_output *output);
void pin_bo(struct gl_renderer *gr, void *buf, struct vm_buffer_info *vb);
void unpin_bo(struct gl_renderer *gr, void *buf);
void vm_destroy(struct gl_renderer *gr);


 #endif
