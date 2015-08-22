#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <syslog.h>
#include <errno.h>
#include <libssh/server.h>
#include "globals.h"
#include "daemon.h"
#include "cmdline.h"
#include "worker.h"
#include "pidfile.h"

#define MAX_THREADS      100
#define SESSION_TIMEOUT  120

struct globals_t globals;

static void set_options(struct globals_t* globals)
{
	ssh_bind_options_set(globals->sshbind, SSH_BIND_OPTIONS_BINDADDR, globals->bind_address);
	ssh_bind_options_set(globals->sshbind, SSH_BIND_OPTIONS_BINDPORT_STR, globals->bind_port);

	if (globals->dsa_key) {
		ssh_bind_options_set(globals->sshbind, SSH_BIND_OPTIONS_DSAKEY, globals->dsa_key);
	}

	if (globals->rsa_key) {
		ssh_bind_options_set(globals->sshbind, SSH_BIND_OPTIONS_RSAKEY, globals->rsa_key);
	}

#ifdef SSH_BIND_OPTIONS_ECDSAKEY
	if (globals->ecdsa_key) {
		ssh_bind_options_set(globals->sshbind, SSH_BIND_OPTIONS_ECDSAKEY, globals->ecdsa_key);
	}
#endif

	if (globals->host_key) {
		ssh_bind_options_set(globals->sshbind, SSH_BIND_OPTIONS_HOSTKEY, globals->host_key);
	}

	ssh_bind_options_set(globals->sshbind, SSH_BIND_OPTIONS_BANNER, "OpenSSH");
}

static void daemonize(struct globals_t* globals)
{
	int res;

	set_signals();
	res = drop_privs(globals);
	if (res != 0) {
		switch (res) {
			case DP_NO_UNPRIV_ACCOUNT:
				fprintf(stderr, "ERROR: Failed to find an unprivileged account\n");
				break;

			case DP_GENERAL_FAILURE:
			default:
				fprintf(stderr, "ERROR: Failed to drop privileges\n");
				break;
		}

		free_globals(globals);
		exit(EXIT_FAILURE);
	}

	if (!globals->foreground) {
		if (daemon(0, 0)) {
			perror("daemon");
			free_globals(globals);
			exit(EXIT_FAILURE);
		}
	}
}

void spawn_thread(struct globals_t* globals, pthread_attr_t* attr, ssh_session session)
{
	size_t num_threads;
	struct connection_info_t* conn = malloc(sizeof(struct connection_info_t));
	if (!conn) {
		syslog(LOG_ALERT, "malloc() failed, out of memory");
		ssh_disconnect(session);
		ssh_free(session);
		return;
	}

	conn->next    = NULL;
	conn->session = session;

	pthread_mutex_lock(&globals->mutex);
	{
		if (!globals->head) globals->head       = conn;
		if (globals->tail)  globals->tail->next = conn;

		conn->prev    = globals->tail;
		globals->tail = conn;
		num_threads   = globals->n_threads;
		++globals->n_threads;
	}
	pthread_mutex_unlock(&globals->mutex);

	if (num_threads > MAX_THREADS) {
		syslog(LOG_ERR, "Too many connections");
		finalize_connection(conn);
	}
	else if (pthread_create(&conn->thread, attr, worker, conn) != 0) {
		syslog(LOG_CRIT, "pthread_create() failed");
		finalize_connection(conn);
	}
}

void main_loop(struct globals_t* globals)
{
	pthread_attr_t attr;
	pthread_attr_init(&attr);
	pthread_attr_setstacksize(&attr, 65536);

	while (!globals->terminate) {
		const long int timeout = SESSION_TIMEOUT;
		ssh_session session    = ssh_new();
		ssh_options_set(session, SSH_OPTIONS_TIMEOUT, &timeout);
		int r = ssh_bind_accept(globals->sshbind, session);
		if (r == SSH_ERROR) {
			if (globals->terminate) {
				break;
			}

			syslog(LOG_WARNING, "Error accepting a connection: %s\n", ssh_get_error(globals->sshbind));
			continue;
		}

		spawn_thread(globals, &attr, session);
	}

	pthread_attr_destroy(&attr);
}

int main(int argc, char** argv)
{
	init_globals(&globals);
	parse_options(argc, argv, &globals);

	globals.pid_fd = create_pid_file(globals.pid_file);
	if (globals.pid_fd == -1) {
		fprintf(stderr, "Error creating PID file %s\n", globals.pid_file);
		free_globals(&globals);
		return EXIT_FAILURE;
	}

	if (globals.pid_fd == -2) {
		fprintf(stderr, "ssh-honeypotd is already running\n");
		free_globals(&globals);
		return EXIT_SUCCESS;
	}

	set_options(&globals);

	if (ssh_bind_listen(globals.sshbind) < 0) {
		fprintf(stderr, "Error listening to socket: %s\n", ssh_get_error(globals.sshbind));
		free_globals(&globals);
		return EXIT_FAILURE;
	}

	openlog(globals.daemon_name, LOG_PID, LOG_AUTH);
	daemonize(&globals);
	if (write_pid(globals.pid_fd)) {
		syslog(LOG_CRIT, "Failed to write to the PID file: %s", strerror(errno));
		free_globals(&globals);
		return EXIT_FAILURE;
	}

	main_loop(&globals);
	free_globals(&globals);
	return 0;
}
