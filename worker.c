#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include <string.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <libssh/libssh.h>
#include <libssh/callbacks.h>
#include <libssh/server.h>
#include "worker.h"
#include "globals.h"
#include "log.h"

static void get_ip_port(const struct sockaddr_storage* addr, char* ipstr, int* port)
{
	assert(addr  != NULL);
	assert(ipstr != NULL);
	assert(port  != NULL);

	if (addr->ss_family == AF_INET) {
		const struct sockaddr_in* s = (const struct sockaddr_in*)addr;
		*port = ntohs(s->sin_port);
		inet_ntop(AF_INET, &s->sin_addr, ipstr, INET6_ADDRSTRLEN);
	}
	else if (addr->ss_family == AF_INET6) {
		const struct sockaddr_in6* s = (const struct sockaddr_in6*)addr;
		*port = ntohs(s->sin6_port);
		inet_ntop(AF_INET6, &s->sin6_addr, ipstr, INET6_ADDRSTRLEN);
	}
	else {
		/* Should not happen */
		assert(0);
	}
}

static int auth_password(ssh_session session, const char* user, const char* pass, void* userdata)
{
	struct connection_info_t* conn = (struct connection_info_t*)userdata;

	my_log(
		LOG_WARNING,
		"Failed password for %s from %s port %d ssh%d (target: %s:%d, password: %s)",
		user,
		conn->ipstr,
		conn->port,
		ssh_get_version(conn->session),
		conn->my_ipstr,
		conn->my_port,
		pass
	);

	return SSH_AUTH_DENIED;
}

static void handle_session(struct connection_info_t* conn)
{
	conn->event = ssh_event_new();
	if (!conn->event) {
		my_log(LOG_ALERT, "Could not create polling context");
		return;
	}

	struct ssh_server_callbacks_struct server_cb;
	memset(&server_cb, 0, sizeof(server_cb));
	ssh_callbacks_init(&server_cb);
	server_cb.userdata               = conn;
	server_cb.auth_password_function = auth_password;

	ssh_set_auth_methods(conn->session, SSH_AUTH_METHOD_PASSWORD);
	ssh_set_server_callbacks(conn->session, &server_cb);

	if (SSH_OK != ssh_handle_key_exchange(conn->session)) {
		my_log(
			LOG_WARNING,
			"Did not receive identification string from %s:%d (target: %s:%d): %s",
			conn->ipstr,
			conn->port,
			conn->my_ipstr,
			conn->my_port,
			ssh_get_error(conn->session)
		);

		return;
	}

	ssh_event_add_session(conn->event, conn->session);
	while (!globals.terminate && ssh_event_dopoll(conn->event, 100) != SSH_ERROR) {
		;
	}
}

void* worker(void* arg)
{
	struct sockaddr_storage addr;
	struct connection_info_t* conn = (struct connection_info_t*)arg;

	socket_t sock = ssh_get_fd(conn->session);
	socklen_t len = sizeof(addr);

	if (!getpeername(sock, (struct sockaddr*)&addr, &len)) {
		get_ip_port(&addr, conn->ipstr, &conn->port);
	}

	if (!getsockname(sock, (struct sockaddr*)&addr, &len)) {
		get_ip_port(&addr, conn->my_ipstr, &conn->my_port);
	}

	handle_session(conn);
	finalize_connection(conn);
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

	if (conn->event) {
		ssh_event_free(conn->event);
	}

	ssh_disconnect(session);
	ssh_free(session);
	free(conn);
}
