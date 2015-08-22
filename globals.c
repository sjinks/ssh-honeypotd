#include <stdio.h>
#include <stdlib.h>
#include <libssh/callbacks.h>
#include "globals.h"

void init_globals(struct globals_t* g)
{
	g->rsa_key      = NULL;
	g->dsa_key      = NULL;
#ifdef SSH_BIND_OPTIONS_ECDSAKEY
	g->ecdsa_key    = NULL;
#endif
	g->host_key     = NULL;
	g->bind_address = NULL;
	g->bind_port    = NULL;
	g->pid_file     = NULL;
	g->daemon_name  = NULL;

	g->n_threads = 0;
	g->terminate = 0;

	pthread_mutex_init(&g->mutex, NULL);

	g->head = NULL;
	g->tail = NULL;

	ssh_threads_set_callbacks(ssh_threads_get_pthread());
	if (ssh_init() == -1) {
		fprintf(stderr, "ssh_init() failed\n");
		exit(EXIT_FAILURE);
	}

	g->sshbind = ssh_bind_new();

	g->pid_fd     = -1;
	g->foreground = 0;
	g->uid_set    = 0;
	g->gid_set    = 0;
}

static void wait_for_threads(struct globals_t* g)
{
	size_t num_threads;
	pthread_t thread;

	do {
		pthread_mutex_lock(&g->mutex);
		{
			num_threads = g->n_threads;
			if (num_threads) {
				thread = g->head->thread;
			}
		}
		pthread_mutex_unlock(&g->mutex);

		if (num_threads) {
			pthread_kill(thread, SIGTERM);
			pthread_join(thread, NULL);
		}
	} while (num_threads > 0);
}

void free_globals(struct globals_t* g)
{
	if (g->pid_fd >= 0) {
		unlink(g->pid_file);
		close(g->pid_fd);
	}

	if (g->rsa_key)      free(g->rsa_key);
	if (g->dsa_key)      free(g->dsa_key);
#ifdef SSH_BIND_OPTIONS_ECDSAKEY
	if (g->ecdsa_key)    free(g->ecdsa_key);
#endif
	if (g->host_key)     free(g->host_key);
	if (g->bind_address) free(g->bind_address);
	if (g->bind_port)    free(g->bind_port);
	if (g->pid_file)     free(g->pid_file);
	if (g->daemon_name)  free(g->daemon_name);

	wait_for_threads(g);
	pthread_mutex_destroy(&g->mutex);

	ssh_bind_free(g->sshbind);
	ssh_finalize();
}
