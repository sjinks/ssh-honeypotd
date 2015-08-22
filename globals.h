	#ifndef GLOBALS_H_
#define GLOBALS_H_

#include <stddef.h>
#include <sys/types.h>
#include <signal.h>
#include <pthread.h>
#include <libssh/server.h>

struct connection_info_t {
	struct connection_info_t* prev;
	struct connection_info_t* next;
	ssh_session session;
	pthread_t thread;
};

struct globals_t {
	char* rsa_key;
	char* dsa_key;
#ifdef SSH_BIND_OPTIONS_ECDSAKEY
	char* ecdsa_key;
#endif
	char* host_key;
	char* bind_address;
	char* bind_port;
	char* pid_file;
	char* daemon_name;

	ssh_bind sshbind;

	volatile size_t n_threads;
	volatile sig_atomic_t terminate;

	pthread_mutex_t mutex;

	struct connection_info_t* head;
	struct connection_info_t* tail;

	int pid_fd;
	int foreground;
	int uid_set;
	int gid_set;
	uid_t uid;
	gid_t gid;
};

extern struct globals_t globals;

void init_globals(struct globals_t* g);
void free_globals(struct globals_t* g);

#endif /* GLOBALS_H_ */
