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

static struct passwd* find_account(void)
{
	struct passwd* pwd;

	pwd = getpwnam("nobody");
	if (!pwd) {
		pwd = getpwnam("daemon");
	}

	return pwd;
}

int drop_privs(const struct globals_t* globals)
{
	if (0 == geteuid()) {
		struct passwd* pwd = find_account();
		if (!pwd) {
			return DP_NO_UNPRIV_ACCOUNT;
		}

		if (
			   setgroups(0, NULL)
			|| setgid(pwd->pw_gid)
			|| setuid(pwd->pw_uid)
		) {
			return DP_GENERAL_FAILURE;
		}
	}

	return 0;
}
