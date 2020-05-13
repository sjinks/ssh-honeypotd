#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
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
	{ "no-syslog",  no_argument,       0, 'x' },
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
		"                        (if not specified, the daemon will run in foreground mode)\n"
		"  -n, --name NAME       the name of the daemon for syslog\n"
		"                        (default: ssh-honeypotd)\n"
		"  -u, --user USER       drop privileges and switch to this USER\n"
		"                        (default: daemon or nobody)\n"
		"  -g, --group GROUP     drop privileges and switch to this GROUP\n"
		"                        (default: daemon or nogroup)\n"
        "  -x, --no-syslog       log messages only to stderr\n"
        "                        (only works with --foreground)\n"
		"  -f, --foreground      do not daemonize\n"
		"  -h, --help            display this help and exit\n"
		"  -v, --version         output version information and exit\n\n"
		"-k option must be specified at least once.\n\n"
		"Please note:\n"
		"  - ECDSA keys are supported if ssh-honeypotd is compiled against libssh 0.6.4+\n"
		"  - ED25519 keys are supported if ssh-honeypotd is compiled against libssh 0.7.0+\n\n"
		"ssh-honeypotd was compiled against libssh " SSH_STRINGIFY(LIBSSH_VERSION) "\n"
		"libssh used: %s\n\n"
		"Please report bugs here: <https://github.com/sjinks/ssh-honeypotd/issues>\n",
		ssh_version(0)
	);

	exit(0);
}

#if defined(__GNUC__) || defined(__clang__)
__attribute__((noreturn))
#endif
static void version(struct globals_t* g)
{
	printf(
		"ssh-honeypotd 2.0.0\n"
		"Copyright (c) 2014-2020, Volodymyr Kolesnykov <volodymyr@wildwolf.name>\n"
		"License: MIT <http://opensource.org/licenses/MIT>\n"
	);

	exit(0);
}

static void check_alloc(void* p, const char* api)
{
	if (!p) {
		perror(api);
		exit(EXIT_FAILURE);
	}
}

#if defined(__GNUC__) || defined(__clang__)
__attribute__((malloc, returns_nonnull))
#endif
static char* my_strdup(const char *s)
{
	char* retval = strdup(s);
	check_alloc(retval, "strdup");
	return retval;
}

static void resolve_pid_file(struct globals_t* g)
{
	if (g->pid_file) {
		if (g->pid_file[0] != '/') {
			char buf[PATH_MAX+1];
			char* cwd    = getcwd(buf, PATH_MAX + 1);
			char* newbuf = NULL;

			/* If the current directory is not below the root directory of
			* the current process (e.g., because the process set a new
			* filesystem root using chroot(2) without changing its current
			* directory into the new root), then, since Linux 2.6.36,
			* the returned path will be prefixed with the string
			* "(unreachable)". Such behavior can also be caused by
			* an unprivileged user by changing the current directory into
			* another mount namespace. When dealing with paths from
			* untrusted sources, callers of these functions should consider
			* checking whether the returned path starts with '/' or '('
			* to avoid misinterpreting an unreachable path as a relative path.
			*/
			if (cwd && cwd[0] == '/') {
				size_t cwd_len = strlen(cwd);
				size_t pid_len = strlen(g->pid_file);
				newbuf         = calloc(cwd_len + pid_len + 2, 1);
				check_alloc(newbuf, "calloc");
				memcpy(newbuf, cwd, cwd_len);
				newbuf[cwd_len] = '/';
				memcpy(newbuf + cwd_len + 1, g->pid_file, pid_len);
				free(g->pid_file);
				g->pid_file = newbuf;
			}
			else {
				fprintf(stderr, "ERROR: Failed to get the current directory: %s\n", strerror(errno));
				free(g->pid_file);
				exit(EXIT_FAILURE);
			}
		}
	}
}

static void set_defaults(struct globals_t* g)
{
	if (!g->bind_address) {
		g->bind_address = my_strdup("0.0.0.0");
	}

	if (!g->bind_port) {
		g->bind_port = my_strdup("22");
	}

	if (!g->daemon_name) {
		g->daemon_name = my_strdup("ssh-honeypotd");
	}
}

void parse_options(int argc, char** argv, struct globals_t* g)
{
	assert(g != NULL);

	while (1) {
		int option_index = 0;
		int c = getopt_long(argc, argv, "r:d:e:k:b:p:P:n:u:g:vfxh", long_options, &option_index);
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
				ssh_key_free(key);
				char** loc   = NULL;
				switch (key_type) {
					case SSH_KEYTYPE_DSS:     loc = &g->dsa_key;     break;
					case SSH_KEYTYPE_RSA:     loc = &g->rsa_key;     break;
#if LIBSSH_VERSION_INT >= SSH_VERSION_INT(0, 6, 4)
					case SSH_KEYTYPE_ECDSA:
#if LIBSSH_VERSION_INT >= SSH_VERSION_INT(0, 9, 0)
					case SSH_KEYTYPE_ECDSA_P256:
					case SSH_KEYTYPE_ECDSA_P384:
					case SSH_KEYTYPE_ECDSA_P521:
#endif
						loc = &g->ecdsa_key;
						break;
#endif
#if LIBSSH_VERSION_INT >= SSH_VERSION_INT(0, 7, 0)
					case SSH_KEYTYPE_ED25519: loc = &g->ed25519_key; break;
#endif
					default:
						fprintf(stderr, "WARNING: unsupported key type in %s (%d)\n", optarg, key_type);
						loc = NULL;
						break;
				}

				if (loc) {
					free(*loc);
					*loc = my_strdup(optarg);
				}

				break;
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

			case 'x':
				g->no_syslog = 1;
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

	set_defaults(g);
	resolve_pid_file(g);

	if (!g->pid_file) {
		g->foreground = 1;
	}

	if (!g->foreground) {
		g->no_syslog = 0;
	}
}
