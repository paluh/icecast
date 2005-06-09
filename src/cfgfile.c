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
#include <libxml/xmlmemory.h>
#include <libxml/parser.h>

#include "thread/thread.h"
#include "cfgfile.h"
#include "refbuf.h"
#include "client.h"
#include "logging.h" 

#define CATMODULE "CONFIG"
#define CONFIG_DEFAULT_LOCATION "Earth"
#define CONFIG_DEFAULT_ADMIN "icemaster@localhost"
#define CONFIG_DEFAULT_CLIENT_LIMIT 256
#define CONFIG_DEFAULT_SOURCE_LIMIT 16
#define CONFIG_DEFAULT_QUEUE_SIZE_LIMIT (100*1024)
#define CONFIG_DEFAULT_BURST_SIZE (64*1024)
#define CONFIG_DEFAULT_THREADPOOL_SIZE 4
#define CONFIG_DEFAULT_CLIENT_TIMEOUT 30
#define CONFIG_DEFAULT_HEADER_TIMEOUT 15
#define CONFIG_DEFAULT_SOURCE_TIMEOUT 10
#define CONFIG_DEFAULT_SOURCE_PASSWORD "changeme"
#define CONFIG_DEFAULT_RELAY_PASSWORD "changeme"
#define CONFIG_DEFAULT_MASTER_USERNAME "relay"
#define CONFIG_DEFAULT_SHOUTCAST_MOUNT "/stream"
#define CONFIG_DEFAULT_ICE_LOGIN 0
#define CONFIG_DEFAULT_FILESERVE 1
#define CONFIG_DEFAULT_TOUCH_FREQ 5
#define CONFIG_DEFAULT_HOSTNAME "localhost"
#define CONFIG_DEFAULT_PLAYLIST_LOG NULL
#define CONFIG_DEFAULT_ACCESS_LOG "access.log"
#define CONFIG_DEFAULT_ERROR_LOG "error.log"
#define CONFIG_DEFAULT_LOG_LEVEL 4
#define CONFIG_DEFAULT_CHROOT 0
#define CONFIG_DEFAULT_CHUID 0
#define CONFIG_DEFAULT_USER NULL
#define CONFIG_DEFAULT_GROUP NULL
#define CONFIG_MASTER_UPDATE_INTERVAL 120
#define CONFIG_YP_URL_TIMEOUT 10

#ifndef _WIN32
#define CONFIG_DEFAULT_BASE_DIR "/usr/local/icecast"
#define CONFIG_DEFAULT_LOG_DIR "/usr/local/icecast/logs"
#define CONFIG_DEFAULT_WEBROOT_DIR "/usr/local/icecast/webroot"
#define CONFIG_DEFAULT_ADMINROOT_DIR "/usr/local/icecast/admin"
#else
#define CONFIG_DEFAULT_BASE_DIR ".\\"
#define CONFIG_DEFAULT_LOG_DIR ".\\logs"
#define CONFIG_DEFAULT_WEBROOT_DIR ".\\webroot"
#define CONFIG_DEFAULT_ADMINROOT_DIR ".\\admin"
#endif

static ice_config_t _current_configuration;
static ice_config_locks _locks;

static void _set_defaults(ice_config_t *c);
static void _parse_root(xmlDocPtr doc, xmlNodePtr node, ice_config_t *c);
static void _parse_limits(xmlDocPtr doc, xmlNodePtr node, ice_config_t *c);
static void _parse_directory(xmlDocPtr doc, xmlNodePtr node, ice_config_t *c);
static void _parse_paths(xmlDocPtr doc, xmlNodePtr node, ice_config_t *c);
static void _parse_logging(xmlDocPtr doc, xmlNodePtr node, ice_config_t *c);
static void _parse_security(xmlDocPtr doc, xmlNodePtr node, ice_config_t *c);
static void _parse_authentication(xmlDocPtr doc, xmlNodePtr node, 
        ice_config_t *c);
static void _parse_relay(xmlDocPtr doc, xmlNodePtr node, ice_config_t *c);
static void _parse_mount(xmlDocPtr doc, xmlNodePtr node, ice_config_t *c);
static void _parse_listen_socket(xmlDocPtr doc, xmlNodePtr node, 
        ice_config_t *c);
static void _add_server(xmlDocPtr doc, xmlNodePtr node, ice_config_t *c);

static void create_locks() {
    thread_mutex_create(&_locks.relay_lock);
    thread_mutex_create(&_locks.mounts_lock);
    thread_rwlock_create(&_locks.config_lock);
}

static void release_locks() {
    thread_mutex_destroy(&_locks.relay_lock);
    thread_mutex_destroy(&_locks.mounts_lock);
    thread_rwlock_destroy(&_locks.config_lock);
}

void config_initialize(void) {
    create_locks();
}

void config_shutdown(void) {
    config_get_config();
    config_clear(&_current_configuration);
    config_release_config();
    release_locks();
}

void config_init_configuration(ice_config_t *configuration)
{
    memset(configuration, 0, sizeof(ice_config_t));
    _set_defaults(configuration);
}

void config_clear(ice_config_t *c)
{
    ice_config_dir_t *dirnode, *nextdirnode;
    relay_server *relay, *nextrelay;
    mount_proxy *mount, *nextmount;
    aliases *alias, *nextalias;
    int i;
    config_options_t *option;

    if (c->config_filename)
        free(c->config_filename);

    if (c->location && c->location != CONFIG_DEFAULT_LOCATION) 
        xmlFree(c->location);
    if (c->admin && c->admin != CONFIG_DEFAULT_ADMIN) 
        xmlFree(c->admin);
    if (c->source_password && c->source_password != CONFIG_DEFAULT_SOURCE_PASSWORD)
        xmlFree(c->source_password);
    if (c->admin_username)
        xmlFree(c->admin_username);
    if (c->admin_password)
        xmlFree(c->admin_password);
    if (c->relay_username)
        xmlFree(c->relay_username);
    if (c->relay_password)
        xmlFree(c->relay_password);
    if (c->hostname && c->hostname != CONFIG_DEFAULT_HOSTNAME) 
        xmlFree(c->hostname);
    if (c->base_dir && c->base_dir != CONFIG_DEFAULT_BASE_DIR) 
        xmlFree(c->base_dir);
    if (c->log_dir && c->log_dir != CONFIG_DEFAULT_LOG_DIR) 
        xmlFree(c->log_dir);
    if (c->webroot_dir && c->webroot_dir != CONFIG_DEFAULT_WEBROOT_DIR)
        xmlFree(c->webroot_dir);
    if (c->adminroot_dir && c->adminroot_dir != CONFIG_DEFAULT_ADMINROOT_DIR)
        xmlFree(c->adminroot_dir);
    if (c->pidfile)
        xmlFree(c->pidfile);
    if (c->playlist_log && c->playlist_log != CONFIG_DEFAULT_PLAYLIST_LOG) 
        xmlFree(c->playlist_log);
    if (c->access_log && c->access_log != CONFIG_DEFAULT_ACCESS_LOG) 
        xmlFree(c->access_log);
    if (c->error_log && c->error_log != CONFIG_DEFAULT_ERROR_LOG) 
        xmlFree(c->error_log);
    if (c->shoutcast_mount && c->shoutcast_mount != CONFIG_DEFAULT_SHOUTCAST_MOUNT)
        xmlFree(c->shoutcast_mount);
    for(i=0; i < MAX_LISTEN_SOCKETS; i++) {
        if (c->listeners[i].bind_address) xmlFree(c->listeners[i].bind_address);
    }
    if (c->master_server) xmlFree(c->master_server);
    if (c->master_username) xmlFree(c->master_username);
    if (c->master_password) xmlFree(c->master_password);
    if (c->user) xmlFree(c->user);
    if (c->group) xmlFree(c->group);

    thread_mutex_lock(&(_locks.relay_lock));
    relay = c->relay;
    while(relay) {
        nextrelay = relay->next;
        xmlFree(relay->server);
        xmlFree(relay->mount);
        xmlFree(relay->localmount);
        free(relay);
        relay = nextrelay;
    }
    thread_mutex_unlock(&(_locks.relay_lock));

    thread_mutex_lock(&(_locks.mounts_lock));
    mount = c->mounts;
    while(mount) {
        nextmount = mount->next;
        xmlFree(mount->mountname);
        xmlFree(mount->username);
        xmlFree(mount->password);
        xmlFree(mount->dumpfile);
        xmlFree(mount->intro_filename);
        xmlFree(mount->fallback_mount);
        xmlFree(mount->stream_name);
        xmlFree(mount->stream_description);
        xmlFree(mount->stream_url);
        xmlFree(mount->stream_genre);
        xmlFree(mount->bitrate);
        xmlFree(mount->type);
        if (mount->cluster_password) {
            xmlFree(mount->cluster_password);
        }

        xmlFree(mount->auth_type);
        option = mount->auth_options;
        while(option) {
            config_options_t *nextopt = option->next;
            xmlFree(option->name);
            xmlFree(option->value);
            free(option);
            option = nextopt;
        }

        free(mount);
        mount = nextmount;
    }
    thread_mutex_unlock(&(_locks.mounts_lock));

    alias = c->aliases;
    while(alias) {
        nextalias = alias->next;
        xmlFree(alias->source);
        xmlFree(alias->destination);
        xmlFree(alias->bind_address);
        free(alias);
        alias = nextalias;
    }

    dirnode = c->dir_list;
    while(dirnode) {
        nextdirnode = dirnode->next;
        xmlFree(dirnode->host);
        free(dirnode);
        dirnode = nextdirnode;
    }
#ifdef USE_YP
    i = 0;
    while (i < c->num_yp_directories)
    {
        xmlFree (c->yp_url[i]);
        i++;
    }
#endif

    memset(c, 0, sizeof(ice_config_t));
}

int config_initial_parse_file(const char *filename)
{
    /* Since we're already pointing at it, we don't need to copy it in place */
    return config_parse_file(filename, &_current_configuration);
}

int config_parse_file(const char *filename, ice_config_t *configuration)
{
    xmlDocPtr doc;
    xmlNodePtr node;

    if (filename == NULL || strcmp(filename, "") == 0) return CONFIG_EINSANE;
    
    xmlInitParser();
    doc = xmlParseFile(filename);
    if (doc == NULL) {
        return CONFIG_EPARSE;
    }

    node = xmlDocGetRootElement(doc);
    if (node == NULL) {
        xmlFreeDoc(doc);
        xmlCleanupParser();
        return CONFIG_ENOROOT;
    }

    if (strcmp(node->name, "icecast") != 0) {
        xmlFreeDoc(doc);
        xmlCleanupParser();
        return CONFIG_EBADROOT;
    }

    config_init_configuration(configuration);

    configuration->config_filename = (char *)strdup(filename);

    _parse_root(doc, node->xmlChildrenNode, configuration);

    xmlFreeDoc(doc);

    return 0;
}

int config_parse_cmdline(int arg, char **argv)
{
    return 0;
}

ice_config_locks *config_locks(void)
{
    return &_locks;
}

void config_release_config(void)
{
    thread_rwlock_unlock(&(_locks.config_lock));
}

ice_config_t *config_get_config(void)
{
    thread_rwlock_rlock(&(_locks.config_lock));
    return &_current_configuration;
}

ice_config_t *config_grab_config(void)
{
    thread_rwlock_wlock(&(_locks.config_lock));
    return &_current_configuration;
}

/* MUST be called with the lock held! */
void config_set_config(ice_config_t *config) {
    memcpy(&_current_configuration, config, sizeof(ice_config_t));
}

ice_config_t *config_get_config_unlocked(void)
{
    return &_current_configuration;
}

static void _set_defaults(ice_config_t *configuration)
{
    configuration->location = CONFIG_DEFAULT_LOCATION;
    configuration->admin = CONFIG_DEFAULT_ADMIN;
    configuration->client_limit = CONFIG_DEFAULT_CLIENT_LIMIT;
    configuration->source_limit = CONFIG_DEFAULT_SOURCE_LIMIT;
    configuration->queue_size_limit = CONFIG_DEFAULT_QUEUE_SIZE_LIMIT;
    configuration->threadpool_size = CONFIG_DEFAULT_THREADPOOL_SIZE;
    configuration->client_timeout = CONFIG_DEFAULT_CLIENT_TIMEOUT;
    configuration->header_timeout = CONFIG_DEFAULT_HEADER_TIMEOUT;
    configuration->source_timeout = CONFIG_DEFAULT_SOURCE_TIMEOUT;
    configuration->source_password = CONFIG_DEFAULT_SOURCE_PASSWORD;
    configuration->shoutcast_mount = CONFIG_DEFAULT_SHOUTCAST_MOUNT;
    configuration->ice_login = CONFIG_DEFAULT_ICE_LOGIN;
    configuration->fileserve = CONFIG_DEFAULT_FILESERVE;
    configuration->touch_interval = CONFIG_DEFAULT_TOUCH_FREQ;
    configuration->on_demand = 0;
    configuration->dir_list = NULL;
    configuration->hostname = CONFIG_DEFAULT_HOSTNAME;
    configuration->port = 0;
    configuration->listeners[0].port = 0;
    configuration->listeners[0].bind_address = NULL;
    configuration->listeners[0].shoutcast_compat = 0;
    configuration->master_server = NULL;
    configuration->master_server_port = 0;
    configuration->master_update_interval = CONFIG_MASTER_UPDATE_INTERVAL;
    configuration->master_username = xmlStrdup (CONFIG_DEFAULT_MASTER_USERNAME);
    configuration->master_password = NULL;
    configuration->base_dir = CONFIG_DEFAULT_BASE_DIR;
    configuration->log_dir = CONFIG_DEFAULT_LOG_DIR;
    configuration->webroot_dir = CONFIG_DEFAULT_WEBROOT_DIR;
    configuration->adminroot_dir = CONFIG_DEFAULT_ADMINROOT_DIR;
    configuration->playlist_log = CONFIG_DEFAULT_PLAYLIST_LOG;
    configuration->access_log = CONFIG_DEFAULT_ACCESS_LOG;
    configuration->error_log = CONFIG_DEFAULT_ERROR_LOG;
    configuration->loglevel = CONFIG_DEFAULT_LOG_LEVEL;
    configuration->chroot = CONFIG_DEFAULT_CHROOT;
    configuration->chuid = CONFIG_DEFAULT_CHUID;
    configuration->user = CONFIG_DEFAULT_USER;
    configuration->group = CONFIG_DEFAULT_GROUP;
    configuration->num_yp_directories = 0;
    configuration->relay_username = xmlStrdup (CONFIG_DEFAULT_MASTER_USERNAME);
    configuration->relay_password = NULL;
    /* default to a typical prebuffer size used by clients */
    configuration->burst_size = CONFIG_DEFAULT_BURST_SIZE;
}

static void _parse_root(xmlDocPtr doc, xmlNodePtr node, 
        ice_config_t *configuration)
{
    char *tmp;

    do {
        if (node == NULL) break;
        if (xmlIsBlankNode(node)) continue;

        if (strcmp(node->name, "location") == 0) {
            if (configuration->location && configuration->location != CONFIG_DEFAULT_LOCATION) xmlFree(configuration->location);
            configuration->location = (char *)xmlNodeListGetString(doc, node->xmlChildrenNode, 1);
        } else if (strcmp(node->name, "admin") == 0) {
            if (configuration->admin && configuration->admin != CONFIG_DEFAULT_ADMIN) xmlFree(configuration->admin);
            configuration->admin = (char *)xmlNodeListGetString(doc, node->xmlChildrenNode, 1);
        } else if(strcmp(node->name, "authentication") == 0) {
            _parse_authentication(doc, node->xmlChildrenNode, configuration);
        } else if (strcmp(node->name, "source-password") == 0) {
            /* TODO: This is the backwards-compatibility location */
            char *mount, *pass;
            if ((mount = (char *)xmlGetProp(node, "mount")) != NULL) {
                pass = (char *)xmlNodeListGetString(doc, node->xmlChildrenNode, 1);
                /* FIXME: This is a placeholder for per-mount passwords */
            }
            else {
                if (configuration->source_password && configuration->source_password != CONFIG_DEFAULT_SOURCE_PASSWORD) xmlFree(configuration->source_password);
                configuration->source_password = (char *)xmlNodeListGetString(doc, node->xmlChildrenNode, 1);
            }
        } else if (strcmp(node->name, "icelogin") == 0) {
            tmp = (char *)xmlNodeListGetString(doc, node->xmlChildrenNode, 1);
            configuration->ice_login = atoi(tmp);
            if (tmp) xmlFree(tmp);
        } else if (strcmp(node->name, "fileserve") == 0) {
            tmp = (char *)xmlNodeListGetString(doc, node->xmlChildrenNode, 1);
            configuration->fileserve = atoi(tmp);
            if (tmp) xmlFree(tmp);
        } else if (strcmp(node->name, "relays-on-demand") == 0) {
            tmp = (char *)xmlNodeListGetString(doc, node->xmlChildrenNode, 1);
            configuration->on_demand = atoi(tmp);
            if (tmp) xmlFree(tmp);
        } else if (strcmp(node->name, "hostname") == 0) {
            if (configuration->hostname && configuration->hostname != CONFIG_DEFAULT_HOSTNAME) xmlFree(configuration->hostname);
            configuration->hostname = (char *)xmlNodeListGetString(doc, node->xmlChildrenNode, 1);
        } else if (strcmp(node->name, "listen-socket") == 0) {
            _parse_listen_socket(doc, node->xmlChildrenNode, configuration);
        } else if (strcmp(node->name, "port") == 0) {
            tmp = (char *)xmlNodeListGetString(doc, node->xmlChildrenNode, 1);
            configuration->port = atoi(tmp);
            configuration->listeners[0].port = atoi(tmp);
            if (tmp) xmlFree(tmp);
        } else if (strcmp(node->name, "bind-address") == 0) {
            if (configuration->listeners[0].bind_address) 
                xmlFree(configuration->listeners[0].bind_address);
            configuration->listeners[0].bind_address = (char *)xmlNodeListGetString(doc, node->xmlChildrenNode, 1);
        } else if (strcmp(node->name, "master-server") == 0) {
            if (configuration->master_server) xmlFree(configuration->master_server);
            configuration->master_server = (char *)xmlNodeListGetString(doc, node->xmlChildrenNode, 1);
        } else if (strcmp(node->name, "master-username") == 0) {
            if (configuration->master_username) xmlFree(configuration->master_username);
            configuration->master_username = (char *)xmlNodeListGetString(doc, node->xmlChildrenNode, 1);
        } else if (strcmp(node->name, "master-password") == 0) {
            if (configuration->master_password) xmlFree(configuration->master_password);
            configuration->master_password = (char *)xmlNodeListGetString(doc, node->xmlChildrenNode, 1);
        } else if (strcmp(node->name, "master-server-port") == 0) {
            tmp = (char *)xmlNodeListGetString(doc, node->xmlChildrenNode, 1);
            configuration->master_server_port = atoi(tmp);
            xmlFree (tmp);
        } else if (strcmp(node->name, "master-update-interval") == 0) {
            tmp = (char *)xmlNodeListGetString(doc, node->xmlChildrenNode, 1);
            configuration->master_update_interval = atoi(tmp);
            xmlFree (tmp);
        } else if (strcmp(node->name, "shoutcast-mount") == 0) {
            if (configuration->shoutcast_mount &&
                    configuration->shoutcast_mount != CONFIG_DEFAULT_SHOUTCAST_MOUNT)
                xmlFree(configuration->shoutcast_mount);
            configuration->shoutcast_mount = (char *)xmlNodeListGetString(doc, node->xmlChildrenNode, 1);
        } else if (strcmp(node->name, "limits") == 0) {
            _parse_limits(doc, node->xmlChildrenNode, configuration);
        } else if (strcmp(node->name, "relay") == 0) {
            _parse_relay(doc, node->xmlChildrenNode, configuration);
        } else if (strcmp(node->name, "mount") == 0) {
            _parse_mount(doc, node->xmlChildrenNode, configuration);
        } else if (strcmp(node->name, "directory") == 0) {
            _parse_directory(doc, node->xmlChildrenNode, configuration);
        } else if (strcmp(node->name, "paths") == 0) {
            _parse_paths(doc, node->xmlChildrenNode, configuration);
        } else if (strcmp(node->name, "logging") == 0) {
            _parse_logging(doc, node->xmlChildrenNode, configuration);
        } else if (strcmp(node->name, "security") == 0) {
            _parse_security(doc, node->xmlChildrenNode, configuration);
        }
    } while ((node = node->next));
}

static void _parse_limits(xmlDocPtr doc, xmlNodePtr node, 
        ice_config_t *configuration)
{
    char *tmp;

    do {
        if (node == NULL) break;
        if (xmlIsBlankNode(node)) continue;

        if (strcmp(node->name, "clients") == 0) {
            tmp = (char *)xmlNodeListGetString(doc, node->xmlChildrenNode, 1);
            configuration->client_limit = atoi(tmp);
            if (tmp) xmlFree(tmp);
        } else if (strcmp(node->name, "sources") == 0) {
            tmp = (char *)xmlNodeListGetString(doc, node->xmlChildrenNode, 1);
            configuration->source_limit = atoi(tmp);
            if (tmp) xmlFree(tmp);
        } else if (strcmp(node->name, "queue-size") == 0) {
            tmp = (char *)xmlNodeListGetString(doc, node->xmlChildrenNode, 1);
            configuration->queue_size_limit = atoi(tmp);
            if (tmp) xmlFree(tmp);
        } else if (strcmp(node->name, "threadpool") == 0) {
            tmp = (char *)xmlNodeListGetString(doc, node->xmlChildrenNode, 1);
            configuration->threadpool_size = atoi(tmp);
            if (tmp) xmlFree(tmp);
        } else if (strcmp(node->name, "client-timeout") == 0) {
            tmp = (char *)xmlNodeListGetString(doc, node->xmlChildrenNode, 1);
            configuration->client_timeout = atoi(tmp);
            if (tmp) xmlFree(tmp);
        } else if (strcmp(node->name, "header-timeout") == 0) {
            tmp = (char *)xmlNodeListGetString(doc, node->xmlChildrenNode, 1);
            configuration->header_timeout = atoi(tmp);
            if (tmp) xmlFree(tmp);
        } else if (strcmp(node->name, "source-timeout") == 0) {
            tmp = (char *)xmlNodeListGetString(doc, node->xmlChildrenNode, 1);
            configuration->source_timeout = atoi(tmp);
            if (tmp) xmlFree(tmp);
        } else if (strcmp(node->name, "burst-on-connect") == 0) {
            tmp = (char *)xmlNodeListGetString(doc, node->xmlChildrenNode, 1);
            if (atoi(tmp) == 0)
                configuration->burst_size = 0;
            if (tmp) xmlFree(tmp);
        } else if (strcmp(node->name, "burst-size") == 0) {
            tmp = (char *)xmlNodeListGetString(doc, node->xmlChildrenNode, 1);
            configuration->burst_size = atoi(tmp);
            if (tmp) xmlFree(tmp);
        }
    } while ((node = node->next));
}

static void _parse_mount(xmlDocPtr doc, xmlNodePtr node, 
        ice_config_t *configuration)
{
    char *tmp;
    mount_proxy *mount = calloc(1, sizeof(mount_proxy));
    mount_proxy *current = configuration->mounts;
    mount_proxy *last=NULL;
    xmlNodePtr option;
    config_options_t *last_option;
    
    while(current) {
        last = current;
        current = current->next;
    }

    if(last)
        last->next = mount;
    else
        configuration->mounts = mount;

    /* default <mount> settings */
    mount->max_listeners = -1;
    mount->burst_size = -1;
    mount->mp3_meta_interval = -1;
    mount->yp_public = -1;
    mount->next = NULL;

    do {
        if (node == NULL) break;
        if (xmlIsBlankNode(node)) continue;

        if (strcmp(node->name, "mount-name") == 0) {
            mount->mountname = (char *)xmlNodeListGetString(
                    doc, node->xmlChildrenNode, 1);
        }
        else if (strcmp(node->name, "username") == 0) {
            mount->username = (char *)xmlNodeListGetString(
                    doc, node->xmlChildrenNode, 1);
        }
        else if (strcmp(node->name, "password") == 0) {
            mount->password = (char *)xmlNodeListGetString(
                    doc, node->xmlChildrenNode, 1);
        }
        else if (strcmp(node->name, "dump-file") == 0) {
            mount->dumpfile = (char *)xmlNodeListGetString(
                    doc, node->xmlChildrenNode, 1);
        }
        else if (strcmp(node->name, "intro") == 0) {
            mount->intro_filename = (char *)xmlNodeListGetString(
                    doc, node->xmlChildrenNode, 1);
        }
        else if (strcmp(node->name, "fallback-mount") == 0) {
            mount->fallback_mount = (char *)xmlNodeListGetString(
                    doc, node->xmlChildrenNode, 1);
        }
        else if (strcmp(node->name, "max-listeners") == 0) {
            tmp = (char *)xmlNodeListGetString(doc, node->xmlChildrenNode, 1);
            mount->max_listeners = atoi(tmp);
            if(tmp) xmlFree(tmp);
        }
        else if (strcmp(node->name, "mp3-metadata-interval") == 0) {
            tmp = (char *)xmlNodeListGetString(doc, node->xmlChildrenNode, 1);
            mount->mp3_meta_interval = atoi(tmp);
            if(tmp) xmlFree(tmp);
        }
        else if (strcmp(node->name, "fallback-override") == 0) {
            tmp = (char *)xmlNodeListGetString(doc, node->xmlChildrenNode, 1);
            mount->fallback_override = atoi(tmp);
            if(tmp) xmlFree(tmp);
        }
        else if (strcmp(node->name, "no-mount") == 0) {
            tmp = (char *)xmlNodeListGetString(doc, node->xmlChildrenNode, 1);
            mount->no_mount = atoi(tmp);
            if(tmp) xmlFree(tmp);
        }
        else if (strcmp(node->name, "no-yp") == 0) {
            tmp = (char *)xmlNodeListGetString(doc, node->xmlChildrenNode, 1);
            mount->yp_public = atoi(tmp) == 0 ? -1 : 0;
            if(tmp) xmlFree(tmp);
        }
        else if (strcmp(node->name, "hidden") == 0) {
            tmp = (char *)xmlNodeListGetString(doc, node->xmlChildrenNode, 1);
            mount->hidden = atoi(tmp);
            if(tmp) xmlFree(tmp);
        }
        else if (strcmp(node->name, "authentication") == 0) {
            mount->auth_type = xmlGetProp(node, "type");
            option = node->xmlChildrenNode;
            last_option = NULL;
            while(option != NULL) {
                if(strcmp(option->name, "option") == 0) {
                    config_options_t *opt = malloc(sizeof(config_options_t));
                    opt->name = xmlGetProp(option, "name");
                    if(!opt->name) {
                        free(opt);
                        option = option->next;
                        continue;
                    }
                    opt->value = xmlGetProp(option, "value");
                    if(!opt->value) {
                        free(opt->name);
                        free(opt);
                        option = option->next;
                        continue;
                    }
                    opt->next = NULL;

                    if(last_option)
                        last_option->next = opt;
                    else
                        mount->auth_options = opt;
                    last_option = opt;
                }
                option = option->next;
            }
        }
        else if (strcmp(node->name, "queue-size") == 0) {
            tmp = (char *)xmlNodeListGetString(doc, node->xmlChildrenNode, 1);
            mount->queue_size_limit = atoi (tmp);
            if(tmp) xmlFree(tmp);
        }
        else if (strcmp(node->name, "source-timeout") == 0) {
            tmp = (char *)xmlNodeListGetString(doc, node->xmlChildrenNode, 1);
            if (tmp)
            {
                mount->source_timeout = atoi (tmp);
                xmlFree(tmp);
            }
        } else if (strcmp(node->name, "burst-size") == 0) {
            tmp = (char *)xmlNodeListGetString(doc, node->xmlChildrenNode, 1);
            mount->burst_size = atoi(tmp);
            if (tmp) xmlFree(tmp);
        } else if (strcmp(node->name, "cluster-password") == 0) {
            mount->cluster_password = (char *)xmlNodeListGetString(
                    doc, node->xmlChildrenNode, 1);
        } else if (strcmp(node->name, "stream-name") == 0) {
            mount->stream_name = (char *)xmlNodeListGetString(
                    doc, node->xmlChildrenNode, 1);
        } else if (strcmp(node->name, "stream-description") == 0) {
            mount->stream_description = (char *)xmlNodeListGetString(
                    doc, node->xmlChildrenNode, 1);
        } else if (strcmp(node->name, "stream-url") == 0) {
            mount->stream_url = (char *)xmlNodeListGetString(
                    doc, node->xmlChildrenNode, 1);
        } else if (strcmp(node->name, "genre") == 0) {
            mount->stream_genre = (char *)xmlNodeListGetString(
                    doc, node->xmlChildrenNode, 1);
        } else if (strcmp(node->name, "bitrate") == 0) {
            mount->bitrate = (char *)xmlNodeListGetString(
                    doc, node->xmlChildrenNode, 1);
        } else if (strcmp(node->name, "public") == 0) {
            tmp = (char *)xmlNodeListGetString(doc, node->xmlChildrenNode, 1);
            mount->yp_public = atoi (tmp);
            if(tmp) xmlFree(tmp);
        } else if (strcmp(node->name, "type") == 0) {
            mount->type = (char *)xmlNodeListGetString(
                    doc, node->xmlChildrenNode, 1);
        } else if (strcmp(node->name, "subtype") == 0) {
            mount->subtype = (char *)xmlNodeListGetString(
                    doc, node->xmlChildrenNode, 1);
        }
    } while ((node = node->next));
}


static void _parse_relay(xmlDocPtr doc, xmlNodePtr node,
        ice_config_t *configuration)
{
    char *tmp;
    relay_server *relay = calloc(1, sizeof(relay_server));
    relay_server *current = configuration->relay;
    relay_server *last=NULL;

    while(current) {
        last = current;
        current = current->next;
    }

    if(last)
        last->next = relay;
    else
        configuration->relay = relay;

    relay->next = NULL;
    relay->mp3metadata = 1;
    relay->on_demand = configuration->on_demand;

    do {
        if (node == NULL) break;
        if (xmlIsBlankNode(node)) continue;

        if (strcmp(node->name, "server") == 0) {
            relay->server = (char *)xmlNodeListGetString(
                    doc, node->xmlChildrenNode, 1);
        }
        else if (strcmp(node->name, "port") == 0) {
            tmp = (char *)xmlNodeListGetString(doc, node->xmlChildrenNode, 1);
            relay->port = atoi(tmp);
            if(tmp) xmlFree(tmp);
        }
        else if (strcmp(node->name, "mount") == 0) {
            relay->mount = (char *)xmlNodeListGetString(
                    doc, node->xmlChildrenNode, 1);
        }
        else if (strcmp(node->name, "local-mount") == 0) {
            relay->localmount = (char *)xmlNodeListGetString(
                    doc, node->xmlChildrenNode, 1);
        }
        else if (strcmp(node->name, "relay-shoutcast-metadata") == 0) {
            tmp = (char *)xmlNodeListGetString(doc, node->xmlChildrenNode, 1);
            relay->mp3metadata = atoi(tmp);
            if(tmp) xmlFree(tmp);
        }
        else if (strcmp(node->name, "username") == 0) {
            relay->username = (char *)xmlNodeListGetString(doc,
                    node->xmlChildrenNode, 1);
        }
        else if (strcmp(node->name, "password") == 0) {
            relay->password = (char *)xmlNodeListGetString(doc,
                    node->xmlChildrenNode, 1);
        }
        else if (strcmp(node->name, "on-demand") == 0) {
            tmp = (char *)xmlNodeListGetString(doc, node->xmlChildrenNode, 1);
            relay->on_demand = atoi(tmp);
            if (tmp) xmlFree(tmp);
        }
    } while ((node = node->next));
    if (relay->localmount == NULL)
        relay->localmount = xmlStrdup (relay->mount);
}

static void _parse_listen_socket(xmlDocPtr doc, xmlNodePtr node,
        ice_config_t *configuration)
{
    listener_t *listener = NULL;
    int i;
    char *tmp;

    for(i=0; i < MAX_LISTEN_SOCKETS; i++) {
        if(configuration->listeners[i].port <= 0) {
            listener = &(configuration->listeners[i]);
            break;
        }
    }

    do {
        if (node == NULL) break;
        if (xmlIsBlankNode(node)) continue;

        if (strcmp(node->name, "port") == 0) {
            tmp = (char *)xmlNodeListGetString(doc, node->xmlChildrenNode, 1);
            if(configuration->port == 0)
                configuration->port = atoi(tmp);
            listener->port = atoi(tmp);
            if(tmp) xmlFree(tmp);
        }
        else if (strcmp(node->name, "shoutcast-compat") == 0) {
            tmp = (char *)xmlNodeListGetString(doc, node->xmlChildrenNode, 1);
            listener->shoutcast_compat = atoi(tmp);
            if(tmp) xmlFree(tmp);
        }
        else if (strcmp(node->name, "bind-address") == 0) {
            listener->bind_address = (char *)xmlNodeListGetString(doc, 
                    node->xmlChildrenNode, 1);
        }
    } while ((node = node->next));
}

static void _parse_authentication(xmlDocPtr doc, xmlNodePtr node,
        ice_config_t *configuration)
{
    do {
        if (node == NULL) break;
        if (xmlIsBlankNode(node)) continue;

        if (strcmp(node->name, "source-password") == 0) {
            char *mount, *pass;
            if ((mount = (char *)xmlGetProp(node, "mount")) != NULL) {
                pass = (char *)xmlNodeListGetString(doc, node->xmlChildrenNode, 1);
                /* FIXME: This is a placeholder for per-mount passwords */
            }
            else {
                if (configuration->source_password && 
                        configuration->source_password != 
                        CONFIG_DEFAULT_SOURCE_PASSWORD) 
                    xmlFree(configuration->source_password);
                configuration->source_password = 
                    (char *)xmlNodeListGetString(doc, node->xmlChildrenNode, 1);
            }
        } else if (strcmp(node->name, "admin-password") == 0) {
            if(configuration->admin_password)
                xmlFree(configuration->admin_password);
            configuration->admin_password =
                (char *)xmlNodeListGetString(doc, node->xmlChildrenNode, 1);
        } else if (strcmp(node->name, "admin-user") == 0) {
            if(configuration->admin_username)
                xmlFree(configuration->admin_username);
            configuration->admin_username =
                (char *)xmlNodeListGetString(doc, node->xmlChildrenNode, 1);
        } else if (strcmp(node->name, "relay-password") == 0) {
            if(configuration->relay_password)
                xmlFree(configuration->relay_password);
            configuration->relay_password =
                (char *)xmlNodeListGetString(doc, node->xmlChildrenNode, 1);
        } else if (strcmp(node->name, "relay-user") == 0) {
            if(configuration->relay_username)
                xmlFree(configuration->relay_username);
            configuration->relay_username =
                (char *)xmlNodeListGetString(doc, node->xmlChildrenNode, 1);
        }
    } while ((node = node->next));
}

static void _parse_directory(xmlDocPtr doc, xmlNodePtr node,
        ice_config_t *configuration)
{
    char *tmp;

    if (configuration->num_yp_directories >= MAX_YP_DIRECTORIES) {
        ERROR0("Maximum number of yp directories exceeded!");
        return;
    }
    do {
        if (node == NULL) break;
        if (xmlIsBlankNode(node)) continue;

        if (strcmp(node->name, "yp-url") == 0) {
            if (configuration->yp_url[configuration->num_yp_directories]) 
                xmlFree(configuration->yp_url[configuration->num_yp_directories]);
            configuration->yp_url[configuration->num_yp_directories] = 
                (char *)xmlNodeListGetString(doc, node->xmlChildrenNode, 1);
        } else if (strcmp(node->name, "yp-url-timeout") == 0) {
            tmp = (char *)xmlNodeListGetString(doc, node->xmlChildrenNode, 1);
            configuration->yp_url_timeout[configuration->num_yp_directories] = 
                atoi(tmp);
            if (tmp) xmlFree(tmp);
        } else if (strcmp(node->name, "server") == 0) {
            _add_server(doc, node->xmlChildrenNode, configuration);
        } else if (strcmp(node->name, "touch-interval") == 0) {
            tmp = (char *)xmlNodeListGetString(doc, node->xmlChildrenNode, 1);
            configuration->yp_touch_interval[configuration->num_yp_directories] =
                atoi(tmp);
            if (tmp) xmlFree(tmp);
        }
    } while ((node = node->next));
    configuration->num_yp_directories++;
}

static void _parse_paths(xmlDocPtr doc, xmlNodePtr node,
        ice_config_t *configuration)
{
    char *temp;
    aliases *alias, *current, *last;

    do {
        if (node == NULL) break;
        if (xmlIsBlankNode(node)) continue;

        if (strcmp(node->name, "basedir") == 0) {
            if (configuration->base_dir && configuration->base_dir != CONFIG_DEFAULT_BASE_DIR) xmlFree(configuration->base_dir);
            configuration->base_dir = (char *)xmlNodeListGetString(doc, node->xmlChildrenNode, 1);
        } else if (strcmp(node->name, "logdir") == 0) {
            if (configuration->log_dir && configuration->log_dir != CONFIG_DEFAULT_LOG_DIR) xmlFree(configuration->log_dir);
            configuration->log_dir = (char *)xmlNodeListGetString(doc, node->xmlChildrenNode, 1);
        } else if (strcmp(node->name, "pidfile") == 0) {
            if (configuration->pidfile) xmlFree(configuration->pidfile);
            configuration->pidfile = (char *)xmlNodeListGetString(doc, node->xmlChildrenNode, 1);
        } else if (strcmp(node->name, "webroot") == 0) {
            if (configuration->webroot_dir && configuration->webroot_dir != CONFIG_DEFAULT_WEBROOT_DIR) xmlFree(configuration->webroot_dir);
            configuration->webroot_dir = (char *)xmlNodeListGetString(doc, node->xmlChildrenNode, 1);
            if(configuration->webroot_dir[strlen(configuration->webroot_dir)-1] == '/')
                configuration->webroot_dir[strlen(configuration->webroot_dir)-1] = 0;
        } else if (strcmp(node->name, "adminroot") == 0) {
            if (configuration->adminroot_dir && configuration->adminroot_dir != CONFIG_DEFAULT_ADMINROOT_DIR) 
                xmlFree(configuration->adminroot_dir);
            configuration->adminroot_dir = (char *)xmlNodeListGetString(doc, node->xmlChildrenNode, 1);
            if(configuration->adminroot_dir[strlen(configuration->adminroot_dir)-1] == '/')
                configuration->adminroot_dir[strlen(configuration->adminroot_dir)-1] = 0;
        } else if (strcmp(node->name, "alias") == 0) {
            alias = malloc(sizeof(aliases));
            alias->next = NULL;
            alias->source = xmlGetProp(node, "source");
            if(alias->source == NULL) {
                free(alias);
                continue;
            }
            alias->destination = xmlGetProp(node, "dest");
            if(alias->destination == NULL) {
                xmlFree(alias->source);
                free(alias);
                continue;
            }
            temp = NULL;
            temp = xmlGetProp(node, "port");
            if(temp != NULL) {
                alias->port = atoi(temp);
                xmlFree(temp);
            }
            else
                alias->port = -1;
            alias->bind_address = xmlGetProp(node, "bind-address");
            current = configuration->aliases;
            last = NULL;
            while(current) {
                last = current;
                current = current->next;
            }
            if(last)
                last->next = alias;
            else
                configuration->aliases = alias;
        }
    } while ((node = node->next));
}

static void _parse_logging(xmlDocPtr doc, xmlNodePtr node,
        ice_config_t *configuration)
{
    do {
        if (node == NULL) break;
        if (xmlIsBlankNode(node)) continue;

        if (strcmp(node->name, "accesslog") == 0) {
            if (configuration->access_log && configuration->access_log != CONFIG_DEFAULT_ACCESS_LOG) xmlFree(configuration->access_log);
            configuration->access_log = (char *)xmlNodeListGetString(doc, node->xmlChildrenNode, 1);
        } else if (strcmp(node->name, "errorlog") == 0) {
            if (configuration->error_log && configuration->error_log != CONFIG_DEFAULT_ERROR_LOG) xmlFree(configuration->error_log);
            configuration->error_log = (char *)xmlNodeListGetString(doc, node->xmlChildrenNode, 1);
        } else if (strcmp(node->name, "playlistlog") == 0) {
            if (configuration->playlist_log && configuration->playlist_log != CONFIG_DEFAULT_PLAYLIST_LOG) xmlFree(configuration->playlist_log);
            configuration->playlist_log = (char *)xmlNodeListGetString(doc, node->xmlChildrenNode, 1);
        } else if (strcmp(node->name, "loglevel") == 0) {
           char *tmp = (char *)xmlNodeListGetString(doc, node->xmlChildrenNode, 1);
           configuration->loglevel = atoi(tmp);
           if (tmp) xmlFree(tmp);
        }
    } while ((node = node->next));
}

static void _parse_security(xmlDocPtr doc, xmlNodePtr node,
        ice_config_t *configuration)
{
   char *tmp;
   xmlNodePtr oldnode;

   do {
       if (node == NULL) break;
       if (xmlIsBlankNode(node)) continue;

       if (strcmp(node->name, "chroot") == 0) {
           tmp = (char *)xmlNodeListGetString(doc, node->xmlChildrenNode, 1);
           configuration->chroot = atoi(tmp);
           if (tmp) xmlFree(tmp);
       } else if (strcmp(node->name, "changeowner") == 0) {
           configuration->chuid = 1;
           oldnode = node;
           node = node->xmlChildrenNode;
           do {
               if(node == NULL) break;
               if(xmlIsBlankNode(node)) continue;
               if(strcmp(node->name, "user") == 0) {
                   if(configuration->user) xmlFree(configuration->user);
                   configuration->user = (char *)xmlNodeListGetString(doc, node->xmlChildrenNode, 1);
               } else if(strcmp(node->name, "group") == 0) {
                   if(configuration->group) xmlFree(configuration->group);
                   configuration->group = (char *)xmlNodeListGetString(doc, node->xmlChildrenNode, 1);
               }
           } while((node = node->next));
           node = oldnode;
       }
   } while ((node = node->next));
}

static void _add_server(xmlDocPtr doc, xmlNodePtr node, 
        ice_config_t *configuration)
{
    ice_config_dir_t *dirnode, *server;
    int addnode;
    char *tmp;

    server = (ice_config_dir_t *)malloc(sizeof(ice_config_dir_t));
    server->touch_interval = configuration->touch_interval;
    server->host = NULL;
    addnode = 0;
    
    do {
        if (node == NULL) break;
        if (xmlIsBlankNode(node)) continue;

        if (strcmp(node->name, "host") == 0) {
            server->host = (char *)xmlNodeListGetString(doc, 
                    node->xmlChildrenNode, 1);
            addnode = 1;
        } else if (strcmp(node->name, "touch-interval") == 0) {
            tmp = (char *)xmlNodeListGetString(doc, node->xmlChildrenNode, 1);
            server->touch_interval = atoi(tmp);
            if (tmp) xmlFree(tmp);
        }
        server->next = NULL;
    } while ((node = node->next));

    if (addnode) {
        dirnode = configuration->dir_list;
        if (dirnode == NULL) {
            configuration->dir_list = server;
        } else {
            while (dirnode->next) dirnode = dirnode->next;
            
            dirnode->next = server;
        }
        
        server = NULL;
        addnode = 0;
    }
    
}


/* return the mount details that match the supplied mountpoint */
mount_proxy *config_find_mount (ice_config_t *config, const char *mount)
{
    mount_proxy *mountinfo = config->mounts;

    while (mountinfo)
    {
        if (strcmp (mountinfo->mountname, mount) == 0)
            break;
        mountinfo = mountinfo->next;
    }
    return mountinfo;
}

