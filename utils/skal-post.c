/* Copyright (c) 2016,2017  Fabrice Triboix
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "skal-msg.h"
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <pthread.h>
#include <errno.h>


static enum {
    STARTING,
    RUNNING,
    TERMINATING
} gRunningState = STARTING;

static void handleSignal(int signum)
{
    switch (gRunningState) {
    case STARTING :
        fprintf(stderr,
                "Received signal %d, but SKAL has not initialised yet; forcing termination\n",
                signum);
        exit(2);
        break;

    case RUNNING :
        fprintf(stderr, "Received signal %d, terminating...\n", signum);
        fprintf(stderr, "  (send signal again to force termination)\n");
        gRunningState = TERMINATING;
        SkalCancel();
        break;

    case TERMINATING :
        fprintf(stderr, "Received signal %d again, forcing termination now\n",
                signum);
        exit(2);
        break;
    }
}


static const char* gOptString = "hnw:u:S:f:F:t:i:d:s:b:";

static void usage(int ret)
{
    printf( "Usage: skal-post [OPTIONS] NAME RECIPIENT\n"
            "Send a SKAL message to the given recipient.\n"
            "Options:\n"
            "     NAME        Message name\n"
            "     RECIPIENT   Recipient of this message\n"
            "  -h             Print this usage information and exit\n"
            "  -n             Dry-run: print message to stdout and exit\n"
            "  -w TIMEOUT_ms  Wait (at most TIMEOUT_ms) for a response and print it to stdout\n"
            "  -u URL         URL to connect to skald\n"
            "  -S SENDER      Set message SENDER\n"
            "  -f FLAGS       Set message FLAGS (8-bit unsigned integer)\n"
            "  -F IFLAGS      Set message internal IFLAGS (8-bit unsigned integer)\n"
            "  -t TTL         Set message TTL (must be >0 and <=127)\n"
            "  -i NAME=VALUE  Add 64-bit signed integer (NAME, VALUE)\n"
            "  -d NAME=VALUE  Add double (NAME, VALUE)\n"
            "  -s NAME=VALUE  Add string (NAME, VALUE)\n"
            "  -b NAME=VALUE  Add miniblob (NAME, VALUE) - see below\n"
            "A miniblob VALUE is a hex string, eg: 123456 is 0x12, 0x34, 0x56\n");
    exit(ret);
}


static char* split(char** pValue)
{
    char* str = SkalStrdup(optarg);
    char* ptr = strchr(str, '=');
    if (NULL == ptr) {
        fprintf(stderr, "Invalid argument '%s'\n", optarg);
        exit(2);
    }
    *ptr = '\0';
    ptr++;
    if ('\0' == str[0]) {
        fprintf(stderr, "Invalid argument '%s': no name\n", optarg);
        exit(2);
    }
    if ('\0' == ptr[0]) {
        fprintf(stderr, "Invalid argument '%s': no value\n", optarg);
        exit(2);
    }
    *pValue = ptr;
    return str;
}


typedef struct {
    int    argc;
    char** argv;
} Args;


static enum {
    INIT,
    WAIT_FOR_RESPONSE,
    DONE
} gState = INIT;


static int gResponseTimeout_ms = -1; // -1 means: to not wait for a response
static pthread_t gTimeoutTread;


static void* timeoutThread(void* arg)
{
    (void)arg;
    usleep(gResponseTimeout_ms * 1000);
    printf("ERROR: Timeout waiting for a response\n");
    exit(1);
}


static bool doPost(Args* args)
{
    // 2nd pass: parse arguments necessary to create the bare message
    uint8_t flags = 0;
    int8_t ttl = 0;
    bool dryrun = false;
    optind = 1; // reset getopt
    int opt = 0;
    while (opt != -1) {
        opt = getopt(args->argc, args->argv, gOptString);
        switch (opt) {
        case 'n' :
            dryrun = true;
            break;
        case 'w' :
            {
                int tmp;
                if (sscanf(optarg, "%d", &tmp) != 1) {
                    fprintf(stderr, "Invalid TIMEOUT_ms: '%s'\n", optarg);
                    exit(2);
                }
                if (tmp <= 0) {
                    fprintf(stderr, "Invalid TIMEOUT_ms: %d\n", tmp);
                    exit(2);
                }
                gResponseTimeout_ms = tmp;
            }
            break;
        case 'f' :
            {
                unsigned int tmp;
                if (sscanf(optarg, "%u", &tmp) != 1) {
                    fprintf(stderr, "Invalid FLAGS: '%s'\n", optarg);
                    exit(2);
                }
                if (tmp & 0xff) {
                    fprintf(stderr, "Invalid FLAGS: %u\n", tmp);
                    exit(2);
                }
                flags = tmp & 0xff;
            }
            break;
        case 't' :
            {
                int tmp;
                if (sscanf(optarg, "%d", &tmp) != 1) {
                    fprintf(stderr, "Invalid TTL: '%s'\n", optarg);
                    exit(2);
                }
                if ((tmp <= 0) || (tmp > 127)) {
                    fprintf(stderr, "Invalid TTL: %d\n", tmp);
                    exit(2);
                }
                ttl = (int8_t)tmp;
            }
            break;
        default :
            break;
        }
    } // getopt loop

    if (args->argc - optind <= 1) {
        fprintf(stderr, "NAME and RECIPIENT are required arguments\n");
        fprintf(stderr, "Run '%s -h' for help\n", args->argv[0]);
        exit(2);
    }

    // Create bare message
    SkalMsg* msg = SkalMsgCreateEx(args->argv[optind], args->argv[optind+1],
            flags, ttl);

    // 3rd pass: parse arguments to add stuff to the message
    optind = 1; // reset getopt(3)
    opt = 0;
    while (opt != -1) {
        opt = getopt(args->argc, args->argv, gOptString);
        switch (opt) {
        case 'S' :
            SkalMsgSetSender(msg, optarg);
            break;
        case 'F' :
            {
                unsigned int tmp;
                if (sscanf(optarg, "%u", &tmp) != 1) {
                    fprintf(stderr, "Invalid IFLAGS: '%s'\n", optarg);
                    exit(2);
                }
                if (tmp & 0xff) {
                    fprintf(stderr, "Invalid IFLAGS: %u\n", tmp);
                    exit(2);
                }
                SkalMsgSetIFlags(msg, tmp & 0xff);
            }
            break;
        case 'i' :
            {
                char* value;
                char* name = split(&value);
                long long tmp;
                if (sscanf(value, "%lld", &tmp) != 1) {
                    fprintf(stderr, "Invalid integer value: '%s'\n", value);
                    exit(2);
                }
                SkalMsgAddInt(msg, name, (int64_t)tmp);
                free(name);
            }
            break;
        case 'd' :
            {
                char* value;
                char* name = split(&value);
                double tmp;
                if (sscanf(value, "%lf", &tmp) != 1) {
                    fprintf(stderr, "Invalid double value: '%s'\n", value);
                    exit(2);
                }
                SkalMsgAddDouble(msg, name, tmp);
                free(name);
            }
            break;
        case 's' :
            {
                char* value;
                char* name = split(&value);
                SkalMsgAddString(msg, name, value);
                free(name);
            }
            break;
        case 'b' :
            {
                char* value;
                char* name = split(&value);
                int n = strlen(value);
                if (n & 1) {
                    fprintf(stderr,
                            "Miniblob value is not a valid hex string: '%s'\n",
                            value);
                    exit(2);
                }
                n = n / 2;
                uint8_t* data = malloc(n);
                if (NULL == data) {
                    fprintf(stderr, "Failed to allocate %d bytes\n", n);
                    exit(1);
                }
                for (int i = 0; i < n ; i++) {
                    char str[3];
                    str[0] = value[2*i];
                    str[1] = value[2*i + 1];
                    str[2] = '\0';
                    unsigned int tmp;
                    if (sscanf(str, "%x", &tmp) != 1) {
                        fprintf(stderr, "Invalid miniblob byte: '%s'\n", str);
                        exit(2);
                    }
                    data[i] = (uint8_t)tmp;
                }
                SkalMsgAddMiniblob(msg, name, data, n);
                free(data);
                free(name);
            }
            break;
        default :
            break;
        }
    } // getopt loop

    if (dryrun) {
        char* json = SkalMsgToJson(msg);
        printf("%s\n", json);
        free(json);
        SkalMsgUnref(msg);
        gState = DONE;
        return false;
    }

    SkalMsgSend(msg);

    if (gResponseTimeout_ms > 0) {
        int ret = pthread_create(&gTimeoutTread, NULL, timeoutThread, NULL);
        if (ret != 0) {
            fprintf(stderr, "Failed to create timeout thread: %s [%d]\n",
                    strerror(errno), errno);
            exit(1);
        }
        gState = WAIT_FOR_RESPONSE;
        return true;
    }

    gState = DONE;
    return false;
}


static bool processMsg(void* cookie, SkalMsg* msg)
{
    if (strcmp(SkalMsgName(msg), "skal-post-kick") == 0) {
        return doPost((Args*)cookie);
    }

    if (WAIT_FOR_RESPONSE == gState) {
        char* json = SkalMsgToJson(msg);
        printf("%s\n", json);
        free(json);
        pthread_cancel(gTimeoutTread);
        pthread_join(gTimeoutTread, NULL);
        gState = DONE;
        return false;
    }

    return true;
}


int main(int argc, char** argv)
{
    // First pass of argument parsing: get arguments to initialise SKAL and
    // start a thread
    if (argc < 2) {
        usage(1);
    }
    char* url = NULL;
    int opt = 0;
    while (opt != -1) {
        opt = getopt(argc, argv, gOptString);
        switch (opt) {
        case 'h' :
            usage(0);
            break;
        case 'u' :
            SKALASSERT(optarg != NULL);
            if (strstr(optarg, "://") == NULL) {
                url = SkalSPrintf("unix://%s", optarg);
            } else {
                url = SkalStrdup(optarg);
            }
            break;
        case '?' :
            fprintf(stderr, "Unknown argument -%c\n", (char)optopt);
            exit(1);
            break;
        default :
            break;
        }
    } // getopt loop

    // Setup signal handlers
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = handleSignal;
    int ret = sigaction(SIGINT, &sa, NULL);
    if (ret < 0) {
        fprintf(stderr, "ERROR: sigaction(SIGINT) failed: %s [%d]\n",
                strerror(errno), errno);
        return 1;
    }
    ret = sigaction(SIGTERM, &sa, NULL);
    if (ret < 0) {
        fprintf(stderr, "ERROR: sigaction(SIGTERM) failed: %s [%d]\n",
                strerror(errno), errno);
        return 1;
    }

    // Initialise SKAL
    if (!SkalInit(url, NULL, 0)) {
        fprintf(stderr, "Failed to connect to skald\n");
        free(url);
        exit(1);
    }
    free(url);
    gRunningState = RUNNING;

    // Start thread
    Args* args = malloc(sizeof(*args));
    args->argc = argc;
    args->argv = argv;
    SkalThreadCfg cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.name = "skal-post";
    cfg.processMsg = processMsg;
    cfg.cookie = args;
    SkalThreadCreate(&cfg);

    // Kick off the thread and wait for it to finish
    SkalMsg* msg = SkalMsgCreate("skal-post-kick", "skal-post");
    SkalMsgSend(msg);
    SkalPause();

    // Cleanup
    SkalExit();
    free(args);
    return 0;
}
