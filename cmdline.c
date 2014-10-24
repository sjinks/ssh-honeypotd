#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>
#include <unistd.h>
#include <pwd.h>
#include <grp.h>
#include <limits.h>
#include "globals.h"
#include "cmdline.h"

static struct option long_options[] = {
	{ "rsakey",  required_argument, 0, 'r' },
	{ "dsakey",  required_argument, 0, 'd' },
	{ "hostkey", required_argument, 0, 'k' },
	{ "address", required_argument, 0, 'b' },
	{ "port",    required_argument, 0, 'p' },
	{ "pid",     required_argument, 0, 'P' },
	{ "name",    required_argument, 0, 'n' },
	{ "user",    required_argument, 0, 'u' },
	{ "group",   required_argument, 0, 'g' },
	{ "help",    no_argument,       0, 'h' },
	{ "version", no_argument,       0, 'v' }
};

static void usage(struct globals_t* g)
{
	printf(
		"Usage: ssh-honeypotd [options]...\n"
		"Low-interaction SSH honeypot\n\n"
		"Mandatory arguments to long options are mandatory for short options too.\n"
		"  -r, --rsa-key FILE    the file containing the private host RSA key (SSH2)\n"
		"  -d, --dsa-key FILE    the file containing the private host DSA key (SSH2)\n"
		"  -k, --host-key FILE   the file containing the private host key (SSH1)\n"
		"  -b, --address ADDRESS the IP address to bind to (default: 0.0.0.0)\n"
		"  -p, --port PORT       the port to bind to (default: 22)\n"
		"  -P, --pid FILE        the PID file\n"
		"                        (default: /var/run/ssh-honeypotd/ssh-honeypotd.pid)\n"
		"  -n, --name NAME       the name of the daemon for syslog\n"
		"                        (default: ssh-honeypotd)\n"
		"  -u, --user USER       drop privileges and switch to this USER\n"
		"                        (default: daemon or nobody)\n"
		"  -g, --group GROUP     drop privileges and switch to this GROUP\n"
		"                        (default: daemon or nogroup)\n"
		"  -f, --foreground      do not daemonize\n"
		"      --help            display this help and exit\n"
		"  -v, --version         output version information and exit\n\n"
		"One of -r, -d, or -k must be specified.\n\n"
		"Please report bugs here: <https://github.com/sjinks/ssh-honeypotd/issues>\n"
	);

	free_globals(g);
	exit(0);
}

static void version(struct globals_t* g)
{
	printf(
		"ssh-honeypotd 0.2\n"
		"Copyright (c) 2014, Volodymyr Kolesnykov <volodymyr@wildwolf.name>\n"
		"License: MIT <http://opensource.org/licenses/MIT>\n"
	);

	free_globals(g);
	exit(0);
}

void parse_options(int argc, char** argv, struct globals_t* g)
{
	while (1) {
		int option_index = 0;
		int c = getopt_long(argc, argv, "r:d:k:b:p:P:n:c:u:g:vf", long_options, &option_index);
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

			case 'k':
				if (g->host_key) {
					free(g->host_key);
				}

				g->host_key = strdup(optarg);
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

			case 'f':
				g->foreground = 1;
				break;

			case 'u': {
				struct passwd* pwd = getpwnam(optarg);
				if (!pwd) {
					fprintf(stderr, "WARNING: unknown user %s\n", optarg);
				}
				else {
					g->uid     = pwd->pw_uid;
					g->uid_set = 1;
					if (!g->gid_set) {
						g->gid     = pwd->pw_gid;
						g->gid_set = 1;
					}
				}

				break;
			}

			case 'g': {
				struct group* grp = getgrnam(optarg);
				if (!grp) {
					fprintf(stderr, "WARNING: unknown group %s\n", optarg);
				}
				else {
					g->gid     = grp->gr_gid;
					g->gid_set = 1;
				}

				break;
			}

			case 'h':
				usage(g);
				break;

			case 'v':
				version(g);
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
		g->pid_file = strdup("/var/run/ssh-honeypotd/ssh-honeypotd.pid");
	}
	else if (g->pid_file[0] != '/') {
		char buf[PATH_MAX+1];
		char* cwd    = getcwd(buf, PATH_MAX + 1);
		char* newbuf = NULL;

		if (cwd) {
			size_t cwd_len = strlen(cwd);
			size_t pid_len = strlen(g->pid_file);
			char* newbuf   = calloc(cwd_len + pid_len + 2, 1);
			if (newbuf) {
				memcpy(newbuf, cwd, cwd_len);
				newbuf[cwd_len + 1] = '/';
				memcpy(newbuf + cwd_len + 1, g->pid_file, pid_len);
			}
		}

		free(g->pid_file);
		g->pid_file = newbuf;
	}

	if (!g->bind_address || !g->bind_port || !g->daemon_name || !g->pid_file) {
		exit(1);
	}
}
