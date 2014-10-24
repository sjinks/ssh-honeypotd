#include <stddef.h>
#include <signal.h>
#include <unistd.h>
#include <pwd.h>
#include <grp.h>
#include "daemon.h"
#include "globals.h"

static void signal_handler(int signal)
{
	globals.terminate = 1;
}

void set_signals(void)
{
	struct sigaction sa;
	sa.sa_handler = signal_handler;
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = 0;
	sigaction(SIGTERM, &sa, NULL);
	sigaction(SIGQUIT, &sa, NULL);
	sigaction(SIGINT,  &sa, NULL);

	sa.sa_handler = SIG_IGN;
	sigaction(SIGHUP, &sa, NULL);
}

static int find_account(uid_t* uid, gid_t* gid)
{
	struct passwd* pwd;

	pwd = getpwnam("nobody");
	if (!pwd) {
		pwd = getpwnam("daemon");
	}

	if (pwd) {
		*uid = pwd->pw_uid;
		*gid = pwd->pw_gid;
		return 0;
	}

	return -1;
}

int drop_privs(struct globals_t* globals)
{
	if (0 == geteuid()) {
		if (!globals->uid_set || !globals->gid_set) {
			uid_t uid;
			gid_t gid;

			if (-1 == find_account(&uid, &gid)) {
				return DP_NO_UNPRIV_ACCOUNT;
			}

			if (!globals->uid_set) {
				globals->uid_set = 1;
				globals->uid     = uid;
			}

			if (!globals->gid_set) {
				globals->gid_set = 1;
				globals->gid     = gid;
			}
		}

		if (
			   setgroups(0, NULL)
			|| setgid(globals->gid)
			|| setuid(globals->uid)
		) {
			return DP_GENERAL_FAILURE;
		}
	}

	return 0;
}
