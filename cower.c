/*
 *  cower.c
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

/* Standard */
#include <alpm.h>
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Non-standard */
#include <curl/curl.h>
#include <jansson.h>

/* Local */
#include "alpmhelper.h"
#include "aur.h"
#include "util.h"

CURL *curl; /* curl agent for interaction with AUR */
int oper_mask = 0, opt_mask = 0; /* Runtime Config */

static alpm_list_t *targets; /* Package argument list */

static int parseargs(int argc, char **argv) {
    int opt;
    int option_index = 0;
    static struct option opts[] = {
        /* Operations */
        {"search",      no_argument,        0, 's'},
        {"update",      no_argument,        0, 'u'},
        {"info",        no_argument,        0, 'i'},
        {"download",    no_argument,        0, 'd'},

        /* Options */
        {"color",       no_argument,        0, 'c'},
        {"verbose",     no_argument,        0, 'v'},
        {"force",       no_argument,        0, 'f'},
        {"quiet",       no_argument,        0, 'q'},
        {0, 0, 0, 0}
    };

    while ((opt = getopt_long(argc, argv, "suidcvfq", opts, &option_index))) {
        if (opt < 0) {
            break;
        }

        switch (opt) {
            case 's':
                oper_mask |= OPER_SEARCH;
                break;
            case 'u':
                oper_mask |= OPER_UPDATE;
                break;
            case 'i':
                oper_mask |= OPER_INFO;
                break;
            case 'd':
                oper_mask |= OPER_DOWNLOAD;
                break;
            case 'c':
                opt_mask |= OPT_COLOR;
                break;
            case 'v':
                opt_mask |= OPT_VERBOSE;
                break;
            case 'f':
                opt_mask |= OPT_FORCE;
                break;
            case 'q':
                opt_mask |= OPT_QUIET;
                break;
            case '?': 
                return 1;
            default: 
                return 1;
        }
    }

    /* Feed the remaining args into a linked list */
    while (optind < argc)
        targets = alpm_list_add(targets, argv[optind++]);

    return 0;
}

static void usage() {
printf("cower v0.9.1\n\
Usage: cower [options] <operation> PACKAGE [PACKAGE2..]\n\
\n\
 Operations:\n\
  -d, --download          download PACKAGE(s)\n\
  -i, --info              show info for PACKAGE(s)\n\
  -s, --search            search for PACKAGE(s)\n\
  -u, --update            check for updates against AUR. If the \n\
                            --download flag is passed as well,\n\
                            fetch each available update.\n\n");
printf(" General options:\n\
  -c, --color             use colored output\n\
  -f, --force             overwrite existing files when dowloading\n\
  -q, --quiet             output less to stdout\n\n");
}

int main(int argc, char **argv) {

    int ret;
    ret = parseargs(argc, argv);

    /* Order matters somewhat. Update must come before download
     * to ensure that we catch the possibility of a download flag
     * being passed along with it.
     */
    if (oper_mask & OPER_UPDATE) { /* 8 */
        curl = curl_easy_init();
        alpm_quick_init();
        alpm_list_t *foreign = alpm_query_search(NULL);

        if (foreign) {
            aur_find_updates(foreign);
        }
        alpm_list_free(foreign);
    } else if (oper_mask & OPER_DOWNLOAD) { /* 4 */
        /* Query alpm for the existance of this package. If it's not in the 
         * sync, do an info query on the package in the AUR. Does it exist?
         * If yes, pass it to aur_get_tarball.
         */
        curl = curl_easy_init();
        alpm_quick_init();
        alpm_list_t *i;
        for (i = targets; i; i = alpm_list_next(i)) {
            int result;
            result = is_in_pacman((const char*)i->data);
            if (! result) { /* Not found in pacman */
                json_t *infojson = 
                    aur_rpc_query(AUR_RPC_QUERY_TYPE_INFO, i->data);
                if (infojson) { /* Found it in the AUR */
                    aur_get_tarball(infojson, NULL);
                    json_decref(infojson);
                } else { /* Not found anywhere */
                    opt_mask & OPT_COLOR ? cfprintf(stderr, RED, "error:", NULL) :
                        fprintf(stderr, "error:");
                    fprintf(stderr, " no results for \"%s\"\n", 
                        (const char*)i->data);
                }
            }
        }
    } else if (oper_mask & OPER_INFO) { /* 2 */
        curl = curl_easy_init();
        alpm_list_t *i;
        for (i = targets; i; i = alpm_list_next(i)) {
            json_t *search = aur_rpc_query(AUR_RPC_QUERY_TYPE_INFO, i->data);

            if (search) {
                print_pkg_info(search);
            } else {
                opt_mask & OPT_COLOR ? cfprintf(stderr, RED, "error:", NULL) :
                    fprintf(stderr, "error:");
                fprintf(stderr, " no results for \"%s\"\n", 
                    (const char*)i->data);
            }
            json_decref(search);

        }
    } else if (oper_mask & OPER_SEARCH) { /* 1 */
        /* TODO: Combine all searches, sort, remove dupes, and print once */
        curl = curl_easy_init();
        alpm_list_t *i;
        for (i = targets; i; i = alpm_list_next(i)) {
            json_t *search = aur_rpc_query(AUR_RPC_QUERY_TYPE_SEARCH, i->data);

            if (search) {
                print_pkg_search(search);
            } else {
                opt_mask & OPT_COLOR ? cfprintf(stderr, RED, "error:", NULL) :
                    fprintf(stderr, "error:");
                fprintf(stderr, " no results for \"%s\"\n", 
                    (const char*)i->data);
            }
            json_decref(search);
        }
    } else {
        usage();
        ret = 1;
    }

    /* Compulsory cleanup */
    curl_easy_cleanup(curl);
    curl_global_cleanup();
    alpm_list_free(targets);
    alpm_release();

    return ret;
}

