#ifndef DAEMON_H_
#define DAEMON_H_

#include <sys/types.h>
#include "globals.h"

#define DP_NO_UNPRIV_ACCOUNT         1
#define DP_GENERAL_FAILURE           2

void set_signals(void);
int drop_privs(struct globals_t* globals);

#endif /* DAEMON_H_ */
