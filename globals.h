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
	ssh_event event;
	pthread_t thread;
	int port;
	int my_port;
	char ipstr[INET6_ADDRSTRLEN];
	char my_ipstr[INET6_ADDRSTRLEN];
};

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wpadded"
struct globals_t {
	char* rsa_key;
	char* dsa_key;
	char* ecdsa_key;
	char* ed25519_key;
	char* bind_address;
	char* bind_port;
#ifndef MINIMALISTIC_BUILD
	char* pid_file;
	char* daemon_name;
#endif

	ssh_bind sshbind;

	pthread_mutex_t mutex;

	struct connection_info_t* head;
	struct connection_info_t* tail;

	volatile size_t n_threads;
	volatile sig_atomic_t terminate;

#ifndef MINIMALISTIC_BUILD
	int pid_fd;
	int foreground;
	int no_syslog;
	int uid_set;
	int gid_set;
	uid_t uid;
	gid_t gid;
#endif
};
#pragma clang diagnostic pop

extern struct globals_t globals;

void init_globals(struct globals_t* g);
void free_globals(struct globals_t* g);

#endif /* GLOBALS_H_ */
