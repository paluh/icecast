/* slave.c
 * by Ciaran Anscomb <ciaran.anscomb@6809.org.uk>
 *
 * Periodically requests a list of streams from a master server
 * and creates source threads for any it doesn't already have.
 * */

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

#include "thread.h"
#include "avl.h"
#include "sock.h"
#include "log.h"
#include "httpp.h"

#include "config.h"
#include "global.h"
#include "util.h"
#include "connection.h"
#include "refbuf.h"
#include "client.h"
#include "stats.h"
#include "logging.h"
#include "geturl.h"
#include "source.h"
#include "format.h"

#define CATMODULE "slave"

static void *_slave_thread(void *arg);
thread_type *_slave_thread_id;
static int _initialized = 0;

void slave_initialize(void) {
	if (_initialized) return;
    /* Don't create a slave thread if it isn't configured */
    if (config_get_config()->master_server == NULL && 
            config_get_config()->relay == NULL)
        return;

	_initialized = 1;
	_slave_thread_id = thread_create("Slave Thread", _slave_thread, NULL, THREAD_ATTACHED);
}

void slave_shutdown(void) {
	if (!_initialized) return;
	_initialized = 0;
	thread_join(_slave_thread_id);
}

static void create_relay_stream(char *server, int port, char *mount)
{
    sock_t streamsock;
	char header[4096];
	connection_t *con;
	http_parser_t *parser;
    client_t *client;

    DEBUG1("Adding source at mountpoint \"%s\"", mount);

	streamsock = sock_connect_wto(server, port, 0);
	if (streamsock == SOCK_ERROR) {
        WARN2("Failed to relay stream from master server, couldn't connect to http://%s:%d", server, port);
        return;
	}
	con = create_connection(streamsock, NULL);
	sock_write(streamsock, "GET %s HTTP/1.0\r\n\r\n", mount);
	memset(header, 0, sizeof(header));
	if (util_read_header(con->sock, header, 4096) == 0) {
		connection_close(con);
		return;
	}
	parser = httpp_create_parser();
	httpp_initialize(parser, NULL);
	if(!httpp_parse_response(parser, header, strlen(header), mount)) {
        if(httpp_getvar(parser, HTTPP_VAR_ERROR_MESSAGE)) {
            ERROR1("Error parsing relay request: %s", 
                    httpp_getvar(parser, HTTPP_VAR_ERROR_MESSAGE));
        }
        else
            ERROR0("Error parsing relay request");
		connection_close(con);
        httpp_destroy(parser);
        return;
    }

    client = client_create(con, parser);
	if (!connection_create_source(client, con, parser, 
                httpp_getvar(parser, HTTPP_VAR_URI))) {
        client_destroy(client);
	}
    return;
}

static void *_slave_thread(void *arg) {
	sock_t mastersock;
	char buf[256];
    int interval = config_get_config()->master_update_interval;
    char *authheader, *data;
    int len;
    char *username = "relay";
    char *password = config_get_config()->master_password;
    relay_server *relay;

    if(password == NULL)
        password = config_get_config()->source_password;


	while (_initialized) {
        if (config_get_config()->master_update_interval > ++interval) {
		    thread_sleep(1000000);
            continue;
        }
        else
            interval = 0;

        if(config_get_config()->master_server != NULL) {
		    mastersock = sock_connect_wto(config_get_config()->master_server, 
                    config_get_config()->master_server_port, 0);
    		if (mastersock == SOCK_ERROR) {
                WARN0("Relay slave failed to contact master server to fetch stream list");
		    	continue;
    		}

            len = strlen(username) + strlen(password) + 1;
            authheader = malloc(len+1);
            strcpy(authheader, username);
            strcat(authheader, ":");
            strcat(authheader, password);
            data = util_base64_encode(authheader);
		    sock_write(mastersock, 
                    "GET /admin/streamlist HTTP/1.0\r\n"
                    "Authorization: Basic %s\r\n"
                    "\r\n", data);
            free(authheader);
            free(data);
    		while (sock_read_line(mastersock, buf, sizeof(buf))) {
                if(!strlen(buf))
                    break;
            }

	    	while (sock_read_line(mastersock, buf, sizeof(buf))) {
		    	avl_tree_rlock(global.source_tree);
			    if (!source_find_mount(buf)) {
				    avl_tree_unlock(global.source_tree);

                    create_relay_stream(
                            config_get_config()->master_server,
                            config_get_config()->master_server_port,
                            buf);
    			} 
                else
    	    		avl_tree_unlock(global.source_tree);
    		}
	    	sock_close(mastersock);
        }

        /* And now, we process the individual mounts... */
        relay = config_get_config()->relay;
        while(relay) {
            avl_tree_rlock(global.source_tree);
            if(!source_find_mount(relay->mount)) {
                avl_tree_unlock(global.source_tree);

                create_relay_stream(relay->server, relay->port, relay->mount);
            }
            else
                avl_tree_unlock(global.source_tree);
            relay = relay->next;
        }
	}
	thread_exit(0);
	return NULL;
}

