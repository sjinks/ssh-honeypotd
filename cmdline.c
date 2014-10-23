#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>
#include "globals.h"
#include "cmdline.h"

static struct option long_options[] = {
	{ "rsakey",  required_argument, 0, 'r' },
	{ "dsakey",  required_argument, 0, 'd' },
	{ "address", required_argument, 0, 'b' },
	{ "port",    required_argument, 0, 'p' },
	{ "pid",     required_argument, 0, 'P' },
	{ "name",    required_argument, 0, 'n' }
};

void parse_options(int argc, char** argv, struct globals_t* g)
{
	while (1) {
		int option_index = 0;
		int c = getopt_long(argc, argv, "r:d:b:p:P:n:c:", long_options, &option_index);
		if (-1 == c) {
			break;
		}

		switch (c) {
			case 'r':
				if (g->rsa_key) {
					free(g->rsa_key);
				}

				g->rsa_key = strdup(optarg);
				break;

			case 'd':
				if (g->dsa_key) {
					free(g->dsa_key);
				}

				g->dsa_key = strdup(optarg);
				break;

			case 'a':
				if (g->bind_address) {
					free(g->bind_address);
				}

				g->bind_address = strdup(optarg);
				break;

			case 'p':
				if (g->bind_port) {
					free(g->bind_port);
				}

				g->bind_port = strdup(optarg);
				break;

			case 'P':
				if (g->pid_file) {
					free(g->pid_file);
				}

				g->pid_file = strdup(optarg);
				break;

			case 'n':
				if (g->daemon_name) {
					free(g->daemon_name);
				}

				g->daemon_name = strdup(optarg);
				break;

			case '?':
			default:
				break;
		}
	}

	while (optind < argc) {
		fprintf(stderr, "WARNING: unrecognized option: %s\n", argv[optind]);
		++optind;
	}

	if (!g->bind_address) {
		g->bind_address = strdup("0.0.0.0");
	}

	if (!g->bind_port) {
		g->bind_port = strdup("22");
	}

	if (!g->daemon_name) {
		g->daemon_name = strdup("ssh-honeypotd");
	}

	if (!g->pid_file) {
		g->pid_file = strdup("/var/run/ssh-honeypotd.pid");
	}
}
