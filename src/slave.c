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
#include "format.h"
#include "logging.h"

#include "source.h"

#define CATMODULE "slave"

static void *_slave_thread(void *arg);
long _slave_thread_id;
static int _initialized = 0;

void slave_initialize(void) {
	if (_initialized) return;
    /* Don't create a slave thread if it isn't configured */
    if (config_get_config()->master_server == NULL)
        return;

	_initialized = 1;
	_slave_thread_id = thread_create("Slave Thread", _slave_thread, NULL, THREAD_ATTACHED);
}

void slave_shutdown(void) {
	if (!_initialized) return;
	_initialized = 0;
	thread_join(_slave_thread_id);
}

static void *_slave_thread(void *arg) {
	sock_t mastersock, streamsock;
	char buf[256];
	char header[4096];
	connection_t *con;
	http_parser_t *parser;
    int interval = config_get_config()->master_update_interval;

	while (_initialized) {
        if (config_get_config()->master_update_interval > ++interval) {
		    thread_sleep(1000000);
            continue;
        }
        else
            interval = 0;

		mastersock = sock_connect_wto(config_get_config()->master_server, config_get_config()->master_server_port, 0);
		if (mastersock == SOCK_ERROR) {
			printf("DEBUG: failed to contact master server\n");
			continue;
		}
		sock_write(mastersock, "GET /allstreams.txt HTTP/1.0\r\nice-password: %s\r\n\r\n", config_get_config()->source_password);
		while (sock_read_line(mastersock, buf, sizeof(buf))) {
			buf[strlen(buf)] = 0;
			avl_tree_rlock(global.source_tree);
			if (!source_find_mount(buf)) {
				avl_tree_unlock(global.source_tree);
				printf("DEBUG: adding source for %s\n", buf);
				streamsock = sock_connect_wto(config_get_config()->master_server, config_get_config()->master_server_port, 0);
				if (streamsock == SOCK_ERROR) {
					printf("DEBUG: failed to get stream from master server\n");
					continue;
				}
				con = create_connection(streamsock, NULL);
				sock_write(streamsock, "GET %s HTTP/1.0\r\n\r\n", buf);
				memset(header, 0, sizeof(header));
				if (util_read_header(con->sock, header, 4096) == 0) {
					connection_close(con);
					continue;
				}
				parser = httpp_create_parser();
				httpp_initialize(parser, NULL);
				if(!httpp_parse_response(parser, header, strlen(header), buf)) {
                    if(httpp_getvar(parser, HTTPP_VAR_ERROR_MESSAGE)) {
                        ERROR1("Error parsing relay request: %s", 
                                httpp_getvar(parser, HTTPP_VAR_ERROR_MESSAGE));
                    }
                    else
                        ERROR0("Error parsing relay request");
					connection_close(con);
                    httpp_destroy(parser);
                    continue;
                }

				if (!connection_create_source(con, parser, 
                            httpp_getvar(parser, HTTPP_VAR_URI))) {
					connection_close(con);
					httpp_destroy(parser);
				}
				continue;

			}
			avl_tree_unlock(global.source_tree);
		}
		sock_close(mastersock);
	}
	thread_exit(0);
	return NULL;
}
