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

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>

#ifdef HAVE_POLL
#include <sys/poll.h>
#endif

#ifndef _WIN32
#include <unistd.h>
#include <sys/time.h>
#include <sys/socket.h>
#else
#include <winsock2.h>
#include <windows.h>
#define snprintf _snprintf
#define S_ISREG(mode)  ((mode) & _S_IFREG)
#endif

#include "thread/thread.h"
#include "avl/avl.h"
#include "httpp/httpp.h"
#include "net/sock.h"

#include "connection.h"
#include "global.h"
#include "refbuf.h"
#include "client.h"
#include "stats.h"
#include "format.h"
#include "logging.h"
#include "cfgfile.h"
#include "util.h"
#include "compat.h"

#include "fserve.h"

#undef CATMODULE
#define CATMODULE "fserve"

#define BUFSIZE 4096

#ifdef _WIN32
#define MIMETYPESFILE ".\\mime.types"
#else
#define MIMETYPESFILE "/etc/mime.types"
#endif

static fserve_t *active_list = NULL;
volatile static fserve_t *pending_list = NULL;

static mutex_t pending_lock;
static avl_tree *mimetypes = NULL;

static thread_type *fserv_thread;
static int run_fserv = 0;
static unsigned int fserve_clients;
static int client_tree_changed=0;

#ifdef HAVE_POLL
static struct pollfd *ufds = NULL;
#else
static fd_set fds;
static int fd_max = -1;
#endif

typedef struct {
    char *ext;
    char *type;
} mime_type;

static void fserve_client_destroy(fserve_t *fclient);
static int _delete_mapping(void *mapping);
static void *fserv_thread_function(void *arg);
static void create_mime_mappings(const char *fn);

void fserve_initialize(void)
{
    create_mime_mappings(MIMETYPESFILE);

    thread_mutex_create (&pending_lock);

    run_fserv = 1;
    stats_event (NULL, "file_connections", "0");

    fserv_thread = thread_create("File Serving Thread", 
            fserv_thread_function, NULL, THREAD_ATTACHED);
}

void fserve_shutdown(void)
{
    if(!run_fserv)
        return;

    run_fserv = 0;
    thread_join(fserv_thread);
    INFO0("file serving thread stopped");
    avl_tree_free(mimetypes, _delete_mapping);
}

#ifdef HAVE_POLL
int fserve_client_waiting (void)
{
    fserve_t *fclient;
    unsigned int i = 0;

    /* only rebuild ufds if there are clients added/removed */
    if (client_tree_changed)
    {
        client_tree_changed = 0;
        ufds = realloc(ufds, fserve_clients * sizeof(struct pollfd));
        fclient = active_list;
        while (fclient)
        {
            ufds[i].fd = fclient->client->con->sock;
            ufds[i].events = POLLOUT;
            ufds[i].revents = 0;
            fclient = fclient->next;
            i++;
        }
    }
    if (!ufds)
        thread_sleep(200000);
    else if (poll(ufds, fserve_clients, 200) > 0)
    {
        /* mark any clients that are ready */
        fclient = active_list;
        for (i=0; i<fserve_clients; i++)
        {
            if (ufds[i].revents & (POLLOUT|POLLHUP|POLLERR))
                fclient->ready = 1;
            fclient = fclient->next;
        }
        return 1;
    }
    return 0;
}
#else
int fserve_client_waiting (void)
{
    fserve_t *fclient;
    fd_set realfds;

    /* only rebuild fds if there are clients added/removed */
    if(client_tree_changed) {
        client_tree_changed = 0;
        FD_ZERO(&fds);
        fd_max = -1;
        fclient = active_list;
        while (fclient) {
            FD_SET (fclient->client->con->sock, &fds);
            if (fclient->client->con->sock > fd_max)
                fd_max = fclient->client->con->sock;
            fclient = fclient->next;
        }
    }
    /* hack for windows, select needs at least 1 descriptor */
    if (fd_max == -1)
        thread_sleep (200000);
    else
    {
        struct timeval tv;
        tv.tv_sec = 0;
        tv.tv_usec = 200000;
        /* make a duplicate of the set so we do not have to rebuild it
         * each time around */
        memcpy(&realfds, &fds, sizeof(fd_set));
        if(select(fd_max+1, NULL, &realfds, NULL, &tv) > 0)
        {
            /* mark any clients that are ready */
            fclient = active_list;
            while (fclient)
            {
                if (FD_ISSET (fclient->client->con->sock, &realfds))
                    fclient->ready = 1;
                fclient = fclient->next;
            }
            return 1;
        }
    }
    return 0;
}
#endif

static void wait_for_fds() {
    fserve_t *fclient;

    while (run_fserv)
    {
        /* add any new clients here */
        if (pending_list)
        {
            thread_mutex_lock (&pending_lock);

            fclient = (fserve_t*)pending_list;
            while (fclient)
            {
                fserve_t *to_move = fclient;
                fclient = fclient->next;
                to_move->next = active_list;
                active_list = to_move;
                client_tree_changed = 1;
                fserve_clients++;
            }
            pending_list = NULL;
            thread_mutex_unlock (&pending_lock);
        }
        /* drop out of here if someone is ready */
        if (fserve_client_waiting())
            break;
    }
}

static void *fserv_thread_function(void *arg)
{
    fserve_t *fclient, **trail;
    int sbytes, bytes;

    INFO0("file serving thread started");
    while (run_fserv) {
        wait_for_fds();

        fclient = active_list;
        trail = &active_list;

        while (fclient)
        {
            /* process this client, if it is ready */
            if (fclient->ready)
            {
                client_t *client = fclient->client;
                refbuf_t *refbuf = client->refbuf;
                fclient->ready = 0;
                if (client->pos == refbuf->len)
                {
                    /* Grab a new chunk */
                    if (fclient->file)
                        bytes = fread (refbuf->data, 1, BUFSIZE, fclient->file);
                    else
                        bytes = 0;
                    if (bytes == 0)
                    {
                        fserve_t *to_go = fclient;
                        fclient = fclient->next;
                        *trail = fclient;
                        fserve_client_destroy (to_go);
                        fserve_clients--;
                        client_tree_changed = 1;
                        continue;
                    }
                    refbuf->len = bytes;
                    client->pos = 0;
                }

                /* Now try and send current chunk. */
                sbytes = format_generic_write_to_client (client);

                if (client->con->error)
                {
                    fserve_t *to_go = fclient;
                    fclient = fclient->next;
                    *trail = fclient;
                    fserve_clients--;
                    fserve_client_destroy (to_go);
                    client_tree_changed = 1;
                    continue;
                }
            }
            trail = &fclient->next;
            fclient = fclient->next;
        }
    }

    /* Shutdown path */
    thread_mutex_lock (&pending_lock);
    while (pending_list)
    {
        fserve_t *to_go = (fserve_t *)pending_list;
        pending_list = to_go->next;

        fserve_client_destroy (to_go);
    }
    thread_mutex_unlock (&pending_lock);

    while (active_list)
    {
        fserve_t *to_go = active_list;
        active_list = to_go->next;
        fserve_client_destroy (to_go);
    }

    return NULL;
}

char *fserve_content_type (const char *path)
{
    char *ext = util_get_extension(path);
    mime_type exttype = {ext, NULL};
    void *result;

    if (!avl_get_by_key (mimetypes, &exttype, &result))
    {
        mime_type *mime = result;
        return mime->type;
    }
    else {
        /* Fallbacks for a few basic ones */
        if(!strcmp(ext, "ogg"))
            return "application/ogg";
        else if(!strcmp(ext, "mp3"))
            return "audio/mpeg";
        else if(!strcmp(ext, "html"))
            return "text/html";
        else if(!strcmp(ext, "css"))
            return "text/css";
        else if(!strcmp(ext, "txt"))
            return "text/plain";
        else if(!strcmp(ext, "jpg"))
            return "image/jpeg";
        else if(!strcmp(ext, "png"))
            return "image/png";
        else if(!strcmp(ext, "m3u"))
            return "audio/x-mpegurl";
        else
            return "application/octet-stream";
    }
}

static void fserve_client_destroy(fserve_t *fclient)
{
    if (fclient)
    {
        if (fclient->file)
            fclose (fclient->file);

        if (fclient->client)
            client_destroy (fclient->client);
        free (fclient);
    }
}


int fserve_client_create(client_t *httpclient, const char *path)
{
    int bytes;
    struct stat file_buf;
    char *range = NULL;
    int64_t new_content_len = 0;
    int64_t rangenumber = 0, content_length;
    int rangeproblem = 0;
    int ret = 0;
    char *fullpath;
    int m3u_requested = 0, m3u_file_available = 1;
    ice_config_t *config;
    FILE *file;

    fullpath = util_get_path_from_normalised_uri (path);
    INFO2 ("checking for file %s (%s)", path, fullpath);

    if (strcmp (util_get_extension (fullpath), "m3u") == 0)
        m3u_requested = 1;

    /* check for the actual file */
    if (stat (fullpath, &file_buf) != 0)
    {
        /* the m3u can be generated, but send an m3u file if available */
        if (m3u_requested == 0)
        {
            free (fullpath);
            return 0;
        }
        m3u_file_available = 0;
    }

    client_set_queue (httpclient, NULL);
    httpclient->refbuf = refbuf_new (BUFSIZE);

    if (m3u_requested && m3u_file_available == 0)
    {
        char *host = httpp_getvar (httpclient->parser, "host");
        char *sourceuri = strdup (path);
        char *dot = strrchr(sourceuri, '.');
        *dot = 0;
        httpclient->respcode = 200;
        if (host == NULL)
        {
            config = config_get_config();
            snprintf (httpclient->refbuf->data, BUFSIZE,
                    "HTTP/1.0 200 OK\r\n"
                    "Content-Type: audio/x-mpegurl\r\n\r\n"
                    "http://%s:%d%s\r\n", 
                    config->hostname, config->port,
                    sourceuri
                    );
            config_release_config();
        }
        else
        {
            snprintf (httpclient->refbuf->data, BUFSIZE,
                    "HTTP/1.0 200 OK\r\n"
                    "Content-Type: audio/x-mpegurl\r\n\r\n"
                    "http://%s%s\r\n", 
                    host, 
                    sourceuri
                    );
        }
        httpclient->refbuf->len = strlen (httpclient->refbuf->data);
        fserve_add_client (httpclient, NULL);
        free (sourceuri);
        free (fullpath);
        return 1;
    }

    /* on demand file serving check */
    config = config_get_config();
    if (config->fileserve == 0)
    {
        DEBUG1 ("on demand file \"%s\" refused", fullpath);
        client_send_404 (httpclient, "The file you requested could not be found");
        config_release_config();
        free (fullpath);
        return 0;
    }
    config_release_config();

    if (S_ISREG (file_buf.st_mode) == 0)
    {
        client_send_404 (httpclient, "The file you requested could not be found");
        WARN1 ("found requested file but there is no handler for it: %s", fullpath);
        free (fullpath);
        return 1;
    }

    file = fopen (fullpath, "rb");
    free (fullpath);
    if (file == NULL)
    {
        WARN1 ("Problem accessing file \"%s\"", fullpath);
        client_send_404 (httpclient, "File not readable");
        return 1;
    }

    content_length = (int64_t)file_buf.st_size;
    range = httpp_getvar (httpclient->parser, "range");

    if (range != NULL) {
        ret = sscanf(range, "bytes=" FORMAT_INT64 "-", &rangenumber);
        if (ret != 1) {
            /* format not correct, so lets just assume
               we start from the beginning */
            rangeproblem = 1;
        }
        if (rangenumber < 0) {
            rangeproblem = 1;
        }
        if (!rangeproblem) {
            ret = fseek (file, rangenumber, SEEK_SET);
            if (ret != -1) {
                new_content_len = content_length - rangenumber;
                if (new_content_len < 0) {
                    rangeproblem = 1;
                }
            }
            else {
                rangeproblem = 1;
            }
            if (!rangeproblem) {
                /* Date: is required on all HTTP1.1 responses */
                char currenttime[50];
                time_t now;
                int strflen;
                struct tm result;
                int64_t endpos = rangenumber+new_content_len-1;
                if (endpos < 0) {
                    endpos = 0;
                }
                time(&now);
                strflen = strftime(currenttime, 50, "%a, %d-%b-%Y %X GMT",
                                   gmtime_r(&now, &result));
                httpclient->respcode = 206;
                bytes = snprintf (httpclient->refbuf->data, BUFSIZE,
                    "HTTP/1.1 206 Partial Content\r\n"
                    "Date: %s\r\n"
                    "Content-Length: " FORMAT_INT64 "\r\n"
                    "Content-Range: bytes " FORMAT_INT64 \
                    "-" FORMAT_INT64 "/" FORMAT_INT64 "\r\n"
                    "Content-Type: %s\r\n\r\n",
                    currenttime,
                    new_content_len,
                    rangenumber,
                    endpos,
                    content_length,
                    fserve_content_type(path));
            }
            else {
                httpclient->respcode = 416;
                sock_write (httpclient->con->sock,
                    "HTTP/1.0 416 Request Range Not Satisfiable\r\n\r\n");
                client_destroy (httpclient);
                return 1;
            }
        }
        else {
            /* If we run into any issues with the ranges
               we fallback to a normal/non-range request */
            httpclient->respcode = 416;
            sock_write (httpclient->con->sock,
                "HTTP/1.0 416 Request Range Not Satisfiable\r\n\r\n");
            client_destroy (httpclient);
            return 1;
        }
    }
    else {

        httpclient->respcode = 200;
        bytes = snprintf (httpclient->refbuf->data, BUFSIZE,
            "HTTP/1.0 200 OK\r\n"
            "Content-Length: " FORMAT_INT64 "\r\n"
            "Content-Type: %s\r\n\r\n",
            content_length,
            fserve_content_type(path));
    }
    httpclient->refbuf->len = bytes;
    httpclient->pos = 0;

    stats_event_inc (NULL, "file_connections");
    fserve_add_client (httpclient, file);

    return 1;
}


/* Add client to fserve thread, client needs to have refbuf set and filled
 * but may provide a NULL file if no data needs to be read
 */
int fserve_add_client (client_t *client, FILE *file)
{
    fserve_t *fclient = calloc (1, sizeof(fserve_t));

    DEBUG0 ("Adding client to file serving engine");
    if (fclient == NULL)
    {
        client_send_404 (client, "memory exhausted");
        return -1;
    }
    fclient->file = file;
    fclient->client = client;
    fclient->ready = 0;

    sock_set_blocking (client->con->sock, SOCK_NONBLOCK);
    sock_set_nodelay (client->con->sock);

    thread_mutex_lock (&pending_lock);
    fclient->next = (fserve_t *)pending_list;
    pending_list = fclient;
    thread_mutex_unlock (&pending_lock);

    return 0;
}


static int _delete_mapping(void *mapping) {
    mime_type *map = mapping;
    free(map->ext);
    free(map->type);
    free(map);

    return 1;
}

static int _compare_mappings(void *arg, void *a, void *b)
{
    return strcmp(
            ((mime_type *)a)->ext,
            ((mime_type *)b)->ext);
}

static void create_mime_mappings(const char *fn) {
    FILE *mimefile = fopen(fn, "r");
    char line[4096];
    char *type, *ext, *cur;
    mime_type *mapping;

    mimetypes = avl_tree_new(_compare_mappings, NULL);

    if (mimefile == NULL)
    {
        WARN1 ("Cannot open mime type file %s", fn);
        return;
    }

    while(fgets(line, 4096, mimefile))
    {
        line[4095] = 0;

        if(*line == 0 || *line == '#')
            continue;

        type = line;

        cur = line;

        while(*cur != ' ' && *cur != '\t' && *cur)
            cur++;
        if(*cur == 0)
            continue;

        *cur++ = 0;

        while(1) {
            while(*cur == ' ' || *cur == '\t')
                cur++;
            if(*cur == 0)
                break;

            ext = cur;
            while(*cur != ' ' && *cur != '\t' && *cur != '\n' && *cur)
                cur++;
            *cur++ = 0;
            if(*ext)
            {
                void *tmp;
                /* Add a new extension->type mapping */
                mapping = malloc(sizeof(mime_type));
                mapping->ext = strdup(ext);
                mapping->type = strdup(type);
                if(!avl_get_by_key(mimetypes, mapping, &tmp))
                    avl_delete(mimetypes, mapping, _delete_mapping);
                avl_insert(mimetypes, mapping);
            }
        }
    }

    fclose(mimefile);
}

