#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "event.h"
#include "cfgfile.h"

#include "refbuf.h"
#include "client.h"
#include "logging.h"

#define CATMODULE "event"

void event_config_read(void *arg)
{
    int ret;
    ice_config_t *config;
    ice_config_t new_config;
    /* reread config file */

    config = config_get_config(); /* Both to get the lock, and to be able
                                     to find out the config filename */
    ret = config_parse_file(config->config_filename, &new_config);
    if(ret < 0) {
        ERROR0("Error parsing config, not replacing existing config");
        switch(ret) {
            case CONFIG_EINSANE:
                ERROR0("Config filename null or blank");
                break;
            case CONFIG_ENOROOT:
                ERROR1("Root element not found in %s", config->config_filename);
                break;
            case CONFIG_EBADROOT:
                ERROR1("Not an icecast2 config file: %s",
                        config->config_filename);
                break;
            default:
                ERROR1("Parse error in reading %s", config->config_filename);
                break;
        }
        config_release_config();
    }
    else {
        config_clear(config);
        config_set_config(&new_config);
        restart_logging ();

        config_release_config();
    }
}

