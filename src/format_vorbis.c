/* format_vorbis.c
**
** format plugin for vorbis
**
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <ogg/ogg.h>
#include <vorbis/codec.h>

#include "refbuf.h"

#include "format.h"

typedef struct _vstate_tag
{
	ogg_sync_state oy;
	ogg_page og;
	unsigned long serialno;
	int header;
	refbuf_t *headbuf[10];
} vstate_t;

void format_vorbis_free_plugin(format_plugin_t *self);
refbuf_t *format_vorbis_get_buffer(format_plugin_t *self, char *data, unsigned long len);
refbuf_queue_t *format_vorbis_get_predata(format_plugin_t *self);

format_plugin_t *format_vorbis_get_plugin(void)
{
	format_plugin_t *plugin;
	vstate_t *state;

	plugin = (format_plugin_t *)malloc(sizeof(format_plugin_t));

	plugin->type = FORMAT_TYPE_VORBIS;
	plugin->has_predata = 1;
	plugin->get_buffer = format_vorbis_get_buffer;
	plugin->get_predata = format_vorbis_get_predata;
	plugin->free_plugin = format_vorbis_free_plugin;

	state = (vstate_t *)calloc(1, sizeof(vstate_t));
	ogg_sync_init(&state->oy);

	plugin->_state = (void *)state;

	return plugin;
}

void format_vorbis_free_plugin(format_plugin_t *self)
{
	int i;
	vstate_t *state = (vstate_t *)self->_state;

	/* free memory associated with this plugin instance */

	/* free state memory */
	ogg_sync_clear(&state->oy);
	
	for (i = 0; i < 10; i++) {
		if (state->headbuf[i]) {
			refbuf_release(state->headbuf[i]);
			state->headbuf[i] = NULL;
		}
	}

	free(state);

	/* free the plugin instance */
	free(self);
}

refbuf_t *format_vorbis_get_buffer(format_plugin_t *self, char *data, unsigned long len)
{
	char *buffer;
	refbuf_t *refbuf;
	int i;
	vstate_t *state = (vstate_t *)self->_state;

	if (data) {
		/* write the data to the buffer */
		buffer = ogg_sync_buffer(&state->oy, len);
	        memcpy(buffer, data, len);
		ogg_sync_wrote(&state->oy, len);
	}

	refbuf = NULL;
	if (ogg_sync_pageout(&state->oy, &state->og) == 1) {
		refbuf = refbuf_new(state->og.header_len + state->og.body_len);
		memcpy(refbuf->data, state->og.header, state->og.header_len);
		memcpy(&refbuf->data[state->og.header_len], state->og.body, state->og.body_len);

		if (state->serialno != ogg_page_serialno(&state->og)) {
			/* this is a new logical bitstream */
			state->header = 0;
			for (i = 0; i < 10; i++) {
				if (state->headbuf[i]) {
					refbuf_release(state->headbuf[i]);
					state->headbuf[i] = NULL;
				}
			}
					
			state->serialno = ogg_page_serialno(&state->og);
		}

		if (state->header >= 0) {
			if (ogg_page_granulepos(&state->og) == 0) {
				state->header++;
			} else {
				state->header = 0;
			}
		}

		/* cache first three pages */
		if (state->header) {
			refbuf_addref(refbuf);
			state->headbuf[state->header - 1] = refbuf;
		}
	}

	return refbuf;
}

refbuf_queue_t *format_vorbis_get_predata(format_plugin_t *self)
{
	refbuf_queue_t *queue;
	int i;
	vstate_t *state = (vstate_t *)self->_state;

	queue = NULL;
	for (i = 0; i < 10; i++) {
		if (state->headbuf[i]) {
			refbuf_addref(state->headbuf[i]);
			refbuf_queue_add(&queue, state->headbuf[i]);
		} else {
			break;
		}
	}

	return queue;
}



