/* Copyright (c) 2016  Fabrice Triboix
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
#include <unistd.h>


const char* gOptString = "hnwS:f:F:m:i:d:s:b:";

static void usage(int ret)
{
    printf( "Usage: skal-post [OPTIONS] TYPE RECIPIENT\n"
            "Send a SKAL message to the given recipient.\n"
            "     TYPE        Message type\n"
            "     RECIPIENT   Recipient of this message\n"
            "  -h             Print this usage information and exit\n"
            "  -n             Dry-run: print message to stdout and exit\n"
            "  -w             Wait for a response and print it to stdout\n"
            "  -u URL         URL to connect to skald\n"
            "  -S SENDER      Set message SENDER\n"
            "  -f FLAGS       Set message FLAGS (8-bit unsigned integer)\n"
            "  -F IFLAGS      Set message internal IFLAGS (8-bit unsigned integer)\n"
            "  -m MARKER      Set message MARKER\n"
            "  -i NAME=VALUE  Add 64-bit signed integer (NAME, VALUE)\n"
            "  -d NAME=VALUE  Add double (NAME, VALUE)\n"
            "  -s NAME=VALUE  Add string (NAME, VALUE)\n"
            "  -b NAME=VALUE  Add miniblob (NAME, VALUE) - see below\n"
            "A miniblob VALUE is a hex string, eg: 123456 is 0x12, 0x34, 0x56\n");
    exit(ret);
}


static char* split(char** pValue)
{
    char* str = strdup(optarg);
    if (NULL == str) {
        fprintf(stderr, "strdup(%s) failed\n", optarg);
        exit(1);
    }
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


// Thread status: -1 = running, 0 = finished & success, 1 = finished & error
static int gStatus = -1;


static bool doPost(void* cookie, SkalMsg* msg)
{
    if (strcmp(SkalMsgType(msg), "kick") != 0) {
        return true;
    }

    Args* args = (Args*)cookie;

    // 2nd pass: parse arguments necessary to create the bare message
    uint8_t flags = 0;
    char* marker = NULL;
    bool dryrun = false;
    bool waitForResponse = false;
    optind = 1; // reset getopt
    int opt = 0;
    while (opt != -1) {
        opt = getopt(args->argc, args->argv, gOptString);
        switch (opt) {
        case 'n' :
            dryrun = true;
            break;
        case 'w' :
            waitForResponse = true;
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
        case 'm' :
            marker = strdup(optarg);
            if (NULL == marker) {
                fprintf(stderr, "Failed to strdup(%s)\n", optarg);
                exit(1);
            }
            break;
        default :
            break;
        }
    } // getopt loop

    if (args->argc - optind <= 1) {
        fprintf(stderr, "TYPE and RECIPIENT are required arguments\n");
        fprintf(stderr, "Run '%s -h' for help\n", args->argv[0]);
        exit(2);
    }

    // Create bare message
    msg = SkalMsgCreate(args->argv[optind], args->argv[optind+1],
            flags, marker);
    free(marker);

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

    } else {
        SkalMsgSend(msg);

        if (waitForResponse) {
            // TODO: implement waitForResponse
        } else {
            // Ensure we don't close the socket too quickly, otherwise skald
            // might error with an ECONNRESET at the same time the message is
            // received.
            usleep(5000);
        }

        gStatus = 0;
    }

    return false;
}


int main(int argc, char** argv)
{
    // First pass of argument parsing: get arguments to initialise SKAL and
    // start a thread
    char* url = NULL;
    int opt = 0;
    while (opt != -1) {
        opt = getopt(argc, argv, gOptString);
        switch (opt) {
        case 'h' :
            usage(0);
            break;
        case 'u' :
            url = strdup(optarg);
            if (NULL == url) {
                fprintf(stderr, "Failed to strdup(%s)\n", optarg);
                exit(1);
            }
            break;
        default :
            break;
        }
    } // getopt loop

    // Initialise SKAL and create a thread
    if (!SkalInit(url, NULL, 0)) {
        fprintf(stderr, "Failed to connect to skald\n");
        exit(1);
    }

    Args* args = malloc(sizeof(*args));
    args->argc = argc;
    args->argv = argv;
    SkalThreadCfg cfg;
    memset(&cfg, 0, sizeof(cfg));
    snprintf(cfg.name, sizeof(cfg.name), "skal-post");
    cfg.processMsg = doPost;
    cfg.cookie = args;
    SkalThreadCreate(&cfg);

    // Kick off the thread and wait for it to finish
    SkalMsg* msg = SkalMsgCreate("kick", "skal-post", 0, NULL);
    SkalMsgSend(msg);
    while (gStatus < 0) {
        usleep(100);
    }

    // Cleanup
    SkalExit();
    free(args);
    return gStatus;
}
