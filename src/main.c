/* -*- c-basic-offset: 4; indent-tabs-mode: nil; -*- */
#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdio.h>
#include <string.h>

#include "thread/thread.h"
#include "avl/avl.h"
#include "net/sock.h"
#include "net/resolver.h"
#include "httpp/httpp.h"

#ifdef CHUID
#include <sys/types.h>
#include <grp.h>
#include <pwd.h>
#include <errno.h>
#endif

#ifdef CHROOT
#include <unistd.h>
#endif

#include "cfgfile.h"
#include "sighandler.h"

#include "global.h"
#include "os.h"
#include "connection.h"
#include "refbuf.h"
#include "client.h"
#include "slave.h"
#include "stats.h"
#include "logging.h"
#include "xslt.h"
#include "fserve.h"
#ifdef USE_YP
#include "geturl.h"
#endif

#include <libxml/xmlmemory.h>

#ifdef _WIN32
#define snprintf _snprintf
#endif

#undef CATMODULE
#define CATMODULE "main"

static void _fatal_error(char *perr)
{
#ifdef WIN32
    MessageBox(NULL, perr, "Error", MB_OK);
#else
    fprintf(stdout, "%s\n", perr);
#endif
}

static void _print_usage()
{
    printf("Usage:\n");
    printf("\ticecast -c <file>\t\tSpecify configuration file\n");
    printf("\n");
}

static void _stop_logging(void)
{
    log_close(errorlog);
    log_close(accesslog);
}

static void _initialize_subsystems(void)
{
    log_initialize();
    thread_initialize();
    sock_initialize();
    resolver_initialize();
    config_initialize();
    connection_initialize();
    global_initialize();
    refbuf_initialize();
    xslt_initialize();
#ifdef USE_YP
    curl_initialize();
#endif
}

static void _shutdown_subsystems(void)
{
#ifdef USE_YP
    curl_shutdown();
#endif
    fserve_shutdown();
    xslt_shutdown();
    refbuf_shutdown();
    stats_shutdown();
    slave_shutdown();

    /* Now that these are done, we can stop the loggers. */
    _stop_logging();

    global_shutdown();
    connection_shutdown();
    config_shutdown();
    resolver_shutdown();
    sock_shutdown();
    thread_shutdown();
    log_shutdown();

    xmlCleanupParser();
}

static int _parse_config_file(int argc, char **argv, char *filename, int size)
{
    int i = 1;
    int    processID = 0;

    if (argc < 3) return -1;

    while (i < argc) {
        if (strcmp(argv[i], "-b") == 0) {
#ifndef WIN32
                fprintf(stdout, "Starting icecast2\nDetaching from the console\n");
                if ((processID = (int)fork()) > 0) {
                        /* exit the parent */
                        _exit(0);
                }
#endif
        }
        if (strcmp(argv[i], "-c") == 0) {
            if (i + 1 < argc) {
                strncpy(filename, argv[i + 1], size-1);
                filename[size-1] = 0;
                return 1;
            } else {
                return -1;
            }
        }
        i++;
    }

    return -1;
}

static int _start_logging(void)
{
    char fn_error[FILENAME_MAX];
    char fn_access[FILENAME_MAX];
    char pbuf[1024];

    ice_config_t *config = config_get_config_unlocked();

    if(strcmp(config->error_log, "-")) {
        snprintf(fn_error, FILENAME_MAX, "%s%s%s", config->log_dir, PATH_SEPARATOR, config->error_log);
        errorlog = log_open(fn_error);
    } else {
        errorlog = log_open_file(stderr);
    }
    if(strcmp(config->access_log, "-")) {
        snprintf(fn_access, FILENAME_MAX, "%s%s%s", config->log_dir, PATH_SEPARATOR, config->access_log);
        accesslog = log_open(fn_access);
    } else {
        accesslog = log_open_file(stderr);
    }
    
    log_set_level(errorlog, config->loglevel);
    log_set_level(accesslog, 4);

    if (errorlog < 0) {
        _fatal_error("FATAL: could not open error logging");
    }
    if (accesslog < 0) {
        memset(pbuf, '\000', sizeof(pbuf));
        snprintf(pbuf, sizeof(pbuf)-1, "FATAL: could not open access logging");
        _fatal_error(pbuf);
    }
    if (errorlog >= 0 && accesslog >= 0) return 1;
    
    return 0;
}

static int _setup_sockets(void)
{
    ice_config_t *config;
    int i = 0;
    int ret = 0;
    int successful = 0;
    char pbuf[1024];

    config = config_get_config_unlocked();

    for(i = 0; i < MAX_LISTEN_SOCKETS; i++) {
        if(config->listeners[i].port <= 0)
            break;

        global.serversock[i] = sock_get_server_socket(
                config->listeners[i].port, config->listeners[i].bind_address);

        if (global.serversock[i] == SOCK_ERROR) {
            memset(pbuf, '\000', sizeof(pbuf));
            snprintf(pbuf, sizeof(pbuf)-1, 
                "Could not create listener socket on port %d", 
                config->listeners[i].port);
            _fatal_error(pbuf);
            return 0;
        }
        else {
            ret = 1;
            successful++;
        }
    }

    global.server_sockets = successful;
    
    return ret;
}

static int _start_listening(void)
{
    int i;
    for(i=0; i < global.server_sockets; i++) {
        if (sock_listen(global.serversock[i], ICE_LISTEN_QUEUE) == SOCK_ERROR)
            return 0;

        sock_set_blocking(global.serversock[i], SOCK_NONBLOCK);
    }

    return 1;
}

/* bind the socket and start listening */
static int _server_proc_init(void)
{
    if (!_setup_sockets())
        return 0;

    if (!_start_listening()) {
        _fatal_error("Failed trying to listen on server socket");
        return 0;
    }

    return 1;
}

/* this is the heart of the beast */
static void _server_proc(void)
{
    int i;

    connection_accept_loop();

    for(i=0; i < MAX_LISTEN_SOCKETS; i++)
        sock_close(global.serversock[i]);
}

/* chroot the process. Watch out - we need to do this before starting other
 * threads. Change uid as well, after figuring out uid _first_ */

static void _ch_root_uid_setup(void)
{
   ice_config_t *conf = config_get_config_unlocked();
#ifdef CHUID
   struct passwd *user;
   struct group *group;
   uid_t uid=-1;
   gid_t gid=-1;

   if(conf->chuid)
   {
       if(conf->user) {
           user = getpwnam(conf->user);
           if(user)
               uid = user->pw_uid;
           else
               fprintf(stderr, "Couldn't find user \"%s\" in password file\n", conf->user);
       }
       if(conf->group) {
           group = getgrnam(conf->group);

           if(group)
               gid = group->gr_gid;
           else
               fprintf(stderr, "Couldn't find group \"%s\" in groups file\n", conf->group);
       }
   }
#endif

#ifdef CHROOT
   if (conf->chroot)
   {
       if(getuid()) /* root check */
       {
           fprintf(stderr, "WARNING: Cannot change server root unless running as root.\n");
           return;
       }
       if(chroot(conf->base_dir))
       {
           fprintf(stderr,"WARNING: Couldn't change server root: %s\n", strerror(errno));
           return;
       }
       else
           fprintf(stdout, "Changed root successfully to \"%s\".\n", conf->base_dir);

   }   
#endif
#ifdef CHUID

   if(conf->chuid)
   {
       if(getuid()) /* root check */
       {
           fprintf(stderr, "WARNING: Can't change user id unless you are root.\n");
           return;
       }

       if(gid != -1) {
           if(!setgid(gid))
               fprintf(stdout, "Changed groupid to %i.\n", gid);
           else
               fprintf(stdout, "Error changing groupid: %s.\n", strerror(errno));
       }

       if(uid != -1) {
           if(!setuid(uid))
               fprintf(stdout, "Changed userid to %i.\n", uid);
           else
               fprintf(stdout, "Error changing userid: %s.\n", strerror(errno));
       }
   }
#endif
}

int main(int argc, char **argv)
{
    int res, ret;
    char filename[512];
    char pbuf[1024];

    /* parse the '-c icecast.xml' option
    ** only, so that we can read a configfile
    */
    res = _parse_config_file(argc, argv, filename, 512);
    if (res == 1) {
        /* startup all the modules */
        _initialize_subsystems();

        /* parse the config file */
        config_get_config();
        ret = config_initial_parse_file(filename);
        config_release_config();
        if (ret < 0) {
            memset(pbuf, '\000', sizeof(pbuf));
            snprintf(pbuf, sizeof(pbuf)-1, 
                "FATAL: error parsing config file (%s)", filename);
            _fatal_error(pbuf);
            switch (ret) {
            case CONFIG_EINSANE:
                _fatal_error("filename was null of blank");
                break;
            case CONFIG_ENOROOT:
                _fatal_error("no root element found");
                break;
            case CONFIG_EBADROOT:
                _fatal_error("root element is not <icecast>");
                break;
            default:
                _fatal_error("XML config parsing error");
                break;
            }
            _shutdown_subsystems();
            return 1;
        }
    } else if (res == -1) {
        _print_usage();
        return 1;
    }
    
    /* override config file options with commandline options */
    config_parse_cmdline(argc, argv);

    /* Bind socket, before we change userid */
    if(!_server_proc_init()) {
        _fatal_error("Server startup failed. Exiting");
        _shutdown_subsystems();
        return 1;
    }

    _ch_root_uid_setup(); /* Change user id and root if requested/possible */

    stats_initialize(); /* We have to do this later on because of threading */
    fserve_initialize(); /* This too */

#ifdef CHUID 
    /* We'll only have getuid() if we also have setuid(), it's reasonable to
     * assume */
    if(!getuid()) /* Running as root! Don't allow this */
    {
        fprintf(stderr, "WARNING: You should not run icecast2 as root\n");
        fprintf(stderr, "Use the changeowner directive in the config file\n");
        _shutdown_subsystems();
        return 1;
    }
#endif

    /* setup default signal handlers */
    sighandler_initialize();

    if (!_start_logging()) {
        _fatal_error("FATAL: Could not start logging");
        _shutdown_subsystems();
        return 1;
    }

    /* Do this after logging init */
    slave_initialize();

    INFO0("icecast server started");

    /* REM 3D Graphics */

    /* let her rip */
    global.running = ICE_RUNNING;

#ifdef USE_YP
    /* Startup yp thread */
    yp_initialize();
#endif

    _server_proc();

    INFO0("Shutting down");

    _shutdown_subsystems();

    return 0;
}















