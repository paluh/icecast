/* Icecast
 *
 * This program is distributed under the GNU General Public License, version 2.
 * A copy of this license is included with this source.
 *
 * Copyright 2000-2004, Jack Moffitt <jack@xiph.org, 
 *                      Michael Smith <msmith@xiph.org>,
 *                      oddsock <oddsock@xiph.org>,
 *                      Karl Heyes <karl@xiph.org>
 *                      and others (see AUTHORS for details).
 */

/* -*- c-basic-offset: 4; -*- */
/* format.c
**
** format plugin implementation
**
*/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdlib.h>
#include <string.h>
#ifdef HAVE_STRINGS_H
# include <strings.h>
#endif
#include <time.h>

#include "connection.h"
#include "refbuf.h"

#include "source.h"
#include "format.h"
#include "global.h"
#include "httpp/httpp.h"

#include "format_ogg.h"
#include "format_mp3.h"

#include "logging.h"
#include "stats.h"
#define CATMODULE "format"

#ifdef WIN32
#define strcasecmp stricmp
#define strncasecmp strnicmp
#endif

format_type_t format_get_type(char *contenttype)
{
    if(strcmp(contenttype, "application/x-ogg") == 0)
        return FORMAT_TYPE_OGG; /* Backwards compatibility */
    else if(strcmp(contenttype, "application/ogg") == 0)
        return FORMAT_TYPE_OGG; /* Now blessed by IANA */
    else
        /* We default to the Generic format handler, which
           can handle many more formats than just mp3 */
        return FORMAT_TYPE_GENERIC;
}

int format_get_plugin(format_type_t type, source_t *source)
{
    int ret = -1;

    switch (type) {
    case FORMAT_TYPE_OGG:
        ret = format_ogg_get_plugin (source);
        break;
    case FORMAT_TYPE_GENERIC:
        ret = format_mp3_get_plugin (source);
        break;
    default:
        break;
    }
    if (ret < 0)
        stats_event (source->mount, "content-type", 
                source->format->contenttype);

    return ret;
}

void format_send_general_headers(format_plugin_t *format,
        source_t *source, client_t *client)
{
    http_var_t *var;
    avl_node *node;
    int bytes;

    /* iterate through source http headers and send to client */
    avl_tree_rlock(source->parser->vars);
    node = avl_get_first(source->parser->vars);
    while (node)
    {
        var = (http_var_t *)node->key;
        if (!strcasecmp(var->name, "ice-audio-info")) {
            /* convert ice-audio-info to icy-br */
            char *brfield;
            unsigned int bitrate;

            brfield = strstr(var->value, "bitrate=");
            if (brfield && sscanf(var->value, "bitrate=%u", &bitrate)) {
                bytes = sock_write(client->con->sock, "icy-br:%u\r\n", bitrate);
                if (bytes > 0)
                    client->con->sent_bytes += bytes;
            }
        }
        else
        {
            if (strcasecmp(var->name, "ice-password") &&
                strcasecmp(var->name, "icy-metaint"))
            {
                bytes = 0;
                if (!strncasecmp("ice-", var->name, 4))
                {
                    if (!strcasecmp("ice-bitrate", var->name))
                        bytes += sock_write(client->con->sock, "icy-br:%s\r\n", var->value);
                    else
                        if (!strcasecmp("ice-public", var->name))
                            bytes += sock_write(client->con->sock, 
                                "icy-pub:%s\r\n", var->value);
                        else
                            bytes = sock_write(client->con->sock, "icy%s:%s\r\n",
                                var->name + 3, var->value);
                            
                }
                if (!strncasecmp("icy-", var->name, 4))
                {
                    bytes = sock_write(client->con->sock, "icy%s:%s\r\n",
                            var->name + 3, var->value);
                }
                if (bytes > 0)
                    client->con->sent_bytes += bytes;
            }
        }
        node = avl_get_next(node);
    }
    avl_tree_unlock(source->parser->vars);
    bytes = sock_write(client->con->sock,
            "Server: %s\r\n", ICECAST_VERSION_STRING);
    if(bytes > 0) client->con->sent_bytes += bytes;
}

