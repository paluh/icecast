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

#ifndef __SOURCE_H__
#define __SOURCE_H__

#include "cfgfile.h"
#include "yp.h"
#include "util.h"
#include "format.h"

#include <stdio.h>

struct auth_tag;

typedef struct source_tag
{
    client_t *client;
    connection_t *con;
    http_parser_t *parser;
    
    char *mount;

    /* If this source drops, try to move all clients to this fallback */
    char *fallback_mount;

    /* set to zero to request the source to shutdown without causing a global
     * shutdown */
    int running;

    struct _format_plugin_tag *format;

    avl_tree *client_tree;
    avl_tree *pending_tree;

    rwlock_t *shutdown_rwlock;
    ypdata_t *ypdata[MAX_YP_DIRECTORIES];
    util_dict *audio_info;

    char *dumpfilename; /* Name of a file to dump incoming stream to */
    FILE *dumpfile;

    int    num_yp_directories;
    long listeners;
    long max_listeners;
    int yp_public;
    int send_return;
    struct auth_tag *authenticator;
    int fallback_override;
    int no_mount;
} source_t;

source_t *source_create(client_t *client, connection_t *con, 
        http_parser_t *parser, const char *mount, format_type_t type,
        mount_proxy *mountinfo);
source_t *source_find_mount(const char *mount);
source_t *source_find_mount_raw(const char *mount);
client_t *source_find_client(source_t *source, int id);
int source_compare_sources(void *arg, void *a, void *b);
int source_free_source(void *key);
int source_remove_client(void *key);
void *source_main(void *arg);

#endif


