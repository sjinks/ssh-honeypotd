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
	{ "rsa-key",    required_argument, 0, 'r' },
	{ "dsa-key",    required_argument, 0, 'd' },
#if LIBSSH_VERSION_INT >= SSH_VERSION_INT(0, 6, 4)
	{ "ecdsa-key",  required_argument, 0, 'e' },
#endif
	{ "host-key",   required_argument, 0, 'k' },
	{ "address",    required_argument, 0, 'b' },
	{ "port",       required_argument, 0, 'p' },
	{ "pid",        required_argument, 0, 'P' },
	{ "name",       required_argument, 0, 'n' },
	{ "user",       required_argument, 0, 'u' },
	{ "group",      required_argument, 0, 'g' },
	{ "help",       no_argument,       0, 'h' },
	{ "version",    no_argument,       0, 'v' },
	{ "foreground", no_argument,       0, 'f' },
	{ 0,            0,                 0, 0   }
};

#if defined(__GNUC__) || defined(__clang__)
__attribute__((noreturn))
#endif
static void usage(struct globals_t* g)
{
	printf(
		"Usage: ssh-honeypotd [options]...\n"
		"Low-interaction SSH honeypot\n\n"
		"Mandatory arguments to long options are mandatory for short options too.\n"
		"  -k, --host-key FILE   the file containing the private host key (RSA, DSA, ECDSA, ED25519)\n"
		"  -b, --address ADDRESS the IP address to bind to (default: 0.0.0.0)\n"
		"  -p, --port PORT       the port to bind to (default: 22)\n"
		"  -P, --pid FILE        the PID file\n"
		"                        (default: /run/ssh-honeypotd/ssh-honeypotd.pid)\n"
		"  -n, --name NAME       the name of the daemon for syslog\n"
		"                        (default: ssh-honeypotd)\n"
		"  -u, --user USER       drop privileges and switch to this USER\n"
		"                        (default: daemon or nobody)\n"
		"  -g, --group GROUP     drop privileges and switch to this GROUP\n"
		"                        (default: daemon or nogroup)\n"
		"  -f, --foreground      do not daemonize\n"
		"  -h, --help            display this help and exit\n"
		"  -v, --version         output version information and exit\n\n"
		"-k option must be specified at least once.\n\n"
		"Please report bugs here: <https://github.com/sjinks/ssh-honeypotd/issues>\n"
	);

	exit(0);
}

#if defined(__GNUC__) || defined(__clang__)
__attribute__((noreturn))
#endif
static void version(struct globals_t* g)
{
	printf(
		"ssh-honeypotd 0.5\n"
		"Copyright (c) 2014-2017, Volodymyr Kolesnykov <volodymyr@wildwolf.name>\n"
		"License: MIT <http://opensource.org/licenses/MIT>\n"
	);

	exit(0);
}

void parse_options(int argc, char** argv, struct globals_t* g)
{
	while (1) {
		int option_index = 0;
		int c = getopt_long(argc, argv, "r:d:e:k:b:p:P:n:u:g:vfh", long_options, &option_index);
		if (-1 == c) {
			break;
		}

		switch (c) {
			case 'r':
			case 'd':
			case 'e':
			case 'k': {
				ssh_key key;
				int rc = ssh_pki_import_privkey_file(optarg, NULL, NULL, NULL, &key);
				if (-1 == rc) {
					fprintf(stderr, "WARNING: failed to import the key from %s\n", optarg);
					break;
				}

				int key_type = ssh_key_type(key);
				char* loc;
				switch (key_type) {
					case SSH_KEYTYPE_DSS:     loc = g->dsa_key;     break;
					case SSH_KEYTYPE_RSA:     loc = g->rsa_key;     break;
					case SSH_KEYTYPE_RSA1:    loc = g->rsa_key;     break;
#ifdef SSH_KEYTYPE_ECDSA
					case SSH_KEYTYPE_ECDSA:   loc = g->ecdsa_key;   break;
#endif
#ifdef SSH_KEYTYPE_ED25519
					case SSH_KEYTYPE_ED25519: loc = g->ed25519_key; break;
#endif
					default:
						fprintf(stderr, "WARNING: unsupported key type in %s\n", optarg);
						loc = NULL;
						break;
				}

				if (loc) {
					free(loc);
					loc = strdup(optarg);
				}
			}

			case 'b':
				free(g->bind_address);
				g->bind_address = strdup(optarg);
				break;

			case 'p':
				free(g->bind_port);
				g->bind_port = strdup(optarg);
				break;

			case 'P':
				free(g->pid_file);
				g->pid_file = strdup(optarg);
				break;

			case 'n':
				free(g->daemon_name);
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
				/* unreachable */
				/* no break */

			case 'v':
				version(g);
				/* unreachable */
				/* no break */

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
		g->pid_file = strdup("/run/ssh-honeypotd/ssh-honeypotd.pid");
	}
	else if (g->pid_file[0] != '/') {
		char buf[PATH_MAX+1];
		char* cwd    = getcwd(buf, PATH_MAX + 1);
		char* newbuf = NULL;

		if (cwd) {
			size_t cwd_len = strlen(cwd);
			size_t pid_len = strlen(g->pid_file);
			newbuf         = calloc(cwd_len + pid_len + 2, 1);
			if (newbuf) {
				memcpy(newbuf, cwd, cwd_len);
				newbuf[cwd_len] = '/';
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
