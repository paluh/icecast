/* format_mp3.c
**
** format plugin for mp3
**
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "refbuf.h"
#include "source.h"
#include "client.h"

#include "stats.h"
#include "format.h"
#include "httpp/httpp.h"

#include "log.h"
#include "logging.h"

#include "format_mp3.h"

#define CATMODULE "format-mp3"

#define ICY_METADATA_INTERVAL 16000

static void format_mp3_free_plugin(format_plugin_t *self);
static int format_mp3_get_buffer(format_plugin_t *self, char *data, 
        unsigned long len, refbuf_t **buffer);
static refbuf_queue_t *format_mp3_get_predata(format_plugin_t *self);
static void *format_mp3_create_client_data(format_plugin_t *self,
        source_t *source, client_t *client);
static int format_mp3_write_buf_to_client(format_plugin_t *self,
        client_t *client, unsigned char *buf, int len);
static void format_mp3_send_headers(format_plugin_t *self, 
        source_t *source, client_t *client);

typedef struct {
   int use_metadata;
   int interval;
   int offset;
   int metadata_age;
   int metadata_offset;
} mp3_client_data;

#ifdef WIN32
#define alloca _alloca
#endif

format_plugin_t *format_mp3_get_plugin(void)
{
	format_plugin_t *plugin;
    mp3_state *state = calloc(1, sizeof(mp3_state));

	plugin = (format_plugin_t *)malloc(sizeof(format_plugin_t));

	plugin->type = FORMAT_TYPE_MP3;
	plugin->has_predata = 0;
	plugin->get_buffer = format_mp3_get_buffer;
	plugin->get_predata = format_mp3_get_predata;
    plugin->write_buf_to_client = format_mp3_write_buf_to_client;
    plugin->create_client_data = format_mp3_create_client_data;
    plugin->client_send_headers = format_mp3_send_headers;
	plugin->free_plugin = format_mp3_free_plugin;
    plugin->format_description = "MP3 audio";

	plugin->_state = state;

    state->metadata_age = 0;
    state->metadata = strdup("");
    thread_mutex_create(&(state->lock));

	return plugin;
}

static int send_metadata(client_t *client, mp3_client_data *client_state,
        mp3_state *source_state)
{
    int send_metadata;
    int len_byte;
    int len;
    unsigned char *buf;
    int ret;
    int source_age;
    char	*fullmetadata = NULL;
    int	fullmetadata_size = 0;

    thread_mutex_lock(&(source_state->lock));
    if(source_state->metadata == NULL) {
        /* Shouldn't be possible */
        thread_mutex_unlock(&(source_state->lock));
        return 0;
    }

    fullmetadata_size = strlen(source_state->metadata) + strlen("StreamTitle='';StreamUrl=''") + 1;

    fullmetadata = alloca(fullmetadata_size);

    memset(fullmetadata, 0, fullmetadata_size);

    sprintf(fullmetadata, "StreamTitle='%s';StreamUrl=''", source_state->metadata);

    source_age = source_state->metadata_age;
    send_metadata = source_age != client_state->metadata_age;

    if(send_metadata && strlen(fullmetadata) > 0)
        len_byte = strlen(fullmetadata)/16 + 1 - 
            client_state->metadata_offset;
    else
        len_byte = 0;
    len = 1 + len_byte*16;
    buf = alloca(len);

    memset(buf, 0, len);

    buf[0] = len_byte;

    if (len > 1) {
	    strncpy(buf+1, fullmetadata + client_state->metadata_offset, len-2);
	    DEBUG1("Sending metadata (%s)", buf+1);
    }

    thread_mutex_unlock(&(source_state->lock));

    ret = sock_write_bytes(client->con->sock, buf, len);

    if(ret > 0 && ret < len) {
        client_state->metadata_offset += ret;
    }
    else if(ret == len) {
        client_state->metadata_age = source_age;
        client_state->offset = 0;
        client_state->metadata_offset = 0;
    }

    return ret;
}

static int format_mp3_write_buf_to_client(format_plugin_t *self, 
    client_t *client, unsigned char *buf, int len) 
{
    int ret;
    
    if(((mp3_state *)self->_state)->metadata) 
    {
        mp3_client_data *state = client->format_data;
        int max = state->interval - state->offset;

        if(len == 0) /* Shouldn't happen */
            return 0;

        if(max > len)
            max = len;

        if(max > 0) {
            ret = sock_write_bytes(client->con->sock, buf, max);
            if(ret > 0)
                state->offset += ret;
        }
        else {
            ret = send_metadata(client, state, self->_state);
	    ret = 0;
	}

    }
    else {
        ret = sock_write_bytes(client->con->sock, buf, len);
    }

    if(ret < 0) {
        if(sock_recoverable(ret)) {
            DEBUG1("Client had recoverable error %ld", ret);
            ret = 0;
        }
    }
    else
        client->con->sent_bytes += ret;

    return ret;
}

static void format_mp3_free_plugin(format_plugin_t *self)
{
	/* free the plugin instance */
    mp3_state *state = self->_state;
    thread_mutex_destroy(&(state->lock));

    free(state->metadata);
    free(state);
	free(self);
}

static int format_mp3_get_buffer(format_plugin_t *self, char *data, 
    unsigned long len, refbuf_t **buffer)
{
	refbuf_t *refbuf;
    if(!data) {
        *buffer = NULL;
        return 0;
    }
    refbuf = refbuf_new(len);

    memcpy(refbuf->data, data, len);

    *buffer = refbuf;
	return 0;
}

static refbuf_queue_t *format_mp3_get_predata(format_plugin_t *self)
{
    return NULL;
}

static void *format_mp3_create_client_data(format_plugin_t *self, 
        source_t *source, client_t *client) 
{
    mp3_client_data *data = calloc(1,sizeof(mp3_client_data));
    char *metadata;

    data->interval = ICY_METADATA_INTERVAL;
    data->offset = 0;

    metadata = httpp_getvar(client->parser, "icy-metadata");
    if(metadata)
        data->use_metadata = atoi(metadata)>0?1:0;

    return data;
}

static void format_mp3_send_headers(format_plugin_t *self,
        source_t *source, client_t *client)
{
    int bytes;
    
    client->respcode = 200;
    /* TODO: This may need to be ICY/1.0 for shoutcast-compatibility? */
    bytes = sock_write(client->con->sock, 
            "HTTP/1.0 200 OK\r\n" 
            "Content-Type: %s\r\n", 
            format_get_mimetype(source->format->type));

    if(bytes > 0) client->con->sent_bytes += bytes;

    format_send_general_headers(self, source, client);

    if(((mp3_client_data *)(client->format_data))->use_metadata) {
        int bytes = sock_write(client->con->sock, "icy-metaint: %d\r\n", 
                ICY_METADATA_INTERVAL);
        if(bytes > 0)
            client->con->sent_bytes += bytes;
    }
}




