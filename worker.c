#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <syslog.h>
#include <libssh/libssh.h>
#include <libssh/server.h>
#include "worker.h"
#include "globals.h"

void* worker(void* arg)
{
	struct sockaddr_storage addr;
	char ipstr[INET6_ADDRSTRLEN];
	int port;

	struct connection_info_t* conn = (struct connection_info_t*)arg;

	ssh_session session = conn->session;
	socket_t sock       = ssh_get_fd(session);
	int version         = ssh_get_version(session);
	socklen_t len       = sizeof(addr);

	if (!getpeername(sock, (struct sockaddr*)&addr, &len)) {
		if (addr.ss_family == AF_INET) {
			struct sockaddr_in* s = (struct sockaddr_in*)&addr;
			port = ntohs(s->sin_port);
			inet_ntop(AF_INET, &s->sin_addr, ipstr, sizeof(ipstr));
		}
		else if (addr.ss_family == AF_INET6) { // AF_INET6
			struct sockaddr_in6* s = (struct sockaddr_in6*)&addr;
			port = ntohs(s->sin6_port);
			inet_ntop(AF_INET6, &s->sin6_addr, ipstr, sizeof(ipstr));
		}
		else {
			/* Should not happen */
			assert(0);
		}
	}
	else {
		ipstr[0] = '?';
		ipstr[1] = 0;
		port     = -1;
	}

	if (SSH_OK == ssh_handle_key_exchange(session)) {
#if LIBSSH_VERSION_INT >= SSH_VERSION_INT(0, 6, 0)
		ssh_set_auth_methods(session, SSH_AUTH_METHOD_PASSWORD);
#endif

		do {
			ssh_message message = ssh_message_get(session);
			if (!message || globals.terminate) {
				break;
			}

			switch (ssh_message_type(message)) {
				case SSH_REQUEST_AUTH:
					switch (ssh_message_subtype(message)) {
						case SSH_AUTH_METHOD_PASSWORD:
							syslog(LOG_WARNING, "Failed password for %s from %s port %d ssh%d (%s)", ssh_message_auth_user(message), ipstr, port, version, ssh_message_auth_password(message));
							/* no break */

						default:
							ssh_message_auth_set_methods(message, SSH_AUTH_METHOD_PASSWORD);
							ssh_message_reply_default(message);
							break;
					}

					break;

				default:
					ssh_message_reply_default(message);
					break;
			}

			ssh_message_free(message);
		} while (!globals.terminate);
	}
	else {
		printf("ssh_handle_key_exchange: %s\n", ssh_get_error(session));
	}

	finalize_connection(conn);
	if (!globals.terminate) {
		pthread_detach(pthread_self());
	}

	return 0;
}

void finalize_connection(struct connection_info_t* conn)
{
	ssh_session session = conn->session;

	pthread_mutex_lock(&globals.mutex);
	{
		if (conn->prev) {
			conn->prev->next = conn->next;
		}

		if (conn->next) {
			conn->next->prev = conn->prev;
		}

		if (globals.tail == conn) {
			globals.tail = conn->prev;
		}

		if (globals.head == conn) {
			globals.head = conn->next;
		}

		--globals.n_threads;
	}
	pthread_mutex_unlock(&globals.mutex);

	ssh_disconnect(session);
	ssh_free(session);
	free(conn);
}
