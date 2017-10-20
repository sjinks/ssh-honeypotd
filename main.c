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

static void set_options(struct globals_t* g)
{
	ssh_bind_options_set(g->sshbind, SSH_BIND_OPTIONS_BINDADDR, g->bind_address);
	ssh_bind_options_set(g->sshbind, SSH_BIND_OPTIONS_BINDPORT_STR, g->bind_port);

	if (g->dsa_key) {
		ssh_bind_options_set(g->sshbind, SSH_BIND_OPTIONS_DSAKEY, g->dsa_key);
	}

	if (g->rsa_key) {
		ssh_bind_options_set(g->sshbind, SSH_BIND_OPTIONS_RSAKEY, g->rsa_key);
	}

#ifdef SSH_BIND_OPTIONS_ECDSAKEY
	if (g->ecdsa_key) {
		ssh_bind_options_set(g->sshbind, SSH_BIND_OPTIONS_ECDSAKEY, g->ecdsa_key);
	}
#endif

	if (g->host_key) {
		ssh_bind_options_set(g->sshbind, SSH_BIND_OPTIONS_HOSTKEY, g->host_key);
	}

	ssh_bind_options_set(g->sshbind, SSH_BIND_OPTIONS_BANNER, "OpenSSH");
}

static void daemonize(struct globals_t* g)
{
	int res;

	set_signals();
	res = drop_privs(g);
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

		exit(EXIT_FAILURE);
	}

	if (!g->foreground) {
		if (daemon(0, 0)) {
			perror("daemon");
			exit(EXIT_FAILURE);
		}
	}
}

static void spawn_thread(struct globals_t* g, pthread_attr_t* attr, ssh_session session)
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

	pthread_mutex_lock(&g->mutex);
	{
		if (!g->head) g->head       = conn;
		if (g->tail)  g->tail->next = conn;

		conn->prev  = g->tail;
		g->tail     = conn;
		num_threads = g->n_threads;
		++g->n_threads;
	}
	pthread_mutex_unlock(&g->mutex);

	if (num_threads > MAX_THREADS) {
		syslog(LOG_ERR, "Too many connections");
		finalize_connection(conn);
	}
	else if (pthread_create(&conn->thread, attr, worker, conn) != 0) {
		syslog(LOG_CRIT, "pthread_create() failed");
		finalize_connection(conn);
	}
}

static void main_loop(struct globals_t* g)
{
	pthread_attr_t attr;
	pthread_attr_init(&attr);
	pthread_attr_setstacksize(&attr, 65536);

	while (!g->terminate) {
		const long int timeout = SESSION_TIMEOUT;
		ssh_session session    = ssh_new();
		ssh_options_set(session, SSH_OPTIONS_TIMEOUT, &timeout);
		int r = ssh_bind_accept(g->sshbind, session);
		if (r == SSH_ERROR) {
			if (g->terminate) {
				break;
			}

			syslog(LOG_WARNING, "Error accepting a connection: %s\n", ssh_get_error(g->sshbind));
			continue;
		}

		spawn_thread(g, &attr, session);
	}

	pthread_attr_destroy(&attr);
}

static void goodbye(void)
{
	free_globals(&globals);
}

int main(int argc, char** argv)
{
	init_globals(&globals);
	atexit(goodbye);
	parse_options(argc, argv, &globals);

	globals.pid_fd = create_pid_file(globals.pid_file);
	if (globals.pid_fd == -1) {
		fprintf(stderr, "Error creating PID file %s\n", globals.pid_file);
		return EXIT_FAILURE;
	}

	if (globals.pid_fd == -2) {
		fprintf(stderr, "ssh-honeypotd is already running\n");
		return EXIT_SUCCESS;
	}

	set_options(&globals);

	if (ssh_bind_listen(globals.sshbind) < 0) {
		fprintf(stderr, "Error listening to socket: %s\n", ssh_get_error(globals.sshbind));
		return EXIT_FAILURE;
	}

	openlog(globals.daemon_name, LOG_PID, LOG_AUTH);
	daemonize(&globals);
	if (write_pid(globals.pid_fd)) {
		syslog(LOG_CRIT, "Failed to write to the PID file: %s", strerror(errno));
		return EXIT_FAILURE;
	}

	main_loop(&globals);
	return 0;
}
