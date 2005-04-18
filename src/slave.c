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

/* -*- c-basic-offset: 4; indent-tabs-mode: nil; -*- */
/* slave.c
 * by Ciaran Anscomb <ciaran.anscomb@6809.org.uk>
 *
 * Periodically requests a list of streams from a master server
 * and creates source threads for any it doesn't already have.
 * */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/types.h>

#ifndef _WIN32
#include <sys/time.h>
#include <sys/socket.h>
#include <netinet/in.h>
#else
#include <winsock2.h>
#define snprintf _snprintf
#define strcasecmp stricmp
#define strncasecmp strnicmp
#endif

#include "os.h"

#include "thread/thread.h"
#include "avl/avl.h"
#include "net/sock.h"
#include "httpp/httpp.h"

#include "cfgfile.h"
#include "global.h"
#include "util.h"
#include "connection.h"
#include "refbuf.h"
#include "client.h"
#include "stats.h"
#include "logging.h"
#include "source.h"
#include "format.h"

#define CATMODULE "slave"

static void *_slave_thread(void *arg);
static thread_type *_slave_thread_id;
static int slave_running = 0;
volatile static unsigned int max_interval = 0;
volatile static int rescan_relays = 0;

relay_server *relay_free (relay_server *relay)
{
    relay_server *next = relay->next;
    DEBUG1("freeing relay %s", relay->localmount);
    if (relay->source)
       source_free_source (relay->source);
    xmlFree (relay->server);
    xmlFree (relay->mount);
    xmlFree (relay->localmount);
    if (relay->username)
        xmlFree (relay->username);
    if (relay->password)
        xmlFree (relay->password);
    free (relay);
    return next;
}


relay_server *relay_copy (relay_server *r)
{
    relay_server *copy = calloc (1, sizeof (relay_server));

    if (copy)
    {
        copy->server = xmlStrdup (r->server);
        copy->mount = xmlStrdup (r->mount);
        copy->localmount = xmlStrdup (r->localmount);
        if (r->username)
            copy->username = xmlStrdup (r->username);
        if (r->password)
            copy->password = xmlStrdup (r->password);
        copy->port = r->port;
        copy->mp3metadata = r->mp3metadata;
    }
    return copy;
}


/* force a recheck of the relays. This will recheck the master server if
 * a this is a slave.
 */
void slave_recheck (void)
{
    max_interval = 0;
}

/* rescan the current relays to see if any need starting or if any
 * relay threads have terminated
 */
void slave_rescan (void)
{
    rescan_relays = 1;
}


void slave_initialize(void)
{
    if (slave_running)
        return;

    slave_running = 1;
    max_interval = 0;
    _slave_thread_id = thread_create("Slave Thread", _slave_thread, NULL, THREAD_ATTACHED);
}


void slave_shutdown(void)
{
    if (!slave_running)
        return;
    slave_running = 0;
    DEBUG0 ("waiting for slave thread");
    thread_join (_slave_thread_id);
}


/* This does the actual connection for a relay. A thread is
 * started off if a connection can be acquired
 */
static void *start_relay_stream (void *arg)
{
    relay_server *relay = arg;
    sock_t streamsock = SOCK_ERROR;
    source_t *src = relay->source;
    http_parser_t *parser = NULL;
    connection_t *con=NULL;
    char header[4096];

    relay->running = 1;
    INFO1("Starting relayed source at mountpoint \"%s\"", relay->localmount);
    do
    {
        char *auth_header;

        streamsock = sock_connect_wto (relay->server, relay->port, 30);
        if (streamsock == SOCK_ERROR)
        {
            WARN3("Failed to relay stream from master server, couldn't connect to http://%s:%d%s",
                    relay->server, relay->port, relay->mount);
            break;
        }
        con = create_connection (streamsock, -1, NULL);

        if (relay->username && relay->password)
        {
            char *esc_authorisation;
            unsigned len = strlen(relay->username) + strlen(relay->password) + 2;

            auth_header = malloc (len);
            snprintf (auth_header, len, "%s:%s", relay->username, relay->password);
            esc_authorisation = util_base64_encode(auth_header);
            free(auth_header);
            len = strlen (esc_authorisation) + 24;
            auth_header = malloc (len);
            snprintf (auth_header, len,
                    "Authorization: Basic %s\r\n", esc_authorisation);
            free(esc_authorisation);
        }
        else
        {
            auth_header = strdup ("");
        }

        /* At this point we may not know if we are relaying an mp3 or vorbis
         * stream, but only send the icy-metadata header if the relay details
         * state so (the typical case).  It's harmless in the vorbis case. If
         * we don't send in this header then relay will not have mp3 metadata.
         */
        sock_write(streamsock, "GET %s HTTP/1.0\r\n"
                "User-Agent: " ICECAST_VERSION_STRING "\r\n"
                "%s"
                "%s"
                "\r\n",
                relay->mount,
                relay->mp3metadata?"Icy-MetaData: 1\r\n":"",
                auth_header);
        free (auth_header);
        memset (header, 0, sizeof(header));
        if (util_read_header (con->sock, header, 4096, READ_ENTIRE_HEADER) == 0)
        {
            WARN0("Header read failed");
            break;
        }
        parser = httpp_create_parser();
        httpp_initialize (parser, NULL);
        if (! httpp_parse_response (parser, header, strlen(header), relay->localmount))
        {
            ERROR0("Error parsing relay request");
            break;
        }
        if (httpp_getvar (parser, HTTPP_VAR_ERROR_MESSAGE))
        {
            ERROR1("Error from relay request: %s", httpp_getvar(parser, HTTPP_VAR_ERROR_MESSAGE));
            break;
        }
        src->parser = parser;
        src->con = con;
        if (connection_complete_source (src) < 0)
        {
            DEBUG0("Failed to complete source initialisation");
            break;
        }
        stats_event_inc(NULL, "source_relay_connections");

        source_main (relay->source);

        /* initiate an immediate relay cleanup run */
        relay->cleanup = 1;
        slave_rescan();

        return NULL;
    } while (0);

    if (con == NULL && streamsock != SOCK_ERROR)
        sock_close (streamsock);
    if (con)
        connection_close (con);
    src->con = NULL;
    if (parser)
        httpp_destroy (parser);
    src->parser = NULL;
    source_clear_source (relay->source);

    /* initiate an immediate relay cleanup run */
    relay->cleanup = 1;
    slave_rescan();

    return NULL;
}


/* wrapper for starting the provided relay stream */
static void check_relay_stream (relay_server *relay)
{
    if (relay->source == NULL)
    {
        if (relay->localmount[0] != '/')
        {
            WARN1 ("relay mountpoint \"%s\" does not start with /, skipping",
                    relay->localmount);
            return;
        }
        /* new relay, reserve the name */
        relay->source = source_reserve (relay->localmount);
        if (relay->source)
            DEBUG1("Adding relay source at mountpoint \"%s\"", relay->localmount);
        else
            WARN1 ("new relay but source \"%s\" already exists", relay->localmount);
    }
    if (relay->source && !relay->running)
    {
        relay->thread = thread_create ("Relay Thread", start_relay_stream,
                relay, THREAD_ATTACHED);
        return;
    }
    /* the relay thread may of close down */
    if (relay->cleanup && relay->thread)
    {
        DEBUG1 ("waiting for relay thread for \"%s\"", relay->localmount);
        thread_join (relay->thread);
        relay->thread = NULL;
        relay->cleanup = 0;
        relay->running = 0;
    }
}


/* go through updated looking for relays that are different configured. The
 * returned list contains relays that should be kept running, current contains
 * the list of relays to shutdown
 */
static relay_server *
update_relay_set (relay_server **current, relay_server *updated)
{
    relay_server *relay = updated;
    relay_server *existing_relay, **existing_p;
    relay_server *new_list = NULL;

    while (relay)
    {
        existing_relay = *current;
        existing_p = current;

        while (existing_relay)
        {
            /* break out if keeping relay */
            if (strcmp (relay->localmount, existing_relay->localmount) == 0)
                break;
            existing_p = &existing_relay->next;
            existing_relay = existing_relay->next;
        }
        if (existing_relay == NULL)
        {
            /* new one, copy and insert */
            existing_relay = relay_copy (relay);
        }
        else
        {
            *existing_p = existing_relay->next;
        }
        existing_relay->next = new_list;
        new_list = existing_relay;
        relay = relay->next;
    }
    return new_list;
}


/* update the relay_list with entries from new_relay_list. Any new relays
 * are added to the list, and any not listed in the provided new_relay_list
 * are separated and returned in a separate list
 */
static relay_server *
update_relays (relay_server **relay_list, relay_server *new_relay_list)
{
    relay_server *active_relays, *cleanup_relays;

    active_relays = update_relay_set (relay_list, new_relay_list);

    cleanup_relays = *relay_list;
    /* re-assign new set */
    *relay_list = active_relays;

    return cleanup_relays;
}


static void relay_check_streams (relay_server *to_start, relay_server *to_free)
{
    relay_server *relay;

    while (to_free)
    {
        if (to_free->running && to_free->source)
        {
            DEBUG1 ("source shutdown request on \"%s\"", to_free->localmount);
            to_free->source->running = 0;
            thread_join (to_free->thread);
        }
        to_free = relay_free (to_free);
    }

    relay = to_start;
    while (relay)
    {
        check_relay_stream (relay);
        relay = relay->next;
    }
}


static int update_from_master(ice_config_t *config)
{
    char *master = NULL, *password = NULL, *username= NULL;
    int port;
    sock_t mastersock;
    int ret = 0;
    char buf[256];
    do
    {
        char *authheader, *data;
        relay_server *new_relays = NULL, *cleanup_relays;
        int len, count = 1;

        username = strdup ("relay");
        if (config->master_password)
            password = strdup (config->master_password);

        if (config->master_server)
            master = strdup (config->master_server);

        port = config->master_server_port;

        if (password == NULL || master == NULL || port == 0)
            break;
        ret = 1;
        config_release_config();
        mastersock = sock_connect_wto (master, port, 0);

        if (mastersock == SOCK_ERROR)
        {
            WARN0("Relay slave failed to contact master server to fetch stream list");
            break;
        }

        len = strlen(username) + strlen(password) + 2;
        authheader = malloc(len);
        snprintf (authheader, len, "%s:%s", username, password);
        data = util_base64_encode(authheader);
        sock_write (mastersock,
                "GET /admin/streamlist.txt HTTP/1.0\r\n"
                "Authorization: Basic %s\r\n"
                "\r\n", data);
        free(authheader);
        free(data);

        if (sock_read_line(mastersock, buf, sizeof(buf)) == 0 ||
                strncmp (buf, "HTTP/1.0 200", 12) != 0)
        {
            sock_close (mastersock);
            WARN0 ("Master rejected streamlist request");
            break;
        }

        while (sock_read_line(mastersock, buf, sizeof(buf)))
        {
            if (!strlen(buf))
                break;
        }
        while (sock_read_line(mastersock, buf, sizeof(buf)))
        {
            relay_server *r;
            if (!strlen(buf))
                continue;
            DEBUG2 ("read %d from master \"%s\"", count++, buf);
            r = calloc (1, sizeof (relay_server));
            if (r)
            {
                r->server = xmlStrdup (master);
                r->port = port;
                r->mount = xmlStrdup (buf);
                r->localmount = xmlStrdup (buf);
                r->mp3metadata = 1;
                r->next = new_relays;
                new_relays = r;
            }
        }
        sock_close (mastersock);

        thread_mutex_lock (&(config_locks()->relay_lock));
        cleanup_relays = update_relays (&global.master_relays, new_relays);
        
        relay_check_streams (global.master_relays, cleanup_relays);
        relay_check_streams (NULL, new_relays);

        thread_mutex_unlock (&(config_locks()->relay_lock));

    } while(0);

    if (master)
        free (master);
    if (username)
        free (username);
    if (password)
        free (password);

    return ret;
}


static void *_slave_thread(void *arg)
{
    ice_config_t *config;
    unsigned int interval = 0;

    while (1)
    {
        relay_server *cleanup_relays;

        thread_sleep (1000000);
        if (slave_running == 0)
            break;
        if (rescan_relays == 0 && max_interval > ++interval)
            continue;

        /* only update relays lists when required */
        if (max_interval <= interval)
        {
            DEBUG0 ("checking master stream list");
            config = config_get_config();

            interval = 0;
            max_interval = config->master_update_interval;

            /* the connection could take some time, so the lock can drop */
            if (update_from_master (config))
                config = config_get_config();

            thread_mutex_lock (&(config_locks()->relay_lock));

            cleanup_relays = update_relays (&global.relays, config->relay);

            config_release_config();

            relay_check_streams (global.relays, cleanup_relays);
            thread_mutex_unlock (&(config_locks()->relay_lock));
        }
        else
        {
            DEBUG0 ("rescanning relay lists");
            thread_mutex_lock (&(config_locks()->relay_lock));
            relay_check_streams (global.master_relays, NULL);
            relay_check_streams (global.relays, NULL);
            thread_mutex_unlock (&(config_locks()->relay_lock));
        }
        rescan_relays = 0;
    }
    DEBUG0 ("shutting down current relays");
    relay_check_streams (NULL, global.relays);
    relay_check_streams (NULL, global.master_relays);

    INFO0 ("Slave thread shutdown complete");

    return NULL;
}

