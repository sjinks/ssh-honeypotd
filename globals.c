#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <libssh/callbacks.h>
#include "globals.h"

void init_globals(struct globals_t* g)
{
	memset(g, 0, sizeof(struct globals_t));

	pthread_mutex_init(&g->mutex, NULL);
	ssh_threads_set_callbacks(ssh_threads_get_pthread());
	if (ssh_init() == -1) {
		fprintf(stderr, "ssh_init() failed\n");
		exit(EXIT_FAILURE);
	}

	g->sshbind = ssh_bind_new();
	g->pid_fd  = -1;
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

	free(g->rsa_key);
	free(g->dsa_key);
	free(g->ecdsa_key);
	free(g->ed25519_key);
	free(g->bind_address);
	free(g->bind_port);
	free(g->pid_file);
	free(g->daemon_name);

	wait_for_threads(g);
	pthread_mutex_destroy(&g->mutex);

	ssh_bind_free(g->sshbind);
	ssh_finalize();
}
